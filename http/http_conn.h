#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

// TODO：头文件可能包含不全，类名和宏def不一致

#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<sys/stat.h>
#include<sys/uio.h>
#include<map>
#include<mysql/mysql.h>
#include<fstream>
#include<fcntl.h>
#include<sys/epoll.h>
#include<sys/mman.h>


#include"../CGImysql/sql_connection_pool.h"


class HTTPConn {
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0, // 解析请求行
        CHECK_STATE_HEADER, // 解析请求头
        CHECK_STATE_CONTENT // 解析消息体，仅用于解析POST请求
    };

    enum HTTP_CODE {
        NO_REQUEST,// 请求不完整，需要继续解析报文数据
        GET_REQUEST,// 获得了完整的HTTP请求
        BAD_REQUEST,// HTTP请求报文有语法错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,// 服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION

    };

    enum LINE_STATUS {
        // 完整读取一行
        LINE_OK = 0,
        // 读取的数据存在语法错误
        LINE_BAD,
        // 读取的行不完整
        LINE_OPEN
    };
public:
    HTTPConn() = default;
    ~HTTPConn() = default;
public:
    // 初始化连接外部接口
    void init(int sock_fd, const sockaddr_in &addr, char * root, int TRIGMode, int close_log, std::string user, std::string passwd, std::string sql_name);
    void closeConn(bool real_close = true);
    void process();
    bool readOnce();
    bool write();
    sockaddr_in* getAddress();
    void initMysqlResult(ConnectionPool* conn_pool);
    // TODO：是否可以改变以下两个变量位置
    int timer_flag_;
    int improv_;

private:
    // 协助外部初始化接口，主要是初始化一些不需要传参赋值的变量
    void init();
    HTTP_CODE processRead();
    bool processWrite(HTTP_CODE ret);
    // 解析 HTTP 请求的请求行，即请求的第一行，包含请求方法、URL 和 HTTP 版本信息。
    HTTP_CODE parseRequestLine(char* text);
    HTTP_CODE parseHeaders(char* text);
    HTTP_CODE parseContent(char* text);
    HTTP_CODE doRequest();
    char* getLine();
    // 根据已读取的数据进行解析，判断是否读取到一行完整的数据，以及该行数据的状态（正常、异常等）
    LINE_STATUS parseLine();
    void unmap();
    bool addResponse(const char* format, ...);
    bool addContent(const char* content);
    bool addStatusLine(int status, const char* title);
    bool addHeaders(int content_length);
    bool addContentType();
    bool addContentLength(int content_length);
    bool addLinger();
    bool addBlankLine();
public:
    static int m_epoll_fd_; // 当前连接对应的epoll实例
    static int m_user_count_;
    MYSQL* mysql_; // 当前与数据库的连接
    int m_state; // 读为0,写为1

private:
    // 当前连接的socket描述符
    int m_sock_fd_;
    // 当前socket的地址
    sockaddr_in m_address_;

    char m_read_buf_[READ_BUFFER_SIZE];
    long m_read_idx_;// 指向已经读取到m_read_buf_中的数据的下一个位置

    long m_checked_idx_;// 指向指向已经被解析（加了\0\0）的那一行数据的下一行数据的开头
    int m_start_line_; // 指向已经被解析（加了\0\0）的那一行数据的开头

    char m_write__buf_[WRITE_BUFFER_SIZE];
    int m_write_idx_;

    CHECK_STATE m_check_state_; // 当前主状态机状态
    METHOD m_method_; // 请求方法

    char m_real_file_[FILENAME_LEN]; // 要访问的文件的完整路径名
    char* m_url_; // 请求url资源
    char* m_version_; // http版本
    char* m_host_; // 域名
    long m_content_length_; // 请求体长度


    // m_linger 的作用是确定是否在关闭连接时启用 Linger 选项。
    // 如果设置为 true，则表示启用 Linger，连接在关闭时会等待一段时间，直到数据传输完毕或超时。
    // 如果设置为 false，则表示在关闭连接时立即关闭，不管是否还有未传输完的数据。
    bool m_linger_;
    char* m_file_address_; // mmap映射到内存的地址
    struct stat m_file_stat_; // 要获取的文件的属性
    struct iovec m_iv_[2];
    int cgi_; // 是否启用的POST,1表示启用，0表示不启用
    char* m_string_; // 存储请求头数据
    int bytes_to_send_;
    int bytes_have_send_;
    char* doc_root_; // 访问资源的根目录

    // 存储数据库中的用户名：密码
    std::map<std::string, std::string> m_users_;
    // 0表示LT模式（没处理完，内核会持续通知），1表示ET模式（只触发一次，内核只通知一次，不管有没有处理完）
    int m_TRIGMode_;
    int m_close_log_;
    // 当前用户，密码，数据库名称
    char sql_user_[100];
    char sql_passwd_[100];
    char sql_name_[100];
  
};


#endif