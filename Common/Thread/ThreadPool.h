#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

// This is the simplest possible worker implementation I can think of
// but entirely sufficient for the given purpose.
// Only handles a single item of work at a time.
class WorkerThread {
public:
	WorkerThread() = default;
	virtual ~WorkerThread();

	void StartUp();

	// submit a new work item
	void Process(std::function<void()> work);
	// wait for a submitted work item to be completed
	void WaitForCompletion();

protected:
	std::thread thread; // the worker thread
	std::condition_variable signal; // used to signal new work
	std::condition_variable done; // used to signal work completion
	std::mutex mutex, doneMutex; // associated with each respective condition variable
	bool active = true;
	int jobsDone = 0;
	int jobsTarget = 0;
private:
	virtual void WorkFunc();

	std::function<void()> work_; // the work to be done by this thread

	WorkerThread(const WorkerThread& other) = delete; // prevent copies
	void operator =(const WorkerThread &other) = delete;
};

class LoopWorkerThread final : public WorkerThread {
public:
	LoopWorkerThread() = default;
	void Process(std::function<void(int, int)> work, int start, int end);

private:
	virtual void WorkFunc() override;

	int start_;
	int end_;
	std::function<void(int, int)> work_; // the work to be done by this thread
};

// A thread pool manages a set of worker threads, and allows the execution of parallel loops on them
// individual parallel loops are fully sequentialized to simplify synchronization, which should not 
// be a problem as they should each use the entire system
class ThreadPool {
public:
	ThreadPool(int numThreads);
	// don't need a destructor, "workers" is cleared on delete, 
	// leading to the stopping and joining of all worker threads (RAII and all that)

	void ParallelLoop(const std::function<void(int,int)> &loop, int lower, int upper);

private:
	int numThreads_;
	std::vector<std::unique_ptr<LoopWorkerThread>> workers;
	std::mutex mutex; // used to sequentialize loop execution

	bool workersStarted = false;
	void StartWorkers();
	
	ThreadPool(const ThreadPool& other) = delete; // prevent copies
	void operator =(const ThreadPool &other) = delete;
};

