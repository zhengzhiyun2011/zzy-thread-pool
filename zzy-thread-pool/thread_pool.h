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
#include <vector>
namespace ztools {
    struct Task
    {
        std::function<void()> func;
        unsigned priority;
    };

    struct TaskPriorityLess
    {
        bool operator()(const Task& left, const Task& right) const noexcept
        {
            return left.priority < right.priority;
        }
    };

    class LoopWaiter
    {
    private:
        struct Wrapper {
            Wrapper() = default;
            Wrapper(std::size_t tasks_count)
                : count(tasks_count) {}

            ~Wrapper() = default;

            std::atomic_size_t count;
            std::mutex mutex;
            std::condition_variable waiter;
        };
    public:
        LoopWaiter() noexcept
            : m_status(nullptr) {}

        LoopWaiter(const LoopWaiter&) noexcept = default;
        LoopWaiter(size_t tasks_number)
            : m_status(std::make_shared<Wrapper>(tasks_number)) {}

        ~LoopWaiter() = default;

        LoopWaiter& operator=(const LoopWaiter&) noexcept = default;
    private:
        void count_down()
        {
            using std::memory_order_relaxed;
            assert(m_status != nullptr);
            m_status->count.fetch_sub(1, memory_order_relaxed);

            if (m_status->count.load(memory_order_relaxed) == 0) {
                m_status->waiter.notify_one();
            }
        }
    public:
        void wait()
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
    private:
        std::shared_ptr<Wrapper> m_status;

        friend class ThreadPool;
    };

    class ThreadPool {
    public:
        static constexpr unsigned default_task_priority = 5;
        static constexpr unsigned default_looped_times_in_one_task = 15;

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

        void wait_all()
        {
            using std::memory_order_relaxed;
            namespace this_thread = std::this_thread;
            while (!(m_tasks.empty() && m_threads_number ==
                m_free_threads_number.load(memory_order_relaxed))) {
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
            return add_task(default_task_priority, forward<Func>(func), forward<Args>(args)...);
        }

        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::forward_iterator_tag, typename Iter::iterator_category>::value, int>::type = 0>
        LoopWaiter add_loop_n(
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
            using function_type = typename std::decay<Func>::type;

            size_t tasks_number = n / one_task_processing_number;
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
            std::forward_iterator_tag, typename Iter::iterator_category>::value, int>::type = 0>
        LoopWaiter add_loop_n(
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
                n,
                move(begin),
                forward<Func>(func)
            );
        }

        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::forward_iterator_tag, typename Iter::iterator_category>::value, int>::type = 0>
        LoopWaiter add_loop_n(
            Iter begin,
            std::size_t n,
            Func&& func
        )
        {
            using std::forward;
            using std::move;

            return add_loop_n(
                default_looped_times_in_one_task,
                n,
                move(begin),
                forward<Func>(func)
            );
        }

        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::random_access_iterator_tag, typename Iter::iterator_category>::value, int>::type =
            0>
        LoopWaiter add_loop(
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
                end - begin,
                move(begin),
                forward<Func>(func)
            );
        }

        template <typename Iter, typename Func, typename std::enable_if<std::is_base_of<
            std::random_access_iterator_tag, typename Iter::iterator_category>::value, int>::type =
            0>
        LoopWaiter add_loop(
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
            std::random_access_iterator_tag, typename Iter::iterator_category>::value, int>::type =
            0>
        LoopWaiter add_loop(
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