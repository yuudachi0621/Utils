#pragma once
#include "safeQueue.h"
#include <atomic>
#include <mysql.h>
#include <string>
#include <mutex>

using namespace util;

class sqlConnPool
{
protected:
    sqlConnPool();

public:
    ~sqlConnPool();
    sqlConnPool(const sqlConnPool&)            = delete;
    sqlConnPool& operator=(const sqlConnPool&) = delete;

public:
    static sqlConnPool& Instance();

    void Init(const std::string& host, int port, const std::string& user, const std::string& pwd, const std::string& dbName, int connSize);

    MYSQL* GetFreeConn();

    void FreeConn(MYSQL* conn);

    int GetFreeConnCount();

    void ClosePool();

private:
    SafeQueue<MYSQL*> m_connQue;
    std::mutex m_mutex;
};
