#ifndef LST_TIMER
#define LST_TIMER

// TODO:头文件可能包含不全
#include<unistd.h>
#include<netinet/in.h>
#include<time.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<errno.h>
#include<signal.h>
#include<string.h>
#include<assert.h>
class UtilTimer;

struct ClientData {
    sockaddr_in address;
    int sockfd;
    UtilTimer* timer;
};
// 双向链表节点数据结构
class UtilTimer {
public:
    UtilTimer() : prev_(nullptr), next_(nullptr) {};
public:
    // 定时器超时时间
    time_t expire_;
    // 函数指针 callback function，回调函数,超时的时候会执行
    void (* cbFunc)(ClientData*);
    ClientData* user_data_;
    UtilTimer* prev_;
    UtilTimer* next_;

};


// 超时时间升序双向链表
class SortTimerLST {
public:
    SortTimerLST() : head_(nullptr), tail_(nullptr) {};
    ~SortTimerLST();
    // 外部接口，增加定时器
    void addTimer(UtilTimer* timer);
    // 用于调整定时器的位置，以保持定时器链表的有序性。当一个定时器的过期时间发生变化时，需要调用这个函数来确保链表中的定时器仍然保持有序。
    void adjustTimer(UtilTimer* timer);
    // 删除定时器
    void delTimer(UtilTimer* timer);
    // 处理定时器链表中已经过期的定时器，调用它们的回调函数，并从链表中删除这些已触发的定时器。
    void tick();

private:
    // 内部函数，处理定时器延迟比头节点大的情况
    // TODO：为了重载而多增添的参数lst_head，其实可以去掉
    void addTimer(UtilTimer* timer, UtilTimer* lst_head);
    UtilTimer* head_;
    UtilTimer* tail_;
};


class Utils {
public:
    Utils() = default;
    ~Utils() = default;
    void init(int time_slot);
    // 对文件描述符设置非阻塞,返回旧的标志位
    int setNonBlocking(int fd);
     //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addFD(int epoll_fd, int fd, bool one_shot, int TRIGMode);
    // 信号处理函数
    static void sigHandler(int sig);

    // 设置信号函数
    void addSig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timerHandler();
    void showError(int conn_fd, const char* info);

    
public:
    // 管道
    static int* u_pipe_fd_;
    // 定时器链表
    SortTimerLST m_timer_lst_;
    // epoll文件描述符
    static int u_epoll_fd_;
    // 定时器的触发间隔时间
    int m_TIME_SLOT_;
};

// 从 epoll 事件表中删除一个文件描述符，关闭与客户端的连接，并更新连接数量。
void cbFunc(ClientData* user_data);
#endif