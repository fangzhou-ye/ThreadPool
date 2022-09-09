#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <unordered_map> 

// 接受任意数据的类型
class Any
{
public:
	Any() = default;
	~Any() = default;

	// 成员变量unique_ptr禁止左值拷贝构造
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	// 成员变量unique_ptr默认右值拷贝构造
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data))
	{}

	template<typename T>
	T cast_()
	{
		// 从base里找到所指向派生类对象中取出data
		// 基类指针=>派生类指针 RTTI
		Derive<T>* ptrDerive = dynamic_cast<Derive<T>*>(base_.get());
		if (ptrDerive == nullptr)
		{
			throw "type is not compatible";
		}
		return ptrDerive->data_;
	}

private:
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data)
		{}
	public:
		T data_;
	};

private:
	std::unique_ptr<Base> base_;
};

class Semaphore
{
public:
	Semaphore() : resourceCounter_(0)
	{}

	~Semaphore() = default;

	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]()->bool { return resourceCounter_ > 0; });
		resourceCounter_--;
	}

	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resourceCounter_++;
		cond_.notify_all();
	}
private:
	int resourceCounter_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;

class Result
{
public:
	Result(std::shared_ptr<Task> t, bool isValid = true);

	~Result() = default;

	// setVal 获取任务执行完的返回值
	void setVal(Any any);

	// get方法获取task的返回值
	Any get();

private:
	Any any_; // 存储任务的返回值
	Semaphore sem_; // 调用get方法需要
	std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
	std::atomic_bool isValid_; // 返回值是否有效（是否任务成功提交）
};

enum class PoolMode
{
	MODE_FIXED,
	MODE_CACHED
};

class Thread
{
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);
	~Thread();

	void start();

	int getThreadId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;
};

class Task;

class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	void start(int initThreadSize = 4);

	void setMode(PoolMode mode);

	void setTaskSizeMaxThres(int thres);
	// cache模式下设置线程数阈值
	void setThreadSizeMaxThres(int thres);

	// 主线程向任务队列提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数
	void threadFunc(int threadId);
	// 检查线程池运行状态
	bool checkRunningState() const;

private:
	//std::vector<std::unique_ptr<Thread>> threads_; // 线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 线程列表
	int initThreadSize_;
	std::atomic_int currentThreadSize_;
	int threadSizeMaxThres_; // cache模式最大线程数阈值
	std::atomic_int idleThreadSize_; // 空闲线程数量

	std::queue<std::shared_ptr<Task>> taskQue_; // 任务队列
	std::atomic_int taskSize_;
	int taskSizeMaxThres_; // 最大任务队列数量

	std::mutex mtxTaskQue_;
	std::condition_variable notFull_;
	std::condition_variable notEmpty_;
	std::condition_variable exitCond_; // 等待线程资源全部回收

	PoolMode poolMode_;
	std::atomic_bool isRunning_; // 线程池是否已经启动
};

// 任务抽象基类，用户可继承重写run方法
class Task
{
public:
	Task();
	~Task() = default;

	void exec();

	void setResult(Result* res);

	virtual Any run() = 0;

private:
	// 不能设为智能指针
	Result* result_; // Result生命周期长于task
};


#endif