#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

class Log {
public:
    // 懒汉模式，顾名思义就是懒，没有对象需要调用它的时候不去实例化，有人来向它要对象的时候再实例化对象
    // 在不加任何额外措施的情况下，懒汉模式是有潜在线程安全问题的，因为多个线程可能在同一时间内尝试创建实例，导致实例的多次创建。
    // 在 C++11 及以后的标准中，当你在一个函数内部声明一个静态局部变量时，
    // 这个变量只会在第一次调用该函数时被初始化，之后的调用不会重新创建这个变量。
    // 即C++11以后,使用局部变量懒汉不用加锁
    static Log* getInstance() {
        static Log instance;
        return &instance;
    }
    // 封装成 POSIX 线程库支持的线程函数
    static void* flushLogThread(void* args) {
        Log::getInstance()->asyncWriteLog();
    }
    // 可选择的参数有日志文件、是否开启日志（0表示开启）、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char* file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    void writeLog(int level, const char* format, ...);
    void flush(void);

private:
    Log();
    virtual ~Log();
    void *asyncWriteLog() {
        std::string single_log;
        // 从阻塞队列取出要写入到文件的日志内容
        while (m_log_queue_->pop(single_log)) {
            m_mutex_.lock();
            // 将日志内容写入到日志文件中。如果写入成功，返回的值将是非负的；如果出现错误，返回 EOF。
            fputs(single_log.c_str(), m_fp_);
            m_mutex_.unlock();
        }
    }

private:
    char dir_name_[128]; // 路径名，专门记录是为了切割日志时可以直接得到路径
    char log_name_[128]; // log文件名，专门记录是为了切割日志时可以直接得到文件名（不含时间戳）
    int m_split_lines_;  // 日志最大行数
    int m_log_buf_size_; // 日志缓冲区大小
    long long m_count_;  // 日志行数记录，就算打到最大行数也不会清零，利用整除标记日志文件名和利用取余判断单个日志文件是否到达最大行数
    int m_today_;        // 因为按天分类，记录当前时间是哪一天
    FILE* m_fp_;         // 打开log的文件指针
    char* m_buf_;        
    BlockQueue<std::string>* m_log_queue_;  // 阻塞队列
    bool m_is_async_;                       // 是否同步标志位
    locker m_mutex_;
    int m_close_log_;                       // 关闭日志

};


#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::getInstance()->writeLog(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif