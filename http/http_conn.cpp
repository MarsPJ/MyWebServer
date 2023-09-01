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

// TODO:可以直接设置为static类型吗
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
    memset(m_write_buf_, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file_, '\0', FILENAME_LEN);

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

// TODO：不太理解这段的具体逻辑
void HTTPConn::process() {
    HTTP_CODE read_ret = processRead();
    if (read_ret == NO_REQUEST) {
        modFD(m_epoll_fd_, m_sock_fd_, EPOLLIN, m_TRIGMode_);
        return;
    }
    bool write_ret = processWrite(read_ret);
    // 如果写入不成功，则直接关闭连接
    // TODO：为什么这么粗暴关闭？
    if (!write_ret) {
        closeConn();
    }
    modFD(m_epoll_fd_, m_sock_fd_, EPOLLOUT, m_TRIGMode_);
}


bool HTTPConn::write() {
    /**
     * writev 将指定的多个内存块的数据按顺序写入到文件描述符中。
     * 这种技术对于减少系统调用的次数，提高数据传输效率以及进行零拷贝（zero-copy）操作都非常有用。
     * 零拷贝技术允许在数据传输中避免将数据从一个缓冲区复制到另一个缓冲区，而是通过引用或映射内存来实现数据传输。
     * 
    */
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

bool HTTPConn::processWrite(HTTP_CODE ret) {
    switch (ret) {
        // 内部错误·
        case INTERNAL_ERROR: {
            addStatusLine(500, error_500_title);// HTTP1.1 500 Internal Error
            addHeaders(strlen(error_500_form));
            if (!addContent(error_500_form)) {
                return false;
            }
            break;
        }
        // 报文语法有误
        case BAD_REQUEST: {
            addStatusLine(404, error_404_title);
            addHeaders(strlen(error_404_form));
            if (!addContent(error_404_form)) {
                return false;
            }
            break;
        }
        // 资源没有访问权限
        case FORBIDDEN_REQUEST: {
            addStatusLine(403, error_403_title);
            addHeaders(strlen(error_403_form));
            if (!addContent(error_400_title)) {
                return false;
            }
            break;  
        }
        // 请求成功，返回带数据的响应
        case FILE_REQUEST: {
            addStatusLine(200, ok_200_title);
            if (m_file_stat_.st_size != 0) {
                addHeaders(m_file_stat_.st_size);
                // 要写入的第一个内容：write_buf,即响应行+响应头
                m_iv_[0].iov_base = m_write_buf_;
                m_iv_[0].iov_len = m_write_idx_;
                // 要写入的第二个内容：请求的资源文件数据，即响应体
                m_iv_[1].iov_base = m_file_address_;
                m_iv_[1].iov_len = m_file_stat_.st_size;
                m_iv_count_ = 2;
                bytes_to_send_ = m_write_idx_ + m_file_stat_.st_size;
                return true;

            }
             //如果请求的资源大小为0，则返回空白html文件
            else {
                const char* ok_string = "<html><body></body></html>";
                addHeaders(strlen(ok_string));
                if (!addContent(ok_string)) {
                    return false;
                }
            }
        }
        default: {
            return false;
        }
    }
    // 请求不成功，返回不带数据的响应
    m_iv_[0].iov_base = m_write_buf_;
    m_iv_[0].iov_len = m_write_idx_;
    m_iv_count_ = 1;
    bytes_to_send_ = m_write_idx_;
    return true;
}

// 解析请求行：请求方法、目标url、http版本号
HTTPConn::HTTP_CODE HTTPConn::parseRequestLine(char *text) {
    // 初步定位url位置
    m_url_ = strpbrk(text, " \t"); // 寻找text中第一次出现\t的位置
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
    m_url_ += strspn(m_url_, " \t"); // strspn返回空格或制表符的数量
    
    // 重复以上方法 
    m_version_ = strpbrk(m_url_, " \t");
    if (!m_version_) {
        return BAD_REQUEST;
    }
    *m_version_++ = '\0'; // 置为\0后，m_url_只包括了url部分
    m_version_ += strspn(m_version_, " \t");
    
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
HTTPConn::HTTP_CODE HTTPConn::parseHeaders(char *text) {
    // 注意，text是已经被parseLine解析过的，以\0\0结尾
    // 请求头为空行
    if (text[0] == '\0') {
        // 请求体不为空，即为POST方法,继续解析请求体
        if (m_content_length_ != 0) {
            m_check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 请求体为空，解析完成
        return GET_REQUEST;
    }
    // 请求头不为空，检查请求头的当前字段
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger_ = true;
        }

    } 
    else if (strncasecmp(text, "Content-length", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length_ = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host_ = text;
    }
    // TODO：增加其他解析
    else {
        LOG_INFO("Unknow header: %s", text);
    }
    return NO_REQUEST;
}


HTTPConn::HTTP_CODE HTTPConn::parseContent(char *text) {
    // 判断m_read_buf_中是否读入了消息体
    if (m_read_idx_ >= (m_checked_idx_ + m_content_length_)) {
        text[m_content_length_] = '\0'; // 封尾
        // 读取POST请求最后的请求数据（用户名：密码）
        m_string_ = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HTTPConn::HTTP_CODE HTTPConn::doRequest() {
    // http://example.com/images/pic.jpg 中，pic.jpg 是资源名，而 /images/ 是资源所在的路径。
    // m_url_:/images/pic.jpg 
    // 通过定位最后一个斜杠，可以将 URL 分成路径和资源名两部分。
    const char* p = strrchr(m_url_, '/');
    strcpy(m_real_file_, doc_root_);
    int len = strlen(doc_root_);
    // 欢迎访问界面：http://192.168.43.219:9006/
    // 新用户注册：http://192.168.43.219:9006/0
    // 已有帐号登录：http://192.168.43.219:9006/1
    // 登录校验：http://192.168.43.219:9006/2CGISQL.cgi
    // 注册校验：http://192.168.43.219:9006/3CGISQL.cgi
    // 看图：http://192.168.43.219:9006/5

    // 获取要访问的完整文件路径
    // cgi登录或注册检验页面
    if (cgi_ == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // flag读取为2或3
        char flag = m_url_[1];

        // 获取资源名m_url_real：CGISQL.cgi
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url_ + 2);

        // 获取文件完整路径m_real_file_：TODO：完整路径名是什么

        strncpy(m_real_file_, m_url_real, FILENAME_LEN - len -1);
        free(m_url_real);

        // 提取用户名和密码
        // 例如：user=123&password=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string_[i] != '&'; ++i) {
            name[i - 5] = m_string_[i];
        }
        name[i - 5] = '\0'; // 封尾

        int j = 0;
        for (i = i + 10; m_string_[i] != '\0'; ++i, ++j) {
            password[j] = m_string_[i];
        }
        password[j] = '\0';

        // 注册校验
        if (*(p + 1) == '3') {
            // 如果是注册，先检查是否有重名
            // 没有重名，新增入数据库
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO VALUES(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "','");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            // 在C++中编写SQL语句时，不需要在SQL语句的末尾添加分号（;）。SQL语句的分号通常在交互式数据库客户端中使用，用于表示输入的SQL语句结束，以便客户端知道何时将语句发送到数据库执行。

            // TODO：这里检查的数据来自初始化时候存入users的数据，数据有可能已经发生了更新？
            // 不重名
            if (users.find(name) == users.end()) {
                // 保护临界区
                m_lock.lock();
                int res = mysql_query(mysql_, sql_insert);
                users.insert(std::pair<std::string, std::string>(name, password));
                m_lock.unlock();

                // 插入（注册）成功，m_url_改为登录界面，跳转到登录界面
                if (!res) {
                    strcpy(m_url_, "/log.html");
                }
                // 插入（注册）失败，跳转到注册失败页面
                else {
                    strcpy(m_url_, "/registerError.html");
                }

            }
            else {
                strcpy(m_url_, "/registerError.html");
            }
        }
        // 登录校验
        else if (*(p + 1) == '2') {
            // 登录成功
            if (users.find(name) != users.end() && users[name] == password) {
                strcpy(m_url_, "welcome.html");
            }
            // 登录失败
            else {
                strcpy(m_url_, "/registerError.html");
            }
        }


    }
    // 普通注册页面
    if (*(p + 1) == '0') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file_ + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    // 普通登录页面
    else if (*(p + 1) == '1') {
        char * m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file_ + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '5') {
        char * m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file_ + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
        else if (*(p + 1) == '6') {
        char * m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file_ + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
        else if (*(p + 1) == '7') {
        char * m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file_ + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else {
        strncpy(m_real_file_ + len, m_url_, FILENAME_LEN - len - 1);
    }


    // 检查要访问文件的权限
    if (stat(m_real_file_, &m_file_stat_) < 0) {
        return NO_RESOURCE;
    }

    // st_mode：文件的类型和权限
    // 是否可读
    if (!(m_file_stat_.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    // 是否是一个目录,是的话报文有误
    if (S_ISDIR(m_file_stat_.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file_, O_RDONLY);
    // 将文件映射到内存
    m_file_address_ = (char*)mmap(0, m_file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
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

// 释放m_file_address指向的内存映射。
void HTTPConn::unmap() {
    if (m_file_address_) {
        munmap(m_file_address_, m_file_stat_.st_size);
        m_file_address_ = 0;
    }

}
// format就是包含了格式占位符(%d %s)的字符串，...就是代入占位符的各个变量
bool HTTPConn::addResponse(const char *format, ...) {
    if (m_write_idx_ >= WRITE_BUFFER_SIZE) {
        return false;
    }
    // 定义可变参数列表
    va_list arg_list;
    // 初始化可变参数列表
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf_ + m_write_idx_, WRITE_BUFFER_SIZE - 1 - m_write_idx_, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx_)) {
        // 清空可变参数列表
        va_end(arg_list);
        return false;
    }
    m_write_idx_ += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf_);
    return true;
}

bool HTTPConn::addContent(const char *content) {
    return addResponse("%s", content);
}

bool HTTPConn::addStatusLine(int status, const char *title) {

    return addResponse("%s %d %s\r\n", "HTTP1.1", status, title);
}

bool HTTPConn::addHeaders(int content_length) {
    return addContentLength(content_length) && addLinger() && addBlankLine();
}

bool HTTPConn::addContentType() {
    return addResponse("Content-Type:%s\r\n", "text/html");
}

bool HTTPConn::addContentLength(int content_length) {
    return addResponse("Content-Length:%d\r\n", content_length);
}

bool HTTPConn::addLinger() {

    return addResponse("Connection:%s\r\n", (m_linger_ == true) ? "keep-alive" : "close");

}
// 添加空行
bool HTTPConn::addBlankLine() {

    return addResponse("%s", "\r\n");
}
