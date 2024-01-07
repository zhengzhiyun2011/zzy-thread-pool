#pragma once
#ifndef ZTOOLS_THREAD_POOL_H_
#define ZTOOLS_THREAD_POOL_H_
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
namespace ztools {
    struct Task {
        std::function<void()> func;
        unsigned priority;
    };

    struct TaskPriorityLess {
        bool operator()(const Task& left, const Task& right) const noexcept
        {
            return left.priority < right.priority;
        }
    };

    class ThreadPool {
    public:
        std::pair<unsigned, unsigned> get_threads_number_limit() const noexcept
        {
            return {
                m_min_threads_number, m_max_threads_number
            };
        }
    private:
        static void executor_func(ThreadPool& pool) noexcept;
        void create_thread()
        {
            using std::memory_order_release;
            using std::thread;
            using std::ref;

            ++m_threads_number;
            m_free_threads_number.fetch_add(1, memory_order_release);
            thread{ executor_func, ref(*this) }.detach();
        }
    public:
        ThreadPool()
            : ThreadPool(
                std::max(std::thread::hardware_concurrency() / 2, 1U),
                std::thread::hardware_concurrency()
            ) {}

        ThreadPool(std::chrono::steady_clock::duration timeout_len)
            : ThreadPool(
                std::max(std::thread::hardware_concurrency() / 2, 1U),
                std::thread::hardware_concurrency(),
                timeout_len
            ) {}

        ThreadPool(
            unsigned min_threads_number,
            unsigned max_threads_number, 
            std::chrono::steady_clock::duration timeout_len = std::chrono::seconds{ 1 }
        )
            : m_threads_number(0)
            , m_free_threads_number(0)
            , m_stop(false)
            , m_min_threads_number(min_threads_number)
            , m_max_threads_number(max_threads_number)
            , m_timeout_len(timeout_len)
        {
            using std::thread;
            using std::ref;

            for (unsigned i = 0; i < min_threads_number; ++i) {
                create_thread();
            }
        }

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        bool operator==(const ThreadPool&) = delete;
        
        ~ThreadPool()
        {
            using std::lock_guard;
            using std::mutex;
            namespace this_thread = std::this_thread;

            {
                lock_guard<mutex> lock(m_mutex);
                m_stop = true;
            }

            m_scheduler.notify_all();

            while (m_threads_number > 0) {
                this_thread::yield();
            }
        }

        template <typename Func, typename... Args>
        auto add_task(unsigned priority, Func&& func, Args&&... args)
            -> std::future<decltype(std::forward<Func>(func)(std::forward<Args>(args)...))>
        {
            using std::memory_order_acquire;
            using std::lock_guard;
            using std::mutex;
            using std::packaged_task;
            using std::bind;
            using std::forward;
            using std::make_shared;
            using std::move;
            using std::ref;
            using result_type = decltype(forward<Func>(func)(forward<Args>(args)...));

            auto task_func = make_shared<packaged_task<result_type()>>(
                bind(forward<Func>(func), forward<Args>(args)...)
            );

            Task task{
                [task_func] {
                    (*task_func)();
                },
                priority
            };

            {
                lock_guard<mutex> lock(m_mutex);

                if (m_free_threads_number.load(memory_order_acquire) == 0 &&
                    m_threads_number < get_threads_number_limit().second) {
                    create_thread();
                }

                m_tasks.emplace(move(task));
            }

            m_scheduler.notify_one();
            return task_func->get_future();
        }

        template <typename Func, typename... Args>
        auto add_task(Func&& func, Args&&... args)
            -> std::future<decltype(std::forward<Func>(func)(std::forward<Args>(args)...))>
        {
            using std::forward;
            return add_task(5, forward<Func>(func), forward<Args>(args)...);
        }
    private:
        std::mutex m_mutex;
        std::condition_variable m_scheduler;
        std::atomic_uint m_free_threads_number;

        // Access is all in a locked environment, so there is no need for atomic variables.
        unsigned m_threads_number;
        bool m_stop;
        unsigned m_max_threads_number;
        unsigned m_min_threads_number;
        std::chrono::steady_clock::duration m_timeout_len;
        std::priority_queue<Task, std::vector<Task>, TaskPriorityLess> m_tasks;
    };
}
#endif // ZTOOLS_THREAD_POOL_H_