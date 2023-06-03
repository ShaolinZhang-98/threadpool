#include "threadpool.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
const int NUMBER= 2;
//任务结构体
typedef struct Task
{
	void (*function)(void* arg);
	void* arg;
}Task;

//线程池结构体
struct ThreadPool
{
	//任务队列
	Task* taskQ;
	int queueCapacity;	//容量
	int queueSize;		//当前任务个数
	int queueFront;		//队头 -> 取数据
	int queueRear;		//队尾 -> 放数据

	pthread_t managerID;	//	管理者线程ID
	pthread_t* threadIDs;	// 工作的线程ID
	int minNum;				//最小线程数量
	int maxNum;				//最大线程数量
	int busyNum;			//忙的线程的个数
	int liveNum;			//存活的线程的个数
	int exitNum;			//要销毁的线程的个数

	pthread_mutex_t mutexPool;	//锁整个的线程池
	pthread_mutex_t mutexBusy;	//锁busyNum变量
	pthread_cond_t notFull;		//任务队列是不是满了
	pthread_cond_t notEmpty;	//任务队列是不是空了

	int shutdown;		//是不是要销毁线程池，销毁为1，不销毁为0
};

//线程池的创建
ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do{
		if (!pool) {
			printf("malloc threadpool fail...\n");
			break;
		}
		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->threadIDs == NULL) {
			printf("malloc threadIDs fail...\n");
			break;
		}
		//线程id初始化为0，表示如果id为0则还没有创建线程
		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);
		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;
		pool->exitNum = 0;
		pool->shutdown = 0;

		//初始化互斥锁和条件变量，如果初始化失败，返回值则不为0，输出报错日志
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 || pthread_mutex_init(&pool->mutexBusy, NULL) != 0
			|| pthread_cond_init(&pool->notEmpty, NULL) != 0 || pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or condition init fail...\n");
			break;
		}

		//创建初始化任务队列
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		//创建管理者线程
		pthread_create(&pool->managerID, NULL, manager, pool);

		//创建工作者（消费者）线程
		for (int i = 0; i < min; ++i) {
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}

		//返回创建的线程池
		return pool;
	} while (0);

	//当创建线程池/消费者线程/任务队列失败时将前面已经创建成功malloc的内存释放
	if (pool && pool->threadIDs) {
		free(pool->threadIDs);
	}
	if (pool && pool->taskQ) free(pool->taskQ);
	if (pool) free(pool);
	return NULL;
}

int threadDestory(ThreadPool* pool)
{
	if (pool == NULL) return -1;

	//将线程池中的shutdown设为1
	pool->shutdown = 1;

	//关闭管理者线程
	pthread_join(pool->managerID, NULL);
	//唤醒阻塞的消费者线程
	for (int i = 0; i < pool->liveNum; i++) {
		pthread_cond_signal(&pool->notEmpty);
	}
	//释放堆内存
	if (pool->taskQ) {
		free(pool->taskQ);
	}
	if (pool->threadIDs) {
		free(pool->threadIDs);
	}

	//销毁互斥锁和条件变量
	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);
	free(pool);
	pool = NULL;
	return 0;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg)
{
	pthread_mutex_lock(&pool->mutexPool);
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown) {
		//表明任务队列满了，需要阻塞生产线程
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown ) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//正常添加任务：将函数指针赋值
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;

	pthread_cond_signal(&pool->notEmpty);
	pthread_mutex_unlock(&pool->mutexPool);

}

int threadPoolBusyNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

int threadPoolLiveNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexPool);
	int aliveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return aliveNum;
}

void worker(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (1) {
		pthread_mutex_lock(&pool->mutexPool);
		while (pool->queueSize==0&&!pool->shutdown) {
			//当前任务队列为空，但是线程池没有关闭
			//利用条件变量notEmpty和互斥锁mutexPool来阻塞线程
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);
			//阻塞的线程解锁之后，需要判断是否要销毁自身
			if (pool->exitNum > 0) {
				pool->exitNum--;
				if (pool->liveNum > pool->minNum) {
					//如果存活的线程大于最小线程数，进行线程销毁
					pool->liveNum--;
					//解锁
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);
				}
			}
		}

		//判断线程池是否被关闭了
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}

		//消费者线程开始工作，从任务队列中执行任务
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		//移动头结点
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		//线程分配完任务，解锁线程
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);

		//线程开始执行任务
		printf("thread %ld start working...\n", pthread_self());
		//改变忙线程的数量，需要用到互斥锁
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		//线程执行任务,并执行完后回收任务的内存
		task.function(task.arg);
		free(task.arg);
		task.arg = NULL;
		//任务执行完毕，修改busyNum
		printf("thread %ld end working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);
	}
	return NULL;
}

//管理者线程的任务函数
//1.判断线程池是否关闭，未关闭，则每3秒检测一下当前存在线程，工作线程，与任务队列的关系
//2.根据存在线程，工作线程，与任务队列，进行调度（创建线程或者销毁线程）
void manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown) {
		//每个3秒检测一次
		sleep(3);

		//取出线程池中的任务数量，存在线程，在忙线程的数量
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//根据数量进行线程调度
		//添加线程：任务个数>存活线程个数&& 存活的线程数<最大的线程数
		if (queueSize > liveNum && liveNum < pool->maxNum) {
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			for (int i = 0; i < pool->maxNum && counter < NUMBER
				&& pool->liveNum < pool->maxNum; i++) {
				if (pool->threadIDs[i] == 0) {
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&pool->mutexPool);
		}

		//销毁线程:忙的线程*2<存活的线程&&存活线程>最小线程数
		if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			//通过解锁条件变量notEmpty来让工作线程自动销毁
			for (int i = 0; i < NUMBER; i++) {
				pthread_cond_signal(&pool->notEmpty);
			}
		}
	}
	return NULL;
}

void threadExit(ThreadPool* pool)
{
	pthread_t tid = pthread_self();
	for (int i = 0; i < pool->maxNum; i++) {
		if (pool->threadIDs[i] == tid) {
			pool->threadIDs[i] = 0;
			printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);
}
