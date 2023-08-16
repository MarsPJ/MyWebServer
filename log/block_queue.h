/**
 * 循环数组实现的阻塞队列
 * 线程安全，对临界区先加锁，后解锁
 * 
*/


#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H
#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"

template <typename T>
class BlockQueue{
public:
    BlockQueue(int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);
        }
        m_max_size_ = max_size;
        m_array_ = new T[max_size];
        m_size_ = 0;
        m_front_ = -1;
        m_back_ = -1;
    }

    void clear() {
        m_mutex_.lock();
        // 逻辑上清空队列
        m_size_ = 0;
        m_front_ = -1;
        m_back_ = -1;
        m_mutex_.unlock();
    }

    // 物理上清空队列
    ~BlockQueue() {
        m_mutex_.lock();
        if (m_array_ != nullptr) {
            delete [] m_array_;
        }
        m_mutex_.unlock();
    }

    bool full() {
        m_mutex_.lock();
        if (m_size_ >= m_max_size_) {
            m_mutex_.unlock();
            return true;
        }
        m_mutex_.unlock();
        return false;
    }

    bool empty() {
        m_mutex_.lock();
        if(0 == m_size_) {
            m_mutex_.unlock();
            return true;
        }
        m_mutex_.unlock();
        return false;
    }

    bool front(T& value) {
        m_mutex_.lock();
        if (0 == m_size_) {
            m_mutex_.unlock();
            return false;
        }
        value = m_array_[m_front_];
        m_mutex_.unlock();
        return true;
    }
    bool back(T &value) {
        m_mutex_.lock();
        if (0 == m_size_) {
            m_mutex_.unlock();
            return false;
        }
        value = m_array_[m_back_];
        m_mutex_.unlock();
        return true;
    }

    int size() {
        int ret = 0;
        m_mutex_.lock();
        ret = m_size_;
        m_mutex_.unlock();
        return ret;
    }
    int max_size() {
        int ret = 0;
        m_mutex_.lock();
        ret = m_max_size_;
        m_mutex_.unlock();
        return ret;
    }


    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T& item) {
        m_mutex_.lock();
        if (m_size_ >= m_max_size_) {
            m_cond_.broadcast();
            m_mutex_.unlock();
            return false;
        }


        m_back_ = (++m_back_) % m_max_size_;
        m_array_[m_back_] = item;
        ++m_size_;
        m_cond_.broadcast();
        m_mutex_.unlock();
        return true;
    }
    bool pop(T& item) {
        m_mutex_.lock();
        // 防止虚假唤醒，所以使用while
        // 当被虚假唤醒时，由于m_size仍然<=0，此时仍无法逃出while循环
        // 无法继续执行后面的代码
        // 只有当m_size>0时，才能跳出while循环，继续执行
        while (m_size_ <= 0) {
            if (!m_cond_.wait(m_mutex_.get())) {
                m_mutex_.unlock();
                return false;
            }
        }

        m_front_ = (m_front_ + 1) % m_max_size_;
        item = m_array_[m_front_];
        --m_size_;
        m_mutex_.unlock();
        return true;
    }
    
    // 增加超时处理的pop
    bool pop(T& item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, nullptr);
        m_mutex_.lock();

        // 将if改为while，更稳固预防虚假唤醒
        // by pan zijian 
        while (m_size_ <= 0) {
            // 计算超时的时候距离1970年的时间的秒数
            t.tv_sec = now.tv_sec + ms_timeout / 1000; 
            // 计算超时的时候距离1970年的时间的纳秒数部分
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            // 获取失败（超时）
            if (!m_cond_.timeWait(m_mutex_.get(), t)) {
                m_mutex_.unlock();
                return false;
            }

        }

        m_front_ = (m_front_ + 1) % m_max_size_;
        item = m_array_[m_front_];
        --m_size_;
        m_mutex_.unlock();
        return true;

    }

private:
    locker m_mutex_;
    Cond m_cond_;

    T* m_array_;
    int m_size_;
    int m_max_size_;
    int m_front_;
    int m_back_;
};




#endif