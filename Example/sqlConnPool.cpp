#include "sqlConnPool.h"

sqlConnPool::sqlConnPool()
{
}

sqlConnPool::~sqlConnPool()
{
    ClosePool();
}

sqlConnPool& sqlConnPool::Instance()
{
    static sqlConnPool instance;
    return instance;
}

void sqlConnPool::Init(const std::string& host, int port, const std::string& user, const std::string& pwd, const std::string& dbName, int connSize)
{
    for (int i = 0; i < connSize; i++)
    {
        MYSQL* sqlPtr = nullptr;
        sqlPtr        = mysql_init(sqlPtr);
        if (sqlPtr == nullptr)
        {
        }
        sqlPtr = mysql_real_connect(sqlPtr, host.data(), user.data(), pwd.data(), dbName.data(), port, nullptr, 0);
        if (sqlPtr == nullptr)
        {
        }
        m_connQue.push(sqlPtr);
    }
}

MYSQL* sqlConnPool::GetFreeConn()
{
    if (m_connQue.empty())
        return nullptr;

    std::lock_guard<std::mutex> lock(m_mutex);
    MYSQL* sqlPtr = nullptr;
    sqlPtr        = m_connQue.pop();
    return sqlPtr;
}

void sqlConnPool::FreeConn(MYSQL* conn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connQue.push(conn);
}

int sqlConnPool::GetFreeConnCount()
{
    return m_connQue.size();
}

void sqlConnPool::ClosePool()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_connQue.empty())
    {
        MYSQL* sqlPtr = m_connQue.pop();
        mysql_close(sqlPtr);
    }
    mysql_library_end();
}
