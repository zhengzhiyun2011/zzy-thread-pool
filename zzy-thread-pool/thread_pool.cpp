#include "thread_pool.h"
using std::memory_order_relaxed;
using std::memory_order_release;
using std::cv_status;
using std::function;
using std::lock_guard;
using std::mutex;
using std::size_t;
using std::thread;
using std::unique_lock;
using std::max;
#ifdef _MSC_VER
#pragma warning(disable : 4455)
using std::operator""s;
#pragma warning(default : 4455)
#endif
namespace chrono = std::chrono;
namespace this_thread = std::this_thread;

namespace ztools {
    LoopWaiter::Wrapper::Wrapper(size_t tasks_count)
        : count(tasks_count) {}

    LoopWaiter::LoopWaiter() noexcept
        : m_status(nullptr) {}

    LoopWaiter::LoopWaiter(size_t tasks_number)
        : m_status(std::make_shared<Wrapper>(tasks_number)) {}

    ThreadPool::ThreadPool(
        unsigned min_threads_number,
        unsigned max_threads_number,
        std::chrono::steady_clock::duration timeout_len
    )
        : m_threads_number(0)
        , m_free_threads_number(0)
        , m_added_tasks_number(0)
        , m_finished_tasks_number(0)
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

    ThreadPool::ThreadPool()
        : ThreadPool(
            max(thread::hardware_concurrency() / 2, 1U),
            thread::hardware_concurrency()
        ) {}

    ThreadPool::ThreadPool(chrono::steady_clock::duration timeout_len)
        : ThreadPool(
            max(thread::hardware_concurrency() / 2, 1U),
            thread::hardware_concurrency(),
            timeout_len
        ) {}

    ThreadPool::~ThreadPool()
    {

        {
            lock_guard<mutex> lock(m_mutex);
            m_stop = true;
        }

        m_scheduler.notify_all();

        while (m_threads_number > 0) {
            this_thread::yield();
        }
    }

    void ThreadPool::wait_all()
    {
        while (!(m_tasks.empty() && m_added_tasks_number.load(memory_order_relaxed) ==
            m_finished_tasks_number.load(memory_order_relaxed))) {
            this_thread::yield();
        }
    }

    void ThreadPool::executor_func(ThreadPool& pool) noexcept
    {
        for (;;) {
            function<void()> task;

            {
                unique_lock<mutex> lock(pool.m_mutex);
                auto result = pool.m_scheduler.wait_for(
                    lock, pool.m_timeout_len, [&pool]() {
                        if (pool.m_stop) {  // is stopped
                            return true;
                        } else if (pool.m_tasks.empty()) {  // has no tasks
                            return false;
                        } else {  // has tasks
                            return true;
                        }
                    }
                );

                // timeout and the number of threads is greater than
                // the minimum number of threads
                if (!result && pool.m_threads_number > pool.get_threads_number_limit().first) {
                    --pool.m_threads_number;
                    return;
                } else if (pool.m_stop && pool.m_tasks.empty()) {  // is stopped and 
                    --pool.m_threads_number;                       // has no tasks
                    return;
                }

                if (pool.m_tasks.empty()) {
                    continue;
                }

                // get one task
                task = pool.m_tasks.top().func;
                pool.m_tasks.pop();
            }

            // execute the task
            pool.m_free_threads_number.fetch_sub(1, memory_order_relaxed);
            task();
            pool.m_free_threads_number.fetch_add(1, memory_order_release);
            pool.m_finished_tasks_number.fetch_add(1, memory_order_relaxed);
        }
    }
}