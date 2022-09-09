#include "threadpool.h"

const int TASK_MAX_THRESHOLD = INT32_MAX;
const int THREAD_MAX_THRESHOLD = 10; // 1024
const int THREAD_MAX_IDLE_TIME = 10; // 60

// ------------------------------ ThreadPool ------------------------------
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, idleThreadSize_(0)
	, currentThreadSize_(0)
	, threadSizeMaxThres_(THREAD_MAX_THRESHOLD)
	, taskSize_(0)
	, taskSizeMaxThres_(TASK_MAX_THRESHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isRunning_(false)
{}

ThreadPool::~ThreadPool()
{
	isRunning_ = false;
	// 等待线程池所有线程返回 => 阻塞 & 执行任务中
	std::unique_lock<std::mutex> lock(mtxTaskQue_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
}

void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

void ThreadPool::setTaskSizeMaxThres(int thres)
{
	if (checkRunningState())
		return;
	taskSizeMaxThres_ = thres;
}

void ThreadPool::setThreadSizeMaxThres(int thres)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeMaxThres_ = thres;
	}
}

// 用户主线程向任务队列生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lock(mtxTaskQue_);
	// 线程通信，等待任务队列有空余
	//while (taskQue_.size() == taskSizeMaxThres_) {
	//	notFull_.wait(lock);
	//}
	// notFull_.wait(lock, [&]()->bool {return taskQue_.size() < taskSizeMaxThres_; });

	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskSizeMaxThres_; }))
	{
		// notFull_等待一秒钟依然没有满足，任务提交失败
		std::cerr << "task queue is full, task submission failed" << std::endl;
		return Result(sp, false);
	}
	// 新加入任务，通知notEmpty_
	taskQue_.emplace(sp);
	taskSize_++;
	notEmpty_.notify_all();

	// cache模式需要根据任务数量和空闲线程数量动态的创建新的线程
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& currentThreadSize_ < threadSizeMaxThres_)
	{
		std::cout << ">>>>>>>>>>>>>>create new thread>>>>>>>>>>>>>>>>>>>>" << std::endl;
		// 创建新线程对象并启动
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int tid = ptr->getThreadId();
		threads_.emplace(tid, std::move(ptr));
		threads_[tid]->start();
		currentThreadSize_++;
		idleThreadSize_++;
	}

	// 返回Task任务的Result对象
	return Result(sp);
}

void ThreadPool::start(int initThreadSize)
{
	isRunning_ = true;
	initThreadSize_ = initThreadSize;
	currentThreadSize_ = initThreadSize;
	// 创建线程对象,把线程函数(ThreadPool中)传给线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		// 创建unique_ptr<Thread>
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int tid = ptr->getThreadId();
		threads_.emplace(tid, std::move(ptr));
	}
	// 启动所有线程
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++;
	}
}

bool ThreadPool::checkRunningState() const
{
	return isRunning_;
}

// ThreadPool中定义的线程函数，线程池线程函数竞态从任务队列消费任务
void ThreadPool::threadFunc(int threadId)
{
	auto lastCallingTime = std::chrono::high_resolution_clock::now();
	// 所有任务必须执行完成,线程池才能回收所有线程
	for (;;)
	{
		// 获取锁
		std::shared_ptr<Task> task = nullptr;
		{
			std::unique_lock<std::mutex> lock(mtxTaskQue_);

			std::cout << "tid: " << std::this_thread::get_id()
				<< " 尝试获取任务..." << std::endl;

			// cache模式下，initThreadSize之外新创建的空闲线程空闲时间超过60s把多余线程回收
			// 当前时间 上一次线程执行时间
			// 锁+双重判断
			while (taskQue_.size() == 0)
			{
				// 线程池要结束，回收线程资源
				if (!isRunning_)
				{
					threads_.erase(threadId);
					std::cout << "tid: " << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return; // 线程函数结束，线程结束
				}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					if (notEmpty_.wait_for(lock, std::chrono::seconds(1)) ==
						std::cv_status::timeout)
					{
						auto now = std::chrono::high_resolution_clock::now();
						auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - lastCallingTime);
						if (duration.count() >= THREAD_MAX_THRESHOLD
							&& currentThreadSize_ > initThreadSize_)
						{
							// 开始回收当前线程
							// threadId => thread对象 => 删除
							threads_.erase(threadId);
							currentThreadSize_--;
							idleThreadSize_--;
							std::cout << "tid: " << std::this_thread::get_id() << " exit!" << std::endl;
							return;
						}
					}
				}
				else
				{
					// 等待notEmpty条件
					notEmpty_.wait(lock);
				}
				// 是否被析构唤醒线程池要结束，回收线程资源
				//if (!isRunning_)
				//{
				//	threads_.erase(threadId);
				//	std::cout << "tid: " << std::this_thread::get_id() << " exit!" << std::endl;
				//	exitCond_.notify_all();
				//	return;
				//}
			}

			idleThreadSize_--;
			std::cout << "tid: " << std::this_thread::get_id()
				<< " 获取任务成功..." << std::endl;

			//从任务队列获取一个任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;
			// 如果依然有剩余任务，继续通知其他线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}
			// 通知可以继续生产线程生产任务
			notFull_.notify_all();
		}
		// 当前线程执行这个任务
		if (task != nullptr)
		{
			// 执行任务，把任务返回值setVal给到Result
			task->exec();
		}
		idleThreadSize_++;
		lastCallingTime = std::chrono::high_resolution_clock::now();
	}
}

// ------------------------------ Thread ------------------------------
int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{}

Thread::~Thread()
{}

void Thread::start()
{
	// 创建一个线程来执行线程函数
	std::thread t(func_, threadId_);
	t.detach(); // 设置分离线程，t作用域结束但是func_不能结束
}

int Thread::getThreadId() const
{
	return threadId_;
}

// ------------------------------ Task ------------------------------
Task::Task()
	: result_(nullptr)
{}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run()); // 这里发生多态调用
	}
}

void Task::setResult(Result* res)
{
	result_ = res;
}

// ------------------------------ Result ------------------------------
Result::Result(std::shared_ptr<Task> t, bool isValid)
	: task_(t)
	, isValid_(isValid)
{
	task_->setResult(this);
}

void Result::setVal(Any any)
{
	// 存储task的返回值
	this->any_ = std::move(any);
	sem_.post(); // 已经获取任务返回值
}

Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); // 任务没有执行完阻塞用户线程
	return std::move(any_);
}