#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace util {

class ThreadPool
{
public:
    ThreadPool(size_t threadNum)
        : m_stop(false)
    {
        for (size_t i = 0; i < threadNum; i++)
        {
            m_workers.emplace_back(
                [this] {
                    while (true)
                    {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this->m_mutex);
                            this->m_condition.wait(lock, [this] {
                                return this->m_stop || !this->m_tasks.empty();
                            });

                            if (this->m_stop && this->m_tasks.empty())
                                return;

                            task = std::move(this->m_tasks.front());
                            m_tasks.pop();
                        }
                        task();
                    }
                });
        }
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_condition.notify_all();
        for (auto& worker : m_workers)
            worker.join();
    }

    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using returnType = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<returnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<returnType> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            if (m_stop)
            {
                throw std::runtime_error("enqueue error! ThreadPool is invalid!");
            }
            m_tasks.emplace([task]() { (*task)(); });
        }
        m_condition.notify_one();
        return res;
    }

private:
    // works
    std::vector<std::thread> m_workers;

    // tasks queue
    std::queue<std::function<void()>> m_tasks;

    // synchronization
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_stop;
};

} // namespace util
#endif // !__THREADPOOL_H__
