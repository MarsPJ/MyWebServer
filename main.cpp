
// TODO：函数声明和实现完全分离，类包含.h和.cpp文件？

#include<iostream>
#include<unistd.h>
#include"lock/locker.h"
#include"log/log.h"
#include"CGImysql/sql_connection_pool.h"
int main(){
    locker m_locker;
    m_locker.lock();
    std::cout<<"hello"<<std::endl;
    m_locker.unlock();
    Log::getInstance()->init("./aaa", 0);
    std::string user = "root";
    std::string passwd = "131317lP";
    std::string databasename = "pzj";
    ConnectionPool::getInstance()->init("localhost", user, passwd, databasename, 3306, 8, 0);
    return 0;
}