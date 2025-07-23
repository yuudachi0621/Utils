#pragma once
#include "webServer.h"
#include <string>
#include <unordered_map>
class httpParser
{
public:
    void Init(const ClientInfo& client);

    bool Parser(const std::string& message = "");

private:
    bool ParserQuestLine(const std::string& line);

    void ParserPath();

    bool ParserQuestHeader(const std::vector<std::string>& headers);

    bool ParserBody(const std::string& body);

private:
    std::string m_reqMessage; // 请求完整报文

    std::string m_reqMethod;
    std::string m_reqPath;
    std::string m_version;
    std::string m_body;

    std::unordered_map<std::string, std::string> m_reqHeader;
};

//
// #include <atomic>
// #include <condition_variable>
// #include <deque>
// #include <functional>
// #include <future>
// #include <memory>
// #include <mutex>
// #include <random>
// #include <thread>
// #include <type_traits>
// #include <vector>
//
// class ThreadPool
//{
// public:
//    explicit ThreadPool(size_t threadNum = std::thread::hardware_concurrency())
//        : m_stop(false), m_index(0)
//    {
//        // 初始化每个线程的任务队列
//        m_queues.resize(threadNum);
//        m_threads.reserve(threadNum);
//
//        // 创建工作线程
//        for (size_t i = 0; i < threadNum; ++i)
//        {
//            m_threads.emplace_back([this, i] {
//                while (true)
//                {
//                    std::function<void()> task;
//
//                    // 优先从自己的队列获取任务
//                    if (m_queues[i].try_pop(task))
//                    {
//                        execute_task(task);
//                        continue;
//                    }
//
//                    // 自己的队列为空，尝试从其他线程窃取任务
//                    if (steal_task(task))
//                    {
//                        execute_task(task);
//                        continue;
//                    }
//
//                    // 没有任务可执行，进入休眠
//                    {
//                        std::unique_lock<std::mutex> lock(m_mutex);
//                        m_condition.wait(lock, [this, i] {
//                            return m_stop.load(std::memory_order_relaxed) || !m_queues[i].empty() || any_pending_task();
//                        });
//
//                        if (m_stop.load(std::memory_order_relaxed) && all_queues_empty())
//                        {
//                            return; // 线程池停止且所有任务完成
//                        }
//                    }
//                }
//            });
//        }
//    }
//
//    ~ThreadPool()
//    {
//        shutdown();
//    }
//
//    void shutdown()
//    {
//        m_stop.store(true, std::memory_order_relaxed);
//        m_condition.notify_all();
//        for (auto& thread : m_threads)
//        {
//            if (thread.joinable())
//            {
//                thread.join();
//            }
//        }
//    }
//
//    template <class F, class... Args>
//    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
//    {
//        using return_type = std::invoke_result_t<F, Args...>;
//
//        if (m_stop.load(std::memory_order_relaxed))
//        {
//            throw std::runtime_error("enqueue on stopped ThreadPool");
//        }
//
//        // 包装任务为shared_ptr以便捕获
//        auto task = std::make_shared<std::packaged_task<return_type()>>(
//            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
//
//        std::future<return_type> res = task->get_future();
//
//        // 随机选择一个工作队列插入任务
//        static thread_local std::default_random_engine engine(std::random_device{}());
//        std::uniform_int_distribution<size_t> dist(0, m_threads.size() - 1);
//        size_t idx = dist(engine);
//
//        // 将任务添加到队列
//        m_queues[idx].push([task]() { (*task)(); });
//
//        // 通知一个线程有新任务
//        m_condition.notify_one();
//
//        return res;
//    }
//
//    size_t thread_count() const { return m_threads.size(); }
//
// private:
//    // 线程安全的双端队列（每个线程一个）
//    class WorkStealingQueue
//    {
//    public:
//        void push(std::function<void()> task)
//        {
//            std::lock_guard<std::mutex> lock(m_mutex);
//            m_queue.push_front(std::move(task));
//        }
//
//        bool try_pop(std::function<void()>& task)
//        {
//            std::lock_guard<std::mutex> lock(m_mutex);
//            if (m_queue.empty())
//            {
//                return false;
//            }
//            task = std::move(m_queue.front());
//            m_queue.pop_front();
//            return true;
//        }
//
//        bool try_steal(std::function<void()>& task)
//        {
//            std::lock_guard<std::mutex> lock(m_mutex);
//            if (m_queue.empty())
//            {
//                return false;
//            }
//            task = std::move(m_queue.back());
//            m_queue.pop_back();
//            return true;
//        }
//
//        bool empty() const
//        {
//            std::lock_guard<std::mutex> lock(m_mutex);
//            return m_queue.empty();
//        }
//
//    private:
//        std::deque<std::function<void()>> m_queue;
//        mutable std::mutex m_mutex;
//    };
//
//    // 执行任务并捕获异常
//    void execute_task(const std::function<void()>& task)
//    {
//        try
//        {
//            task();
//        } catch (...)
//        {
//            // 可以在这里添加异常处理逻辑
//        }
//    }
//
//    // 尝试从其他线程窃取任务
//    bool steal_task(std::function<void()>& task)
//    {
//        const size_t start_idx = m_index.fetch_add(1, std::memory_order_relaxed) % m_queues.size();
//
//        for (size_t i = 0; i < m_queues.size(); ++i)
//        {
//            size_t idx = (start_idx + i) % m_queues.size();
//            if (idx != start_idx && m_queues[idx].try_steal(task))
//            {
//                return true;
//            }
//        }
//        return false;
//    }
//
//    // 检查是否有任何队列有任务
//    bool any_pending_task() const
//    {
//        for (const auto& queue : m_queues)
//        {
//            if (!queue.empty())
//            {
//                return true;
//            }
//        }
//        return false;
//    }
//
//    // 检查所有队列是否为空
//    bool all_queues_empty() const
//    {
//        for (const auto& queue : m_queues)
//        {
//            if (!queue.empty())
//            {
//                return false;
//            }
//        }
//        return true;
//    }
//
//    std::vector<std::thread> m_threads;      // 工作线程集合
//    std::vector<WorkStealingQueue> m_queues; // 每个线程一个任务队列
//    mutable std::mutex m_mutex;              // 用于条件变量的互斥锁
//    std::condition_variable m_condition;     // 线程间通信的条件变量
//    std::atomic<bool> m_stop;                // 线程池停止标志
//    std::atomic<size_t> m_index;             // 用于任务窃取的索引
//};