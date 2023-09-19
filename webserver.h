#ifndef WEBSERVER_H
#define WEBSERVER_H

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#include"http/http_conn.h"
#include"threadpool/threadpool.h"
#include"timer/lst_timer.h"
#include"log/log.h"
#include"timer/lst_timer.h"

const int MAX_FD = 65536;     // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;  // 最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port, std::string user, std::string password, std::string database_name, int log_write, int opt_liner, int trigmode, int sql_num, int thread_num, int close_log, int actor_mode);
    void threadPool();
    void sqlPool();
    void logWrite();
    void trigMode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjustTimer(UtilTimer* timer);
    void dealTimer(UtilTimer* timer, int sock_fd);
    bool dealClientData();
    bool dealWithSignal(bool& timeout, bool& stop_server);
    void dealWithRead(int sockfd);
    void dealWithWrite(int sockfd);
    


public:
    // 基本配置
    int m_port_;
    char* m_root_;
    int m_log_write_;
    int m_close_log_;
    int m_actormodel_;

    int m_pipefd_[2];
    int m_epoll_fd_;
    HTTPConn* users_;

    // 数据库配置
    ConnectionPool* m_connPool_;
    std::string m_user_;
    std::string m_password_;
    std::string m_database_name_;
    int m_sql_num_;

    // 线程池配置
    ThreadPool<HTTPConn>* m_thread_pool_;
    int m_thread_num_;

    // epoll_event 配置
    epoll_event events_[MAX_EVENT_NUMBER];


    int m_listen_fd_;
    int m_OPT_LINGER_;
    int m_TRIGMode_;
    int m_LISTENTrigmode_;
    int m_CONNTrigmode_;

    // 定时器配置
    ClientData* users_timer_;
    Utils utils_;


    
};




#endif