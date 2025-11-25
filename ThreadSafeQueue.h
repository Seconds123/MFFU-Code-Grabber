// ThreadSafeQueue.h
#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class ThreadSafeQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(value));

        m_cond.notify_one();
    }

    bool wait_and_pop(T& value) {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_cond.wait(lock, [this] { return !m_queue.empty() || m_stop; });

        if (m_stop && m_queue.empty()) {
            return false;
        }

        value = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        
        m_cond.notify_all();
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    bool m_stop = false; // Stop signal
};