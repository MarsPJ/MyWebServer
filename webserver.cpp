#include "webserver.h"

WebServer::WebServer() {
    // 连接对象
    users_ = new HTTPConn[MAX_FD];

    // root文件夹所在本地的路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";

    // 完整路径
    m_root_ = (char*)malloc(strlen(server_path) + strlen(root) + 1); //+1是/0
    strcpy(m_root_, server_path);
    strcat(m_root_, root);

    //定时器
    users_timer_ = new ClientData[MAX_FD];


}

WebServer::~WebServer() {
    close(m_epoll_fd_);
    close(m_listen_fd_);
    close(m_pipefd_[1]);
    close(m_pipefd_[0]);
    delete[] users_;
    delete[] users_timer_;
    delete[] m_thread_pool_;
}

void WebServer::init(int port, std::string user, std::string password, std::string database_name, int log_write, int opt_liner, int trigmode, int sql_num, int thread_num, int close_log, int actor_mode) {
    m_port_ = port;
    m_user_ = user;
    m_password_ = password;
    m_database_name_ = database_name;
    m_sql_num_ = sql_num;
    m_thread_num_ = thread_num;
    m_log_write_ = log_write;
    m_OPT_LINGER_ = opt_liner;
    m_TRIGMode_ = trigmode;
    m_close_log_ = close_log;
    m_actormodel_ = actor_mode;
}

void WebServer::threadPool() {
    m_thread_pool_ = new ThreadPool<HTTPConn>(m_actormodel_, m_connPool_, m_thread_num_);
}

void WebServer::sqlPool() {
    m_connPool_ = ConnectionPool::getInstance();
    // TODO：这里的m_log_close应该不需要？
    m_connPool_->init("localhost", m_user_, m_password_, m_database_name_, 3306, m_sql_num_, m_close_log_);

    // 初始化数据库读取表,TODO：不太理解
    users_->initMysqlResult(m_connPool_);
}

void WebServer::logWrite() {
    if (0 == m_close_log_) {
        if (1 == m_log_write_) {
            Log::getInstance()->init("./MyServerLog", m_close_log_, 2000, 800000, 800);
        }
        else {
            Log::getInstance()->init("MyServerLog", m_close_log_, 2000, 800000, 0);
        }

    }
}

void WebServer::trigMode() {
    // LT + LT
    if (0 == m_TRIGMode_) {
        m_listen_fd_ = 0;
        m_CONNTrigmode_ = 0;


    }
    // LT + ET
    else if (1 == m_TRIGMode_) {
        m_listen_fd_ = 0;
        m_CONNTrigmode_ = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode_) {
        m_listen_fd_ = 1;
        m_CONNTrigmode_ = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode_) {
        m_listen_fd_ = 1;
        m_CONNTrigmode_ = 1;

    }
}

void WebServer::eventListen() {
    // 网络编程基础步骤
    // PF_INET:IPV4
    // SOCK_STREAM:TCP
    m_listen_fd_ = socket(PF_INET, SOCK_STREAM, 0); 
    // 创建失败，触发断言，终止程序
    assert(m_listen_fd_ >= 0);

    // close时立即关闭连接
    if (0 == m_OPT_LINGER_) {
        // 0 表示不启动延迟关闭
        struct linger tmp = {0, 1};
        setsockopt(m_listen_fd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    }
    else if (1 == m_OPT_LINGER_) {
        // 延迟关闭套接字，等待1秒钟
        struct linger tmp = {1, 1};
        setsockopt(m_listen_fd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 将监听套接字绑定到服务器的地址和端口
    int ret = 0;
    //1.先初始化sockaddr_in变量
    struct sockaddr_in address;
    // 将 address 结构体变量的内存清零，以确保其中的字段没有任何随机值
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET; // 协议簇使用IPV4
    // 监听所有可用的 本地网络接口 。INADDR_ANY 是一个特殊的 IP 地址，表示绑定到所有可用的网络接口。
    address.sin_addr.s_addr = htonl(INADDR_ANY);    // "Socket INternet ADDRess"
    address.sin_port = htons(m_port_); // 监听的端口

    // 2.设置允许地址复用
    /**
     * 允许服务器在关闭监听套接字后，立即重新绑定到相同的 IP 地址和端口上，
     * 而不必等待操作系统回收处于 TIME_WAIT 状态的套接字。
     * 这可以加速服务器的重新启动和快速重启。
    */
    int flag = 1;
    setsockopt(m_listen_fd_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    
    // 3.绑定
    ret = bind(m_listen_fd_, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    
     // 4.开始监听连接请求，设置等待连接队列的最大长度为5
    ret = listen(m_listen_fd_, 5);
    assert(ret >= 0);

    // 初始化工具类
    utils_.init(TIMESLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER]; // 事件
    m_epoll_fd_ = epoll_create(5); // 内核事件表大小设置为5
    assert(m_epoll_fd_ != -1);

    utils_.addFD(m_epoll_fd_, m_listen_fd_, false, m_LISTENTrigmode_);
    HTTPConn::m_epoll_fd_ = m_epoll_fd_;


    /**
     * 创建一个全双工的 UNIX 域套接字对，其中 m_pipefd 是一个数组，包含两个文件描述符，
     * 一个用于读，一个用于写。这对套接字通常用于实现进程间通信。
     * 
    */
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd_);

    assert(ret != -1);
    utils_.setNonBlocking(m_pipefd_[1]);
    utils_.addFD(m_epoll_fd_, m_pipefd_[0], false, 0);
    utils_.addSig(SIGPIPE, SIG_IGN);
    utils_.addSig(SIGALRM, utils_.sigHandler, false);
    utils_.addSig(SIGTERM, utils_.sigHandler, false);

    alarm(TIMESLOT);

    Utils::u_pipe_fd_ = m_pipefd_;
    Utils::u_epoll_fd_ = m_epoll_fd_;
}

void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        /**
         * epoll_wait 会等待套接字事件的发生，并将事件信息存储在 events 数组中。
        */
        int number = epoll_wait(m_epoll_fd_, events_, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        // 遍历event，处理事件
        for (int i = 0; i < number; ++i) {
            int sockfd = events_[i].data.fd;

            // 如果事件是监听套接字的新连接事件
            if (sockfd == m_listen_fd_) {
                bool flag = dealClientData();
                if (false == flag) {
                    continue;
                }
            }
            // 如果事件是与关闭连接有关的事件（如客户端断开连接）
            else if (events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 服务器关闭连接，移除对应的定时器
                UtilTimer* timer = users_timer_[sockfd].timer;
                dealTimer(timer, sockfd);
            }
            // 如果事件是处理信号的事件，例如处理定时器信号和停止服务器的信号。
            // 首先检查事件是否与 m_pipefd[0] 相关，并且事件类型为 EPOLLIN，即判断是否有数据可读取。
            else if ((sockfd == m_pipefd_[0]) && (events_[i].events & EPOLLIN)) {
                bool flag = dealWithSignal(timeout, stop_server);
                if (false == flag) {
                    LOG_ERROR("%s", "dealclientdata failure");
                }
            }
            // 如果事件是客户连接上的读事件（EPOLLIN）
            else if (events_[i].events & EPOLLIN) {
                dealWithRead(sockfd);
            }
            // 如果事件是客户连接上的写事件（EPOLLOUT）
            else if (events_[i].events & EPOLLOUT) {
                dealWithWrite(sockfd);
            }
        }
        if (timeout) {
            utils_.timerHandler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }

    }
}

void WebServer::timer(int connfd, sockaddr_in client_address) {
    users_[connfd].init(connfd, client_address, m_root_, m_CONNTrigmode_, m_close_log_, m_user_, m_password_, m_database_name_);

    //初始化ClientData数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer_[connfd].address = client_address;
    users_timer_[connfd].sockfd = connfd;
    UtilTimer* timer = new UtilTimer;
    timer->user_data_ = &users_timer_[connfd];
    timer->cbFunc = cbFunc;
    time_t cur = time(nullptr);
    timer->expire_ = cur + 3 * TIMESLOT;
    users_timer_[connfd].timer = timer;
    utils_.m_timer_lst_.addTimer(timer);


}
//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjustTimer(UtilTimer *timer) {
    time_t cur = time(nullptr);
    timer->expire_ = cur + 3 * TIMESLOT;
    utils_.m_timer_lst_.adjustTimer(timer);
    LOG_INFO("%S", "adjust timer once");
}

void WebServer::dealTimer(UtilTimer *timer, int sock_fd) {
    timer->cbFunc(&users_timer_[sock_fd]);
    if (timer) {
        utils_.m_timer_lst_.delTimer(timer);
    }
    LOG_INFO("close fd %d", users_timer_[sock_fd].sockfd);
}

bool WebServer::dealClientData() {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode_) {
        int connfd = accept(m_listen_fd_, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0) {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (HTTPConn::m_user_count_ >= MAX_FD) {
            // 发送错误响应
            utils_.showError(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        // 利用已有数据初始化定时器
        timer(connfd, client_address);
    }
    else {
        while (true) {
            int connfd = accept(m_listen_fd_, (struct sockaddr*)&client_address, &client_addrlength);
            if (connfd < 0) {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (HTTPConn::m_user_count_ >= MAX_FD) {
                utils_.showError(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealWithSignal(bool& timeout, bool& stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd_[0], signals, sizeof(signals), 0);
    // 接受数据发生错误
    if (ret == -1) {
        return false;
    }
    // 对方关闭连接
    else if (ret == 0) {
        return false;
    }
    else {
        for (int i = 0; i < ret; ++i) {
            switch (signals[i]) {
                case SIGALRM: {
                    timeout = true;
                    break;
                }
                case SIGTERM: {
                    stop_server = true;
                    break;
                }

            }
        }
    }
    return true;
}

void WebServer::dealWithRead(int sockfd) {
    UtilTimer* timer = users_timer_[sockfd].timer;
    //reactor
    if (1 == m_actormodel_) {
        if (timer) {
            adjustTimer(timer);
            
        }
        //若监测到读事件，将该事件放入请求队列
        m_thread_pool_->append(users_ + sockfd, 0);

        while (true) {
            if (1 == users_[sockfd].improv_) {
                if (1 == users_[sockfd].timer_flag_) {
                    dealTimer(timer, sockfd);
                    users_[sockfd].timer_flag_ = 0;
                }
                users_[sockfd].improv_ = 0;
                break;
            }
        }
        
    }
    //proactor
    else {
        if (users_[sockfd].readOnce()) {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users_[sockfd].getAddress()->sin_addr));
            //若监测到读事件，将该事件放入请求队列
            m_thread_pool_->appendP(users_ + sockfd);
            if (timer) {
                adjustTimer(timer);
            }
        }
        else {
            dealTimer(timer, sockfd);
        }
    }
}

void WebServer::dealWithWrite(int sockfd) {
    UtilTimer* timer = users_timer_[sockfd].timer;

    // reactor
    if (1 == m_actormodel_) {
        if (timer) {
            adjustTimer(timer);
        }
        m_thread_pool_->append(users_ + sockfd, 1);
        while (true) {
            if (1 == users_[sockfd].improv_) {
                if (1 == users_[sockfd].timer_flag_) {
                    dealTimer(timer, sockfd);
                    users_[sockfd].timer_flag_ = 0;
                }
                users_[sockfd].improv_ = 0;
                break;
            }

        }
    }
    else {
        // proactor
        if (users_[sockfd].write()) {
            LOG_INFO("send data to client(%s)", inet_ntoa(users_[sockfd].getAddress()->sin_addr));
            if (timer) {
                adjustTimer(timer);
            }
        }
        else {
            dealTimer(timer, sockfd);
        }
    }


}
