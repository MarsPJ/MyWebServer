#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include"../lock/locker.h"
#include"../CGImysql/sql_connection_pool.h"

template <typename T>
class ThreadPool {
public:

    ThreadPool(int actor_model, ConnectionPool* conn_pool, int thread_number = 8, int max_request = 10000);
    ~ThreadPool();
    bool append(T* request, int state);
    bool appendP(T* request);
private:
    // 工作线程运行的函数，不断从请求队列中取出任务并执行
    // 注意要定义成static，如果是普通成员函数，会将this作为参数传入，导致和void* 类型不匹配报错
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number_;           // 线程池中的线程数
    int m_max_requests_;             // 请求队列中允许的最大请求数
    pthread_t* m_threads_;           // 描述线程池的数组，大小为m_thread_number_
    std::list<T*> m_work_queue_;    // 请求队列
    locker m_queue_locker_;         // 保护请求队列的互斥锁
    Sem m_queue_state_;              // 是否有任务需要处理
    ConnectionPool* m_conn_pool_;   // 数据库
    int m_actor_model_;             // 模型切换
};




#endif

template <typename T>
ThreadPool<T>::ThreadPool(int actor_model, ConnectionPool *conn_pool, int thread_number, int max_request) : 
                        m_actor_model_(actor_model), m_conn_pool_(nullptr), m_thread_number_(thread_number), m_max_requests_(max_request) {
    if (thread_number <= 0 || max_request <= 0) {
        throw std::exception();
    }
    m_threads_ = new pthread_t[m_thread_number_];
    if (!m_threads_) {
        throw std::exception();
    }

    for (int i = 0; i < thread_number; ++i) {
        
        // 如果返回值为 0：表示线程创建成功，新线程已经开始执行。
        // 如果返回值不为 0：表示线程创建失败，失败的原因可以是多种，比如系统资源不足、参数错误等。
        // 传递this作为线程函数的参数
        if (pthread_create(m_threads_ + i, nullptr, worker, this) != 0) {
            delete[] m_threads_;
            throw::std::exception();
        }

        // 用于将一个线程设置为分离状态，这意味着该线程的资源（例如内存、文件描述符等）在线程终止时会自动被回收，
        // 无需其他线程来调用 pthread_join 来等待线程的终止。
        if (pthread_detach(m_threads_[i]) != 0) {
            delete[] m_threads_;
            throw std::exception();
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] m_threads_;
}

template <typename T>
bool ThreadPool<T>::append(T *request, int state) {
    m_queue_locker_.lock();
    if (m_work_queue_.size() >= m_max_requests_) {
        m_queue_locker_.unlock();
        return false;
    }
    // T 类型(request)是HttpConn类型
    request->m_state_ = state;
    m_work_queue_.push_back(request);
    m_queue_locker_.unlock();
    // V操作，告诉工作线程有任务
    m_queue_state_.post();
    return true;
}

template <typename T>
bool ThreadPool<T>::appendP(T *request) {
    m_queue_locker_.lock();
    if (m_work_queue_.size() >= m_max_requests_) {
        m_queue_locker_.unlock();
        return false;
    }
    m_work_queue_.push_back(request);
    m_queue_locker_.unlock();
    m_queue_state_.post();
    return true;
}

template <typename T>
void *ThreadPool<T>::worker(void *arg) {
    ThreadPool* thread_pool = (ThreadPool*) arg;
    thread_pool->run();
    return thread_pool;
}
// TODO:http完成后补充
template <typename T>
void ThreadPool<T>::run() {
    while (true) {
        // P操作
        m_queue_state_.wait();
        // 保护任务队列
        m_queue_locker_.lock();
        if (m_work_queue_.empty()) {
            m_queue_locker_.unlock();
            // 继续等待
            continue;
        }
        T* request = m_work_queue_.front();
        m_work_queue_.pop_front();
        m_queue_locker_.unlock();

        // 空连接
        if (!request) {
            continue;
        }

        if (1 == m_actor_model_) {
            // 读取请求数据的阶段
            if (0 == request->m_state_) {
                if (request->readOnce()) {
                    request->improv_ = 1;
                    ConnectionRAII mysqlconn(&request->mysql_, m_conn_pool_);
                    request->process();
                }
                else {
                    request->improv_ = 1;
                    request->timer_flag_ = 1;
                }
            }
            // 响应阶段
            else {
                if (request->write()) {
                    request->improv_ = 1;

                }
                else {
                    request->improv_ = 1;
                    request->timer_flag_ = 1;
                }
            }
        }
        else {
            ConnectionRAII mysqlconn(&request->mysql_, m_conn_pool_);
            request->process();
        }


    }
}
