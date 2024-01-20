#include "thread_pool.h"
using std::memory_order_relaxed;
using std::memory_order_release;
using std::cv_status;
using std::function;
using std::mutex;
using std::unique_lock;
#ifdef _MSC_VER
#pragma warning(disable : 4455)
using std::operator""s;
#pragma warning(default : 4455)
#endif
namespace this_thread = std::this_thread;

namespace ztools {
	void ThreadPool::executor_func(ThreadPool& pool) noexcept
    {
        
        this_thread::sleep_for(std::chrono::seconds(0));
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
        }
    }
}