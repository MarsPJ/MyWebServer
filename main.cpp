
// TODO：函数声明和实现完全分离，类包含.h和.cpp文件？
#include "config.h"
int main(int argc, char* argv[]) {
    std::string user = "root";
    std::string passwd = "131317lP";
    std::string databasename = "pzj";
    // 命令行解析
    Config config;
    config.parseArg(argc, argv);
    WebServer server;
    server.init(config.PORT_, user, passwd, databasename, config.LOGWrite_,
                config.OPT_LINGER_, config.TRIGMode_, config.sql_num_,
                config.thread_num_, config.close_log_, config.actor_model_);
    // 初始化各个模块
    server.logWrite();
    server.sqlPool();
    server.threadPool();
    server.trigMode();
    server.eventListen();
    // 运行
    server.eventLoop();
    return 0;
}