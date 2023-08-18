#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_


/**
 * 线程安全的数据库连接池
 * 
 * 
*/
#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

class ConnectionPool {
public:
    MYSQL* getConnection();             // (P操作)有请求时，返回一个数据库连接
    bool releaseConnection(MYSQL* con); // （V操作）释放当前所用的conn连接
    int getFreeConn();                  // 获取当前空闲连接数
    void destroyPool();                 // 销毁所有连接(不是释放，所有都不可用)


    // 单例模式
    static ConnectionPool* getInstance();
    void init(std::string url, std::string user, std::string password, std::string database_name, int port, int max_conn, int close_log);
private:
    ConnectionPool();
    ~ConnectionPool();

    int m_max_conn_;            // 最大连接数
    int m_cur_conn_;            // 当前已使用的连接数
    int m_free_conn_;           // 当前空闲的连接数
    locker lock_;
    std::list<MYSQL*> conn_list_;// 连接池
    Sem reserve_;

public:
    std::string m_url_;          // 主机地址
    std::string m_port_;         // 数据库端口号
    std::string m_user_;         // 登录数据库用户名
    std::string m_password_;     // 登录数据库密码
    std::string m_database_name_;// 使用数据库名
    int m_close_log_;            // 日志开关


};

/**
 * 
 * 用于在构造函数中获取数据库连接，
 * 并在析构函数中释放连接，以确保数据库连接的安全获取和释放
 * RAII 的核心思想是将资源的生命周期与对象的生命周期绑定在一起，
 * 通过对象的构造函数在资源获取时初始化资源，在对象的析构函数中释放资源，
 * 从而确保资源的正确管理。
 * 
*/
class ConnectionRAII {
public:
    // 初始化即可获得一个可用的数据库连接GetConnection
    ConnectionRAII(MYSQL** con, ConnectionPool* conn_pool);
    // 析构时释放这个连接ReleaseConnection
    ~ConnectionRAII();
private:
    // 指向一个可用的数据库连接
    MYSQL* con_RAII_;
    // con_RAII_所属的连接池
    ConnectionPool* pool_RAII_;
};


#endif

