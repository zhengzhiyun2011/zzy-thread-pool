#pragma once
#ifndef ZTOOLS_THREAD_POOL_H_
#define ZTOOLS_THREAD_POOL_H_
#include <cassert>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
namespace ztools {
    struct Task
    {
        std::function<void()> func;
        unsigned priority;
    };

    struct TaskPriorityLess
    {
        inline bool operator()(const Task& left, const Task& right) const noexcept;
    };

    class LoopWaiter
    {
    private:
        struct Wrapper {
            Wrapper() = default;
            Wrapper(std::size_t tasks_count);
            ~Wrapper() = default;

            std::atomic_size_t count;
            std::mutex mutex;
            std::condition_variable waiter;
        };
    public:
        LoopWaiter() noexcept;
        LoopWaiter(const LoopWaiter&) noexcept = default;
        LoopWaiter(LoopWaiter&&) noexcept = default;
        LoopWaiter(size_t tasks_number);
        ~LoopWaiter() = default;
        LoopWaiter& operator=(const LoopWaiter&) noexcept = default;
    private:
        inline void count_down();
    public:
        inline void wait();
    private:
        std::shared_ptr<Wrapper> m_status;
        friend class ThreadPool;
    };

    class ThreadPool {
    public:
        static constexpr unsigned default_task_priority = 5;
        static constexpr unsigned default_looped_times_in_one_task = 15;

        inline std::pair<unsigned, unsigned> get_threads_number_limit() const noexcept;
    private:
        static void executor_func(ThreadPool& pool) noexcept;
        inline void create_thread();
    public:
        ThreadPool();
        ThreadPool(std::chrono::steady_clock::duration timeout_len);
        ThreadPool(
            unsigned min_threads_number,
            unsigned max_threads_number,
            std::chrono::steady_clock::duration timeout_len = std::chrono::seconds{ 1 }
        );
        ThreadPool(const ThreadPool&) = delete;
        ~ThreadPool();
        ThreadPool& operator=(const ThreadPool&) = delete;
        bool operator==(const ThreadPool&) = delete;

        void wait_all();
        template <typename Func, typename... Args>
        auto add_task(unsigned priority, Func&& func, Args&&... args)
            -> std::future<decltype(std::forward<Func>(func)(std::forward<Args>(args)...))>;
        template <typename Func, typename... Args>
        auto add_task(Func&& func, Args&&... args)
            -> std::future<decltype(std::forward<Func>(func)(std::forward<Args>(args)...))>;
        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::forward_iterator_tag, typename Iter::iterator_category>::value, int>::type = 0>
        LoopWaiter add_loop_n(
            unsigned priority,
            std::size_t one_task_processing_number,
            Iter begin,
            std::size_t n,
            Func&& func
        );
        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::forward_iterator_tag, typename Iter::iterator_category>::value, int>::type = 0>
        LoopWaiter add_loop_n(
            std::size_t one_task_processing_number,
            Iter begin,
            std::size_t n,
            Func&& func
        );
        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::forward_iterator_tag, typename Iter::iterator_category>::value, int>::type = 0>
        LoopWaiter add_loop_n(
            Iter begin,
            std::size_t n,
            Func&& func
        );
        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::random_access_iterator_tag, typename Iter::iterator_category>::value, int>::type =
            0>
        LoopWaiter add_loop(
            unsigned priority,
            std::size_t one_task_processing_number,
            Iter begin,
            Iter end,
            Func&& func
        );
        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::random_access_iterator_tag, typename Iter::iterator_category>::value, int>::type =
            0>
        LoopWaiter add_loop(
            std::size_t one_task_processing_number,
            Iter begin,
            Iter end,
            Func&& func
        );
        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::random_access_iterator_tag, typename Iter::iterator_category>::value, int>::type =
            0>
        LoopWaiter add_loop(
            Iter begin,
            Iter end,
            Func&& func
        );
    private:
        std::mutex m_mutex;
        std::condition_variable m_scheduler;
        std::atomic_uint m_free_threads_number;
        std::atomic_ullong m_added_tasks_number;
        std::atomic_ullong m_finished_tasks_number;

        // Access is all in a locked environment, so there is no need for atomic variables.
        unsigned m_threads_number;
        bool m_stop;
        unsigned m_max_threads_number;
        unsigned m_min_threads_number;
        std::chrono::steady_clock::duration m_timeout_len;
        std::priority_queue<Task, std::vector<Task>, TaskPriorityLess> m_tasks;
    };

    inline bool TaskPriorityLess::operator()(const Task& left, const Task& right) const noexcept
    {
        return left.priority > right.priority;
    }

    inline void LoopWaiter::count_down()
    {
        using std::memory_order_relaxed;
        assert(m_status != nullptr);
        m_status->count.fetch_sub(1, memory_order_relaxed);

        if (m_status->count.load(memory_order_relaxed) == 0) {
            m_status->waiter.notify_one();
        }
    }

    inline void LoopWaiter::wait()
    {
        using std::memory_order_relaxed;
        using std::unique_lock;
        using std::mutex;

        assert(m_status != nullptr);
        if (m_status->count.load(memory_order_relaxed) > 0) {
            unique_lock<mutex> lock(m_status->mutex);
            m_status->waiter.wait(
                lock,
                [this]() {
                    return this->m_status->count.load(memory_order_relaxed) == 0;
                }
            );
        }
    }

    inline std::pair<unsigned, unsigned> ThreadPool::get_threads_number_limit() const noexcept
    {
        return {
            m_min_threads_number, m_max_threads_number
        };
    }

    inline void ThreadPool::create_thread()
    {
        using std::memory_order_relaxed;
        using std::thread;
        using std::ref;

        ++m_threads_number;
        m_free_threads_number.fetch_add(1, memory_order_relaxed);
        thread{ executor_func, ref(*this) }.detach();
    }

    template <typename Func, typename... Args>
    auto ThreadPool::add_task(unsigned priority, Func&& func, Args&&... args)
        -> std::future<decltype(std::forward<Func>(func)(std::forward<Args>(args)...))>
    {
        using std::memory_order_relaxed;
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
            // TODO: ÐÞ¸´ThreadPoolµÄBug

            if (m_free_threads_number.load(memory_order_relaxed) == 0 &&
                m_threads_number < get_threads_number_limit().second) {
                create_thread();
            }

            m_added_tasks_number.fetch_add(1, memory_order_relaxed);
            m_tasks.emplace(move(task));
        }

        m_scheduler.notify_one();
        return task_func->get_future();
    }

    template <typename Func, typename... Args>
    auto ThreadPool::add_task(Func&& func, Args&&... args)
        -> std::future<decltype(std::forward<Func>(func)(std::forward<Args>(args)...))>
    {
        using std::forward;
        return add_task(default_task_priority, forward<Func>(func), forward<Args>(args)...);
    }

    template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
        std::forward_iterator_tag, typename Iter::iterator_category>::value, int>::type>
    LoopWaiter ThreadPool::add_loop_n(
        unsigned priority,
        std::size_t one_task_processing_number,
        Iter begin,
        std::size_t n,
        Func&& func
    )
    {
        using std::size_t;
        using std::forward;
        using std::make_shared;
        using std::max;
        using function_type = typename std::decay<Func>::type;

        size_t tasks_number = max<size_t>(n / one_task_processing_number, 1);
        size_t remaining_number = n % one_task_processing_number;
        LoopWaiter waiter(tasks_number);
        auto packaged_func = make_shared<function_type>(forward<Func>(func));

        if (n < one_task_processing_number) {
            add_task(
                priority,
                [packaged_func, waiter](Iter begin, Iter end) mutable noexcept {
                    for (; begin != end; ++begin) {
                        (*packaged_func)(*begin);
                    }

                    waiter.count_down();
                },
                begin, begin + n
            );
        } else {
            // Create tasks_number - 1 tasks
            while (--tasks_number) {
                add_task(
                    priority,
                    [packaged_func, waiter](Iter begin, Iter end) mutable noexcept {
                        for (; begin != end; ++begin) {
                            (*packaged_func)(*begin);
                        }

                        waiter.count_down();
                    },
                    begin, begin + one_task_processing_number
                );

                begin += one_task_processing_number;
            }

            // Create the last task
            add_task(
                priority,
                [packaged_func, waiter](Iter begin, Iter end) mutable noexcept {
                    for (; begin != end; ++begin) {
                        (*packaged_func)(*begin);
                    }

                    waiter.count_down();
                },
                begin, begin + one_task_processing_number + remaining_number
            );
        }

        return waiter;
    }

    template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
        std::forward_iterator_tag, typename Iter::iterator_category>::value, int>::type>
    LoopWaiter ThreadPool::add_loop_n(
        std::size_t one_task_processing_number,
        Iter begin,
        std::size_t n,
        Func&& func
    )
    {
        using std::forward;
        using std::move;

        return add_loop_n(
            default_task_priority,
            one_task_processing_number,
            move(begin),
            n,
            forward<Func>(func)
        );
    }

    template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
        std::forward_iterator_tag, typename Iter::iterator_category>::value, int>::type>
    LoopWaiter ThreadPool::add_loop_n(
        Iter begin,
        std::size_t n,
        Func&& func
    )
    {
        using std::forward;
        using std::move;

        return add_loop_n(
            default_looped_times_in_one_task,
            move(begin),
            n,
            forward<Func>(func)
        );
    }

    template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
        std::random_access_iterator_tag, typename Iter::iterator_category>::value, int>::type>
    LoopWaiter ThreadPool::add_loop(
        unsigned priority,
        std::size_t one_task_processing_number,
        Iter begin,
        Iter end,
        Func&& func
    )
    {
        using std::forward;
        using std::move;

        return add_loop_n(
            priority,
            one_task_processing_number,
            move(begin),
            end - begin,
            forward<Func>(func)
        );
    }

    template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
        std::random_access_iterator_tag, typename Iter::iterator_category>::value, int>::type>
    LoopWaiter ThreadPool::add_loop(
        std::size_t one_task_processing_number,
        Iter begin,
        Iter end,
        Func&& func
    )
    {
        using std::forward;
        using std::move;

        return add_loop(
            default_task_priority,
            one_task_processing_number,
            move(begin),
            move(end),
            forward<Func>(func)
        );
    }

    template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
        std::random_access_iterator_tag, typename Iter::iterator_category>::value, int>::type>
    LoopWaiter ThreadPool::add_loop(
        Iter begin,
        Iter end,
        Func&& func
    )
    {
        using std::forward;
        using std::move;

        return add_loop(
            default_looped_times_in_one_task,
            move(begin),
            move(end),
            forward<Func>(func)
        );
    }
}
#endif // ZTOOLS_THREAD_POOL_H_