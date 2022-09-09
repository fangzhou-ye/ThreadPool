#include "threadpool.h"
#include <chrono>

#include <iostream>

using ULong = unsigned long long;

class MyTask : public Task
{
public:
	MyTask(int begin, int end)
		: begin_(begin)
		, end_(end)
	{}

	// 如何设计返回值可以表示任意类型(Any )
	Any run()
	{
		std::cout << "tid: " << std::this_thread::get_id()
			<< " begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		ULong sum = 0;
		for (ULong i = begin_; i <= end_; i++)
			sum += i;
		std::cout << "tid: " << std::this_thread::get_id()
			<< " end!" << std::endl;
		return sum;
	}

private:
	int begin_;
	int end_;
};

int main()
{
	// ThreadPool对象析构如何把线程资源回收

	{
		ThreadPool pool;
		// 用户设置线程池工作模式
		pool.setMode(PoolMode::MODE_CACHED);
		// 启动线程池
		pool.start(4);

		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		Result res4 = pool.submitTask(std::make_shared<MyTask>(300000001, 400000000));
		Result res5 = pool.submitTask(std::make_shared<MyTask>(400000001, 500000000));
		Result res6 = pool.submitTask(std::make_shared<MyTask>(500000001, 600000000));

		ULong sum1 = res1.get().cast_<ULong>();
		ULong sum2 = res2.get().cast_<ULong>();
		ULong sum3 = res3.get().cast_<ULong>();
		ULong sum4 = res4.get().cast_<ULong>();
		ULong sum5 = res5.get().cast_<ULong>();
		ULong sum6 = res6.get().cast_<ULong>();

		// Master - Slave线程模型
		std::cout << (sum1 + sum2 + sum3 + sum4 + sum5 + sum6) << std::endl;

		ULong sum = 0;
		for (int i = 1; i <= 600000000; i++)
			sum += i;

		std::cout << sum << std::endl;
	}
	std::cout << "main thread finished!!!" << std::endl;
	getchar();
	return 0;
}