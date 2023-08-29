#include "lst_timer.h"


SortTimerLST::~SortTimerLST() {

}

void SortTimerLST::addTimer(UtilTimer *timer) {
    if (!timer) {
        return;
    }
    if (!head_) {
        head_ = tail_ = timer;
        return;
    }

    if (timer->expire_ < head_->expire_) {
        timer->next_ = head_;
        head_->prev_ = timer;
        head_ = timer;
        return;
    }
    addTimer(timer, head_);
}

void SortTimerLST::adjustTimer(UtilTimer *timer) {
    // 传入已经改变了过期时间的timer
    if (!timer) {
        return;
    }
    UtilTimer* tmp = timer->next_;
    // 过期时间只会增加，因此timer后面没有元素或者后面元素过期时间比timer大，则不需要调整
    if (!tmp || (timer->expire_ < tmp->expire_)) {
        return;
    }
    // timer是头节点，先去掉，再重新插入
    if (timer == head_) {
        // 去掉
        head_ = timer->next_;
        head_->prev_ = nullptr;

        // 重新插入
        timer->next_ = nullptr;
        addTimer(timer, head_);
    }
    // 先去掉，再重新插入
    else {
        timer->prev_->next_ = timer->next_;
        timer->next_->prev_ = timer->prev_;
        addTimer(timer, head_);
    }
}

void SortTimerLST::delTimer(UtilTimer *timer) {
    if (!timer) {
        return;
    }
    // 要删除节点是链表唯一节点
    if (timer == head_ && timer == tail_) {
        delete timer;
        head_ = nullptr;
        tail_ = nullptr;
        return;
    }
    // 要删除节点是头节点
    if (timer == head_) {
        head_ = head_->next_;
        head_->prev_ = nullptr;
        delete timer;
        return;
    }
    // 要删除节点是尾部节点
    if (timer == tail_) {
        tail_ = tail_->prev_;
        tail_->next_ = nullptr;
        delete timer;
        return;
    }
    // 要删除节点是中间节点
    timer->prev_->next_ = timer->next_;
    timer->next_->prev_ = timer->prev_;
    delete timer;
    // 定时器不存在该链表中，结束
}

void SortTimerLST::tick() {
    if (!head_) {
        return;
    }

    time_t cur = time(nullptr);
    UtilTimer* tmp = head_;
    // TODO：可以改为if+dowhile，减少重复判断cur < tmp->expire_
    while (!tmp) {
        // 链表是升序链表，只要第一个没有超时，后面都不超时
        // 第一个超时，后面都超时
        // 不超时
        if (cur < tmp->expire_) {
            break;
        }
        // 超时，调用回调函数
        tmp->cbFunc(tmp->user_data_);
        // 头节点一个一个向后指
        head_ = tmp->next_;
        // 新的头节点不为空，则将其prev置为nullptr
        if (head_) {
            head_->prev_ = nullptr;
        }
        // 无论怎样都要删除超时节点
        delete tmp;
        // 更新tmp
        tmp = head_;
        

    }
}

void SortTimerLST::addTimer(UtilTimer *timer, UtilTimer *lst_head) {
    UtilTimer* prev = lst_head;
    UtilTimer* tmp = prev->next_;
    while (tmp) {
        // 找到过期时间比timer大的定时器，并插入，维护双向链表
        if (timer->expire_ < tmp->expire_) {
            
            prev->next_ = timer;
            timer->prev_ = prev;
            timer->next_ = tmp;
            tmp->prev_ = timer;
            break;

        }
        prev = tmp;
        tmp = tmp->next_;
    }
    // 插在链表尾部
    if (!tmp) {
        prev->next_ = timer;
        timer->prev_ = prev;
        timer->next_ = nullptr;
        tail_ = timer;
    }
}

void cbFunc(ClientData *user_data)
{
}

void Utils::init(int time_slot) {
    m_TIME_SLOT_ = time_slot;
}

int Utils::setNonBlocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addFD(int epoll_fd, int fd, bool one_shot, int TRIGMode) {
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

void Utils::sigHandler(int sig) {
    // 保证函数的可重入性，保留原来的error
    int save_errno = errno;
    int msg = sig;
    // u_pipe_fd_[1]表示写端，发送长度为1字节的数据
    send(u_pipe_fd_[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addSig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    // 使用 memset 函数将 sa 清零，以确保结构体的初始状态
    memset(&sa, '\0', sizeof(sa));
    // 设置为信号处理函数。这个函数将在接收到指定信号时调用。
    sa.sa_handler = handler;
    // 这个标志表示在信号处理函数返回后，被中断的系统调用可以自动恢复，而不是中断调用
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    // 将所有信号添加到信号屏蔽字（sa_mask）中。这样在信号处理函数执行期间，阻塞其他信号的传递。
    sigfillset(&sa.sa_mask);
    // sigaction 函数将设置好的信号处理结构应用于指定的信号。如果设置成功，sigaction 返回 0，否则返回 -1。
    assert(sigaction(sig, &sa, nullptr) != -1);
    
}

void Utils::timerHandler() {
    // 周期性地处理定时任务，并在每次处理后重新设置定时器，以便在指定的时间间隔后再次触发相应的定时任务。
    // 删掉过时的定时器
    m_timer_lst_.tick();
    // 增加新的定时器
    alarm(m_TIME_SLOT_);

}

void Utils::showError(int conn_fd, const char *info) {
    // 向客户端发送错误信息
    send(conn_fd, info, strlen(info), 0);
    // 关闭连接，结束与客户端的通信。
    close(conn_fd);
}

int* Utils::u_pipe_fd_ = 0;
int Utils::u_epollfd_ = 0;





