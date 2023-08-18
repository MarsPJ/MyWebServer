#include "sql_connection_pool.h"

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *ConnectionPool::getConnection() {

    MYSQL* con = nullptr;
    // 检查连接池是否还有可用连接
    if (0 == conn_list_.size()) {
        return nullptr;
    }


    // P操作
    reserve_.wait();

    // 上锁，保护临界区conn_list_、m_free_conn_、m_cur_conn_
    lock_.lock();
    con = conn_list_.front();
    conn_list_.pop_front();

    --m_free_conn_;
    ++m_cur_conn_;

    lock_.unlock();
    return con;
}

bool ConnectionPool::releaseConnection(MYSQL *con) {
    if (nullptr == con) {
        return false;
    }

    // 上锁，保护临界区conn_list_、m_free_conn_、m_cur_conn_
    lock_.lock();
    // 重新放入连接池即可，不需要先销毁再创建
    conn_list_.push_back(con);
    ++m_free_conn_;
    --m_cur_conn_;
    lock_.unlock();

    // V操作
    reserve_.post();
    return true;

}

int ConnectionPool::getFreeConn()
{
    return this->m_free_conn_;
}

void ConnectionPool::destroyPool() {
    lock_.lock();
    if (conn_list_.size() > 0) {
        for(auto& it:conn_list_) {
            mysql_close(it);
        }
        m_cur_conn_ = 0;
        m_free_conn_ = 0;
        conn_list_.clear();

    }
    lock_.unlock();

}

ConnectionPool *ConnectionPool::getInstance() {
    // 单例模式-懒汉模式
    static ConnectionPool conn_pool;
    return &conn_pool;
}

void ConnectionPool::init(std::string url, std::string user, std::string password, std::string database_name, int port, int max_conn, int close_log) {
    m_url_ = url;
    m_user_ = user;
    m_port_ = port;
    m_password_ = password;
    m_database_name_ = database_name;
    m_close_log_ = close_log;


    for (int i = 0; i < max_conn; ++i) {
        MYSQL* con = nullptr;
        con = mysql_init(con);
        if (nullptr == con) {
            LOG_ERROR("MySQL Initializing Error");
            exit(1);
        }

        // 函数的返回值是一个 MYSQL * 类型的指针，
        // 如果连接成功，则返回一个指向连接对象的指针，否则返回 NULL
        con = mysql_real_connect(con, url.c_str(), user.c_str(), password.c_str(), database_name.c_str(), port, nullptr, 0);

        if (nullptr == con) {
            LOG_ERROR("MySQL Connecting Error");
            exit(1);
        }

        conn_list_.push_back(con);
        ++m_free_conn_;
    }

    reserve_ = Sem(m_free_conn_); 
    // 只有成功创建的连接才会被计算入内
    m_max_conn_ = m_free_conn_;

}

ConnectionPool::ConnectionPool() {
    m_cur_conn_ = 0;
    m_free_conn_ = 0;
}

ConnectionPool::~ConnectionPool() {
    destroyPool();
}



ConnectionRAII::ConnectionRAII(MYSQL **con, ConnectionPool *conn_pool) {
    *con = conn_pool->getConnection();
    con_RAII_ = *con;
    pool_RAII_ = conn_pool;
}

ConnectionRAII::~ConnectionRAII() {
    pool_RAII_->releaseConnection(con_RAII_);
}
