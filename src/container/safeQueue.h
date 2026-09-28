#ifndef __SAFE_QUEUE_H__
#define __SAFE_QUEUE_H__

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace util {

/**
 * 线程安全双端队列。
 *
 * 设计取舍：
 * - m_deque 的所有修改必须在 m_mutex 下完成；
 * - m_size 是轻量级近似计数，empty()/size() 无需加锁；
 * - push 只在入队完成后通知，减少等待线程的无效唤醒；
 * - try_pop 不会阻塞，适合带有停止标志的后台线程；
 * - 未提供 close 语义时，pop() 会持续等待队列非空。
 */
template <typename T>
class SafeQueue
{
public:
    SafeQueue() = default;
    SafeQueue(const SafeQueue&) = delete;
    SafeQueue& operator=(const SafeQueue&) = delete;

    // 通用入队接口；保留 push/push_back 两个名称以兼容现有调用方。
    void push(const T& item) { emplace_back(item); }
    void push(T&& item) { emplace_back(std::move(item)); }

    void push_front(const T& item) { emplace_front(item); }
    void push_front(T&& item) { emplace_front(std::move(item)); }

    void push_back(const T& item) { emplace_back(item); }
    void push_back(T&& item) { emplace_back(std::move(item)); }

    // 仅用于快速判断，不能作为后续 pop 的严格同步条件。
    bool empty() const noexcept
    {
        return m_size.load(std::memory_order_acquire) == 0;
    }

    size_t size() const noexcept
    {
        return m_size.load(std::memory_order_relaxed);
    }

    void clear()
    {
        std::deque<T> emptyDeque;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_deque.swap(emptyDeque);
            m_size.store(0, std::memory_order_release);
        }
    }

    // 阻塞式取出；调用者必须确保停止条件不会永久缺失。
    T pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] {
            return !m_deque.empty();
        });

        T item = std::move(m_deque.front());
        m_deque.pop_front();
        m_size.fetch_sub(1, std::memory_order_release);
        return item;
    }

    // 非阻塞取出，供日志写线程等带停止标志的后台线程优雅退出。
    bool try_pop(T& item)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_deque.empty())
            return false;

        item = std::move(m_deque.front());
        m_deque.pop_front();
        m_size.fetch_sub(1, std::memory_order_release);
        return true;
    }

private:
    template <typename... Args>
    void emplace_front(Args&&... args)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_deque.emplace_front(std::forward<Args>(args)...);
            m_size.fetch_add(1, std::memory_order_release);
        }
        m_cv.notify_one();
    }

    template <typename... Args>
    void emplace_back(Args&&... args)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_deque.emplace_back(std::forward<Args>(args)...);
            m_size.fetch_add(1, std::memory_order_release);
        }
        m_cv.notify_one();
    }

private:
    std::deque<T> m_deque;
    std::atomic<size_t> m_size{0};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
};

} // namespace util
#endif
