#include <iostream>
#include "thread_pool.h"
using namespace std;

ztools::ThreadPool pool;

int main()
{
	auto res1 = async(
		launch::async, [] {
			pool.add_task(
				1,
				[] {
					cout << "This is thread 1.\n";
				}
			);

			pool.add_task(
				2,
				[] {
					cout << "This is thread 2.\n";
				}
			);
		}
	);

	auto res2 = async(
		launch::async, [] {
			pool.add_task(
				[] {
					cout << "This is thread 3.\n";
				}
			);
		}
	);

	res1.get();
	res2.get();

	return 0;
}