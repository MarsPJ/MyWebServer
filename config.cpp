#include "config.h"

Config::Config() {
    // 默认9006
    PORT_ = 9006;
    // 日志写入方式，默认同步
    LOGWrite_ = 0;
    // 触发组合模式，默认listenfd LT + connfd LT
    TRIGMode_ = 0;

    // listenfd触发模式，默认LT
    LISTENTrigmode_ = 0;

    //connfd触发模式，默认LT
    CONNTrigmode_ = 0;

    //优雅关闭链接，默认不使用
    OPT_LINGER_ = 0;

    //数据库连接池数量,默认8
    sql_num_ = 8;

    //线程池内的线程数量,默认8
    thread_num_ = 8;

    //关闭日志,默认不关闭
    close_log_ = 0;

    //并发模型,默认是proactor
    actor_model_ = 0;

}

void Config::parseArg(int argc, char *argv[]) {
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";
    /**
     * 如果有可接受的选项字符被找到，函数返回该选项字符。
     * 如果所有命令行参数都已被处理完毕，函数返回 -1。
    */
    while ((opt = getopt(argc, argv, str)) != -1) {
        /**
         * 当 getopt 函数解析到一个带有参数值的选项时，
         * 它将参数值存储在 optarg 中，以便程序在后续代码中访问和使用。
         * 如果选项没有参数值，optarg 的值将为 NULL
        */
        switch (opt) {
            case 'p': {
                PORT_ = atoi(optarg);
                break;
            }
            case 'l': {
                LOGWrite_ = atoi(optarg);
                break;
            }
            case 'm': {
                TRIGMode_ = atoi(optarg);
                break;
            }
            case 'o': {
                OPT_LINGER_ = atoi(optarg);
                break;
            }
            case 's': {
                sql_num_ = atoi(optarg);
                break;
            }
            case 't': {
                thread_num_ = atoi(optarg);
                break;
            }
            case 'c': {
                close_log_ = atoi(optarg);
                break;
            }
            case 'a': {
                actor_model_ = atoi(optarg);
                break;
            }
            default: {
                break;
            }
        }
    }
}
