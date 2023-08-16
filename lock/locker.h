#ifndef LOCKER_H
#define LOCKER_H


#include <exception>
#include <pthread.h>
#include <semaphore.h>
// 封装信号量
class Sem {

public:
    Sem(int num = 0) {
        // sem_init 函数在成功时返回0，在失败时返回-1
        // sem_wait和sem_post一样
        if (sem_init(&m_sem_, 0, num) != 0) { 
            throw std::exception();
        }
    }
    ~Sem() {
        sem_destroy(&m_sem_);
    }
    bool wait() {
        return sem_wait(&m_sem_) == 0;
    }
    bool post() {
        return sem_post(&m_sem_) == 0;
    }
private:
    sem_t m_sem_;
};
// 封装互斥锁
class locker
{

public:
    locker() {
        if (pthread_mutex_init(&m_mutex_, NULL) != 0){
            throw std::exception();
        }
    }
    ~locker(){
        pthread_mutex_destroy(&m_mutex_);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mutex_) == 0;
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex_) == 0;
    }
    pthread_mutex_t* get(){
        return &m_mutex_;
    }
private:
    pthread_mutex_t m_mutex_;
};
// 封装条件变量
class Cond{
public:
    Cond() {
        if (pthread_cond_init(&m_cond_, NULL) != 0) {
            throw std::exception();
        }
    }
    ~Cond() {
        pthread_cond_destroy(&m_cond_);
    }
    // 调用前m_mutex必须处于lock状态
    bool wait(pthread_mutex_t* m_mutex) {
        int ret = 0;
        // pthread_cond_wait 的返回值 int 正常情况下，它会返回零（0）
        // 主要用于指示等待的结果，通常情况下只需要关注是否返回了0，来判断等待是否成功完成。
        /**
         * 在调用 pthread_cond_wait 时，以下几个步骤会发生：
        1.线程首先需要获得与条件变量相关联的互斥锁。如果线程未持有该锁，则应该在调用 pthread_cond_wait 之前先获取锁。这个互斥锁用于确保在等待条件变量时的互斥操作。
        2.一旦获得了互斥锁，线程会释放这个锁，并进入等待状态，即阻塞在 pthread_cond_wait 这一行代码上。
        3.当其他线程调用 pthread_cond_signal 或 pthread_cond_broadcast 来通知条件变量时，等待在条件变量上的线程会被唤醒。
        4.一旦被唤醒，线程会重新尝试获取与条件变量关联的互斥锁，这表示线程再次持有锁。
        5.线程获得锁后，它可以继续执行，并且可以进行进一步的操作，例如检查特定条件是否满足。
         * 
        */
        ret = pthread_cond_wait(&m_cond_, m_mutex);
        return ret == 0;
    }
    // 调用前m_mutex必须处于lock状态
    bool timeWait(pthread_mutex_t* m_mutex, struct timespec t) {
        int ret = 0;
        // 如果条件变量在指定的超时时间内被通知唤醒，且互斥锁再次成功获取（之前释放了一次锁再等待的），函数将返回0
        ret = pthread_cond_timedwait(&m_cond_, m_mutex, &t);
        return ret == 0;
    }
    bool signal() {
        return pthread_cond_signal(&m_cond_) == 0;
    }
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond_) == 0;
    }
private:
    pthread_cond_t m_cond_;
};

#endif