#include <iostream>

#include "thread_pool.h"
using ztools::ThreadPool;
using namespace std;

int main()
{
    ThreadPool pool;
    atomic_int result{ 0 };
    for (int i = 1; i <= 100; ++i) {
        pool.add_task(
            [](atomic_int& data, int add_value) {
                data.fetch_add(add_value, memory_order_relaxed);
            },
            ref(result), i
        );
    }

    pool.wait_all();
    cout << result.load(memory_order_relaxed) << '\n';

    return 0;
}