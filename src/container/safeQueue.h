#ifndef __SAFE_QUEUE_H__
#define __SAFE_QUEUE_H__

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace util {
template <typename T>
class SafeQueue
{
public:
    SafeQueue()
        : m_size(0) {};

    void push(const T& item) { emplace_back(item); }
    void push(T&& item) { emplace_back(std::move(item)); }

    void push_front(const T& item) { emplace_front(item); }
    void push_front(T&& item) { emplace_front(std::move(item)); }

    void push_back(const T& item) { emplace_back(item); }
    void push_back(T&& item) { emplace_back(std::move(item)); }

    bool empty() const { return m_size.load() == 0; }

    bool size() const { return m_size.load(); }

    void clear()
    {
        std::deque<T> empty_deque;
        std::unique_lock<std::mutex> lock(m_mutex);
        std::swap(m_deque, empty_deque);
        m_size.store(0);
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_deque.empty()) m_cv.wait(lock);
        T item = std::move(m_deque.front());
        m_deque.pop_front();
        m_size--;
        return item;
    }

private:
    template <typename... Args>
    void emplace_front(Args&&... args)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_deque.emplace_front(std::forward<Args>(args)...);
        m_size++;
        lock.unlock();
        m_cv.notify_one();
    }

    template <typename... Args>
    void emplace_back(Args&&... args)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_deque.emplace_back(std::forward<Args>(args)...);
        m_size++;
        lock.unlock();
        m_cv.notify_one();
    }

private:
    std::deque<T> m_deque;
    std::atomic<int> m_size;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
};
} // namespace util
#endif // !__SAFE_QUEUE_H__
