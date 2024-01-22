#include <iostream>
#include <vector>
#include "thread_pool.h"
using std::cout;
using std::launch;
using std::vector;
using std::async;

ztools::ThreadPool pool;

int main()
{
    vector<int> arr(100);
    pool.add_loop(arr.begin(), arr.end(), [](int& e) { ++e; });
    pool.add_loop_n(arr.begin(), arr.size(), [](int& e) { ++e; });
    pool.wait_all();

    for (auto& e : arr) {
        cout << e << ' ';
    }

    return 0;
}