#include "http_conn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
std::map<std::string, std::string> users;
void HTTPConn::initMysqlResult(ConnectionPool* conn_pool) {
    // 1.取一个连接
    MYSQL* mysql = nullptr;
    ConnectionRAII mysql_con(&mysql, conn_pool);

    // 查询用户和密码
    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    
    }

    // 2.得到查询结果
    MYSQL_RES* result = mysql_store_result(mysql);

    // 3.将结果存储在map中

    // // 结果的列数
    // int num_fields = mysql_num_fields(result);

    // // 返回所有字段结构的数组
    // MYSQL_FIELD* = mysql_fetch_field(result);

    // typedef char **MYSQL_ROW;
    // MYSQL_ROW 实际上是一个指向 char*（C 字符串）数组的指针
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        std::string u_name(row[0]);
        std::string u_passwd(row[1]);
        users[u_name] = u_passwd;
    }
}
// TODO：将utils中的函数设置为静态，直接调用即可，不用重新写，但是静态成员好像已经被timer独享了，设置为共享可能出问题
int setNonBlocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addFD(int epoll_fd, int fd, bool one_shot, int TRIGMode) {
    // epoll_fd：epoll事件表，fd:要操作的文件描述符
    epoll_event event; // event：指向 epoll_event 结构体的指针，包含要操作的事件信息。
    event.data.fd = fd;
    // 初始化都要选择输入事件和远程关闭连接事件
    if (1 == TRIGMode) {
        // 选择开启ET模式
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    }
    else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if (one_shot) {
        // 追加oneshot标志位
        event.events |= EPOLLONESHOT;

    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}
// 从内核时间表删除描述符
void removeFD(int epoll_fd, int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
// 将事件重置为EPOLLONESHOT
void modFD(int epoll_fd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if (1 == TRIGMode) {
        event.events = ev | EPOLLET | EPOLLONESHOT |EPOLLRDHUP;

    }
    else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

int HTTPConn::m_user_count_ = 0;
int HTTPConn::m_epoll_fd_ = -1; // -1表示无效



// 关闭连接，关闭一个连接，客户总量减1
void HTTPConn::closeConn(bool real_close) {
    if (real_close && (m_sock_fd_ != -1)) {
        printf("close %d\n", m_sock_fd_);
        removeFD(m_epoll_fd_, m_sock_fd_);
        m_sock_fd_ = -1;
        --m_user_count_;
    }
}
// 初始化连接
void HTTPConn::init(int sock_fd, const sockaddr_in &addr, char * root, int TRIGMode, int close_log, std::string user, std::string passwd, std::string sql_name) {
    m_sock_fd_ = sock_fd;
    m_address_ = addr;

    addFD(m_epoll_fd_, sock_fd, true, m_TRIGMode_);
    ++m_user_count_;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root_ = root;
    m_TRIGMode_ = TRIGMode;
    m_close_log_ = close_log;

    // char* 类型要用函数复制
    strcpy(sql_user_, user.c_str());
    strcpy(sql_passwd_, passwd.c_str());
    strcpy(sql_name_, sql_name.c_str());

    init();


}

void HTTPConn::init() {
    mysql_ = nullptr;
    bytes_to_send_ = 0;
    bytes_have_send_ = 0;
    m_check_state_ = CHECK_STATE_REQUESTLINE;
    m_linger_ = false;
    m_method_ = GET;
    m_url_ = 0;
    m_version_ = 0;
    m_content_length_ = 0;
    m_host_ = 0;
    m_start_line_ = 0;
    m_checked_idx_ = 0;
    m_read_idx_ = 0;
    m_write_idx_ = 0;
    cgi_ = 0;
    m_state = 0;
    timer_flag_ = 0;
    improv_ = 0;

    // 对char*类型的初始化
    memset(m_read_buf_, '\0', READ_BUFFER_SIZE);
    memset(m_write__buf_, '\0', WRITE_BUFFER_SIZE);
    memset(m_read_file_, '\0', FILENAME_LEN);

}
bool HTTPConn::readOnce() {
    if (m_read_idx_ >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    // LT模式，即使应用程序没有对事件做出响应，内核仍然会持续通知事件，直到事件被处理
    // 因此只需要解决当前事件即可，如果处理的同时还有新的数据到来，等待内核马上到来的下一次通知即可
    if (0 == m_TRIGMode_) {
        bytes_read = recv(m_sock_fd_, m_read_buf_ + m_read_idx_, READ_BUFFER_SIZE - m_read_idx_, 0);
        // 更新已读的字节数
        m_read_idx_ += bytes_read;

        if (bytes_read <= 0) {
            return false;
        }
        return true;


    }
    // ET模式，内核只通知一次
    else {
        while (true) {
            bytes_read = recv(m_sock_fd_, m_read_buf_ + m_read_idx_, READ_BUFFER_SIZE - m_read_idx_, 0);
            if (bytes_read == -1) {
                // 在非阻塞ET模式下，当缓冲区没有数据可读时，
                // 会返回EAGAIN或EWOULDBLOCK错误码，这时需要退出循环，等待下一次事件触发。
                // 非阻塞ET模式下，需要一次性将数据读完
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                // 其它错误则返回false
                return false;
                
            }
            else if (bytes_read == 0) {
                return false;
            }
            m_read_idx_ += bytes_read;
        }
        return true;
    }

    return true;
}

void HTTPConn::process()
{
}


bool HTTPConn::write()
{
    return false;
}

sockaddr_in *HTTPConn::getAddress()
{
    return nullptr;
}





HTTPConn::HTTP_CODE HTTPConn::processRead() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    // m_check_state_被初始化为CHECK_STATE_REQUESTLINE
    while ((m_check_state_ == CHECK_STATE_CONTENT && line_status == LINE_OK) 
            || ((line_status = parseLine()) == LINE_OK)) {
        // 获取每一行信息，存入text
        text = getLine();
        // 先获取前一行text再更新m_start_line_，getLine会用到m_checked_idx_
        m_start_line_ = m_checked_idx_;
        LOG_INFO("%s", text);
        switch (m_check_state_) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parseRequestLine(text);

                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parseHeaders(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST) {
                    return doRequest();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parseContent(text);
                if (ret == GET_REQUEST) {
                    return doRequest();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }

    }
    return INTERNAL_ERROR;
}

bool HTTPConn::processWrite(HTTP_CODE ret)
{
    return false;
}

// 解析请求行：请求方法、目标url、http版本号
HTTPConn::HTTP_CODE HTTPConn::parseRequestLine(char *text) {
    // 初步定位url位置
    m_url_ = strpbrk(text, "\t"); // 寻找text中第一次出现\t的位置
    if (!m_url_) {
        return BAD_REQUEST;
    }
    *m_url_++ = '\0'; // 先+6+,再间址，改为\0，是为了后面赋值给method时，自动读取到该位置就停止读取
    // 获取请求方法，url前面部分就是请求方法
    char* method = text;

    // 检查请求方法是否合法，strcasecmp忽略大小写
    if (strcasecmp(method, "GET") == 0) {
        m_method_ = GET;

    }
    else if (strcasecmp(method, "POST") == 0) {
        m_method_ = POST;
        cgi_ = 1;
    }
    else {
        return BAD_REQUEST;
    }

    // 精细定位url：跳过所有制表符或空格
    m_url_ += strspn(m_url_, "\t"); // strspn返回空格或制表符的数量
    
    // 重复以上方法 
    m_version_ = strpbrk(m_url_, "\t");
    if (!m_version_) {
        return BAD_REQUEST;
    }
    *m_version_++ = '\0'; // 置为\0后，m_url_只包括了url部分
    m_version_ += strspn(m_version_, "\t");
    
    // 检查version是否合法,只支持HTTP/1.1
    if (strcasecmp(m_version_, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    // 提取有效的url
    // 1.有前缀http://和域名
    if (strncasecmp(m_url_, "http://", 7) == 0) {
        // 去除前缀
        m_url_ += 7;
        // 去除域名
        m_url_ = strchr(m_url_, '/');
    }
    // 2.有前缀https://和域名
    if (strncasecmp(m_url_, "https://", 8) == 0) {
        m_url_ += 8;
        m_url_ = strchr(m_url_, '/');
    }

    // 不存在路径或者路径开头不是/
    if (!m_url_ || m_url_[0] != '/') {
        return BAD_REQUEST;
    }

    // 当路径为/时（根路径），显示判断页面
    if (strlen(m_url_) == 1) {
        strcat(m_url_, "judge.html");
    }
    // 主状态机状态转移
    m_check_state_ = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析请求头
HTTPConn::HTTP_CODE HTTPConn::parseHeaders(char *text)
{
    return HTTP_CODE();
}


HTTPConn::HTTP_CODE HTTPConn::parseContent(char *text) {

    return HTTP_CODE();
}

HTTPConn::HTTP_CODE HTTPConn::doRequest()
{
    return HTTP_CODE();
}

char *HTTPConn::getLine()
{
    return m_read_buf_ + m_start_line_;
}

// 将报文格式转化为每一行以\0结尾
// 在HTTP报文中，每一行的数据由\r\n作为结束字符，空行则是仅仅是字符\r\n
HTTPConn::LINE_STATUS HTTPConn::parseLine() {
    char temp;
    for (; m_checked_idx_ < m_read_idx_; ++m_checked_idx_) {
        temp = m_read_buf_[m_checked_idx_];
        // 当前是\r
        if (temp == '\r') {
            // \r是最后一个字符，缺少\n还需要继续接收
            if ((m_checked_idx_ + 1) == m_read_idx_) {
                // 读取行不完整
                return LINE_OPEN;
            }
            // \r\n一起出现，用\0代替
            else if (m_read_buf_[m_checked_idx_ + 1] == '\n') {
                m_read_buf_[m_checked_idx_++] = '\0';
                m_read_buf_[m_checked_idx_++] = '\0';
                return LINE_OK;
            }
            // 当前是'\r',但不是结尾，下一个字符也不是\n，则语法错误
            return LINE_BAD;
        }
        // 当前是\n
        // (一般是上次读取到\r就到了buffer末尾，没有接收完整，再次接收时会出现这种情况)
        // 否则按道理来说前一个情况已经解决了当前的情况
        else if (temp == '\n') {
            // TODO：搞清楚为什么是>1而不是>=1
            // 前一个字符是\r
            if (m_checked_idx_ > 1 && m_read_buf_[m_checked_idx_ - 1] == '\r') {
                m_read_buf_[m_checked_idx_ - 1] = '\0';
                m_read_buf_[m_checked_idx_++] = '\0';
                // 完整读取一行
                return LINE_OK;
            }
            // 前一个字符不是\r，则语法错误
            return LINE_BAD;
        }

    }
    // 运行这里说明没有以\r\n结束，说明读取的行不完整
    return LINE_OPEN;
}

void HTTPConn::unmap() {

}

bool HTTPConn::addResponse(const char *format, ...)
{
    return false;
}

bool HTTPConn::addContent(const char *content)
{
    return false;
}

bool HTTPConn::addStatusLine(int status, const char *title)
{
    return false;
}

bool HTTPConn::addHeaders(int content_length)
{
    return false;
}

bool HTTPConn::addContentType()
{
    return false;
}

bool HTTPConn::addContentLength(int content)
{
    return false;
}

bool HTTPConn::addLinger()
{
    return false;
}

bool HTTPConn::addBlankLine()
{
    return false;
}
