
// TODO：函数声明和实现完全分离，类包含.h和.cpp文件？

#include<iostream>
#include<unistd.h>
#include"lock/locker.h"
#include"log/log.h"
int main(){
    locker m_locker;
    m_locker.lock();
    std::cout<<"hello"<<std::endl;
    m_locker.unlock();
    Log::getInstance()->init("./aaa", 0);
    return 0;
}