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


private:
    Log();
    virtual ~Log();

private:
    char dir_name_[128]; // 路径名
    char log_name_[128]; // log文件名
    int m_split_lines_;  // 日志最大行数
    int m_log_buf_size_; // 日志缓冲区大小
    long long m_count_;  // 日志行数记录
    int m_today_;        // 因为按天分类，记录当前时间是哪一天
    FILE* m_fp_;         // 打开log的文件指针
    char* m_buf_;        
    BlockQueue<std::string>* m_log_queue_;  // 阻塞队列
    bool m_is_async_;                       // 是否同步标志位
    locker m_mutex_;
    int m_close_log_;                       // 关闭日志

};


#endif