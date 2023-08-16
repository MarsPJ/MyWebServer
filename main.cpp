#include<iostream>
#include<unistd.h>
#include"lock/locker.h"
int main(){
    locker m_locker;
    m_locker.lock();
    std::cout<<"hello"<<std::endl;
    m_locker.unlock();
    return 0;
}