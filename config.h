#ifndef CONFIG_H
#define CONFIG_H

#include"webserver.h"
class Config {
public:
    Config();
    ~Config() {};
    void parseArg(int argc, char* argv[]);

    // 端口号
    int PORT_;

    // 日志写入方式
    int LOGWrite_;

    // 触发组合模式
    int TRIGMode_;

    // listenfd触发模式
    int LISTENTrigmode_;

    // connfd触发模式
    int CONNTrigmode_;

    // 优雅关闭连接
    int OPT_LINGER_;

    // 数据库连接池数量
    int sql_num_;

    // 线程池内的线程数量
    int thread_num_;

    // 是否关闭日志
    int close_log_;

    // 并发模型选择
    int actor_model_;


};

#endif