#include "sqlConnPool.h"
#include "log.h"

util::sqlConnPool::sqlConnPool()
{
}

util::sqlConnPool::~sqlConnPool()
{
    ClosePool();
}

util::sqlConnPool& util::sqlConnPool::Instance()
{
    static sqlConnPool instance;
    return instance;
}

void util::sqlConnPool::Init(const std::string& host, int port, const std::string& user, const std::string& pwd, const std::string& dbName, int connSize)
{
    for (int i = 0; i < connSize; i++)
    {
        MYSQL* sqlPtr = nullptr;
        sqlPtr        = mysql_init(sqlPtr);
        if (sqlPtr == nullptr)
        {
            LOG_ERROR("mysql init %d error!", i);
            continue;
        }
        sqlPtr = mysql_real_connect(sqlPtr, host.data(), user.data(), pwd.data(), dbName.data(), port, nullptr, 0);
        if (sqlPtr == nullptr)
        {
            LOG_ERROR("mysql real connect %d error!", i);
            continue;
        }
        m_connQue.push(sqlPtr);
    }
}

MYSQL* util::sqlConnPool::GetFreeConn()
{
    if (m_connQue.empty())
    {
        LOG_WARN("free sql connection is empty, so busy!");
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    MYSQL* sqlPtr = nullptr;
    sqlPtr        = m_connQue.pop();
    return sqlPtr;
}

void util::sqlConnPool::FreeConn(MYSQL* conn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connQue.push(conn);
}

int util::sqlConnPool::GetFreeConnCount()
{
    return m_connQue.size();
}

void util::sqlConnPool::ClosePool()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_connQue.empty())
    {
        MYSQL* sqlPtr = m_connQue.pop();
        mysql_close(sqlPtr);
    }
    mysql_library_end();
}
