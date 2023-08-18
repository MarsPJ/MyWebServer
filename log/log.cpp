
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
Log::Log()
{
    m_count_ = 0;
    m_is_async_ = false;
}

Log::~Log() {
    if (nullptr != m_fp_) {
        fclose(m_fp_);
    }
}
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    // 如果设置了max_queue_size，说明需要使用缓冲队列，因此设置为异步
    if (max_queue_size >= 1) {
        m_is_async_ = true;
        m_log_queue_ = new BlockQueue<std::string>(max_queue_size);
        pthread_t tid;
        // 创建线程，执行回调函数，异步写日志
        pthread_create(&tid, nullptr, flushLogThread, nullptr);

    }
    m_close_log_ = close_log;
    m_log_buf_size_ = log_buf_size;
    // 开辟缓冲区
    m_buf_ = new char[m_log_buf_size_];
    memset(m_buf_, '\0', m_log_buf_size_);
    m_split_lines_ = split_lines;
    time_t t = time(nullptr);
    // 将时间格式化为年、月、日、时、分等字段，以便后续生成日志文件名。
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 如果str中存在字符ch,返回出现ch的位置的指针；否则返回NULL
    const char* p = strrchr(file_name, '/');
    // 加上时间戳的完整文件名
    char log_full_name[256] = {0};
    /**
     * 如果传入的file_name没有路径，则直接在前面加上时间戳
     * 如果有路径，则需要先分割file_name，分别得到路径名和文件名
     * 然后在文件名前面加上时间戳，然后再拼接成包含路径的完整文件名
    */

    // 找不到/，说明传入的文件名不包含路径信息，只是一个名称
    // 因此可以直接在file_name前加上时间戳作为完整的文件名
    if (nullptr == p) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name_);

    }
    else {
        // 第一个参数是目标，第二个是要复制的内容
        // 截取file_name中的原来的日志文件名
        strcpy(log_name_, p + 1);
        // 截取file_name中的路径名
        strncpy(dir_name_, file_name, p - file_name + 1);
        // 在原来的文件名基础上，在前面加上时间戳，构成完整文件名
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name_, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name_);
    }
    m_today_ = my_tm.tm_mday;
    // 追加的方式打开文件
    m_fp_ = fopen(log_full_name, "a");
    if(nullptr == m_fp_) {
        return false;
    }
    return true;
}
void Log::writeLog(int level, const char* format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 准备写入一个log
    // 先判断是否要分割日志文件
    m_mutex_.lock();
    ++m_count_;

    // 如果不是同一天或者写入的日志行数超过了最大限制，则新建文件日志
    if (m_today_ != my_tm.tm_mday || m_count_ % m_split_lines_ == 0) {
        // 先将缓冲区内容写入文件（磁盘）
        fflush(m_fp_);
        // 关闭文件
        fclose(m_fp_);
        char new_log[256] = {0};
        // 时间戳
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        // 跨天
        if (m_today_ != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name_, tail, log_name_);
            // 更新m_today_
            m_today_ = my_tm.tm_mday;
            m_count_ = 0;
        }
        // 行数超过最大限制
        else {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name_, tail, log_name_, m_count_ / m_split_lines_);

        }
        // 追加方式打开新文件
        m_fp_ = fopen(new_log, "a");
    }

    m_mutex_.unlock();

    // 开始写日志
    char s[16] = {0};
    switch (level) {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[error]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    va_list valst;
    va_start(valst, format);


    // 构造日志内容
    std::string log_str;
    m_mutex_.lock();
    // 先写入时间戳信息，年月日时分秒微秒，日志类型
    int n = snprintf(m_buf_, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s",
                    my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                    my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf_ + n, m_log_buf_size_ - n -1, format, valst);
    m_buf_[n + m] = '\n';
    m_buf_[n + m + 1] = '\0';
    log_str = m_buf_;
    m_mutex_.unlock();


    /**
     * 如果是异步模式且队列未满，则将内容放入队列，
     * 如果是同步模式或队列已满，则使用锁来保护文件写入。
     * 这样可以保证多线程环境下的安全写入。
     * 
    */
    // m_log_queue_是线程安全的，因此不用上锁
    if (m_is_async_ && !m_log_queue_->full()) {
        m_log_queue_->push(log_str);
    }
    // 立即写入文件，并且只允许当前线程进行写入操作
    else {
        m_mutex_.lock();
        // 将日志字符串写入到日志文件
        fputs(log_str.c_str(), m_fp_);
        m_mutex_.unlock();
    }

    va_end(valst);

}


void Log::flush(void) {
    m_mutex_.lock();

    /**
     * fflush 函数的作用是将文件流的缓冲区中的数据立即写入文件。
     * 在通常情况下，文件流的写入是有缓冲的，即数据会先存储在缓冲区中，
     * 然后根据一定的条件（比如缓冲区满了、程序退出等）才会被写入到磁盘文件中。
    */
    // 强制刷新写入流缓冲区，这个操作在日志文件切割的过程中，
    // 确保当前的日志内容已经写入到文件中，保证数据完整性
    fflush(m_fp_);
    m_mutex_.unlock();
}
