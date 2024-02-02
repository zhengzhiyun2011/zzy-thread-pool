#include <atomic>
#include <chrono>
#include <future>
#include <numeric>
#include <thread>
#include <vector>

#include "../zzy-thread-pool/thread_pool.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;
using namespace ztools;

namespace UnitTest
{
	TEST_CLASS(UnitTest)
	{
	public:
		
		TEST_METHOD(test_thread_pool_construct_and_destroy)
		{
			try {
				ThreadPool pool1;
				ThreadPool pool2(1s);
				ThreadPool pool3(0, 20, 2s);
			} catch (...) {
				Assert::Fail(L"Catch Exception");
			}
		}

		TEST_METHOD(test_thread_pool_add_task)
		{
			try {
				ThreadPool pool;
				auto result = pool.add_task([]() { return 100; });
				Assert::AreEqual(100, result.get(), L"The result isn\'t correct.");
			} catch (...) {
				Assert::Fail(L"Catch Exception");
			}
		}

		TEST_METHOD(test_thread_pool_add_loop)
		{
			try {
				ThreadPool pool;
				vector<int> arr(100);
				auto add_one = [](int& e) { ++e; };
				auto waiter = pool.add_loop(arr.begin(), arr.end(), add_one);
				waiter.wait();
				Assert::AreEqual(
					100,
					accumulate(arr.begin(), arr.end(), 0),
					L"The result isn\'t correct."
				);
			} catch (...) {
				Assert::Fail(L"Catch exception.");
			}
		}

		TEST_METHOD(test_thread_pool_add_loop_n)
		{
			try {
				ThreadPool pool;
				vector<atomic_int> arr(100);
				auto waiter1 = pool.add_loop_n(arr.begin(), 100, [](atomic_int& e) { ++e; });
				auto waiter2 = pool.add_loop_n(200, arr.begin(), 100, [](atomic_int& e) { ++e; });
				waiter1.wait();
				waiter2.wait();
				Assert::AreEqual(
					200,
					accumulate(arr.begin(), arr.end(), 0),
					L"The result isn\'t correct."
				);
			} catch (...) {
				Assert::Fail(L"Catch exception.");
			}
		}

		TEST_METHOD(test_thread_pool_wait_all)
		{
			try {
				ThreadPool pool;
				atomic_uint result = 0;
				for (int i = 1; i <= 10000; ++i) {
					pool.add_task(
						[](atomic_uint& data, int add_value) {
							data.fetch_add(add_value, memory_order_relaxed);
						},
						ref(result),
						i
					);
				}

				pool.wait_all();

				unsigned reference_result = 0;
				for (int i = 1; i <= 10000; ++i) {
					reference_result += i;
				}

				Assert::AreEqual(
					reference_result,
					result.load(memory_order_relaxed),
					L"The result isn\'t correct"
				);
			} catch (...) {
				Assert::Fail(L"Catch exception.");
			}
		}

		TEST_METHOD(test_thread_pool_dynamic_threads_number)
		{
			try {
				ThreadPool pool(100ms);
				for (int i = 0; i < 1000; ++i) {
					pool.add_task(
						this_thread::sleep_for<chrono::milliseconds::rep,
						chrono::milliseconds::period>,
						1ms
					);
				}

				this_thread::sleep_for(1500ms);

				for (int i = 0; i < 1000; ++i) {
					pool.add_task(
						this_thread::sleep_for<chrono::milliseconds::rep,
						chrono::milliseconds::period>,
						1ms
					);
				}
			} catch (...) {
				Assert::Fail(L"Catch exception.");
			}
		}
	};
}
