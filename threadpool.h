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

// �����������ݵ�����
class Any
{
public:
	Any() = default;
	~Any() = default;

	// ��Ա����unique_ptr��ֹ��ֵ��������
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	// ��Ա����unique_ptrĬ����ֵ��������
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data))
	{}

	template<typename T>
	T cast_()
	{
		// ��base���ҵ���ָ�������������ȡ��data
		// ����ָ��=>������ָ�� RTTI
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

	// setVal ��ȡ����ִ����ķ���ֵ
	void setVal(Any any);

	// get������ȡtask�ķ���ֵ
	Any get();

private:
	Any any_; // �洢����ķ���ֵ
	Semaphore sem_; // ����get������Ҫ
	std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_; // ����ֵ�Ƿ���Ч���Ƿ�����ɹ��ύ��
};

enum class PoolMode
{
	MODE_FIXED,
	MODE_CACHED
};

class Thread
{
public:
	// �̺߳�����������
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
	// cacheģʽ�������߳�����ֵ
	void setThreadSizeMaxThres(int thres);

	// ���߳�����������ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// �����̺߳���
	void threadFunc(int threadId);
	// ����̳߳�����״̬
	bool checkRunningState() const;

private:
	//std::vector<std::unique_ptr<Thread>> threads_; // �߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // �߳��б�
	int initThreadSize_;
	std::atomic_int currentThreadSize_;
	int threadSizeMaxThres_; // cacheģʽ����߳�����ֵ
	std::atomic_int idleThreadSize_; // �����߳�����

	std::queue<std::shared_ptr<Task>> taskQue_; // �������
	std::atomic_int taskSize_;
	int taskSizeMaxThres_; // ��������������

	std::mutex mtxTaskQue_;
	std::condition_variable notFull_;
	std::condition_variable notEmpty_;
	std::condition_variable exitCond_; // �ȴ��߳���Դȫ������

	PoolMode poolMode_;
	std::atomic_bool isRunning_; // �̳߳��Ƿ��Ѿ�����
};

// ���������࣬�û��ɼ̳���дrun����
class Task
{
public:
	Task();
	~Task() = default;

	void exec();

	void setResult(Result* res);

	virtual Any run() = 0;

private:
	// ������Ϊ����ָ��
	Result* result_; // Result�������ڳ���task
};


#endif