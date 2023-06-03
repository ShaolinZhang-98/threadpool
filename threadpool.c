#include "threadpool.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
const int NUMBER= 2;
//����ṹ��
typedef struct Task
{
	void (*function)(void* arg);
	void* arg;
}Task;

//�̳߳ؽṹ��
struct ThreadPool
{
	//�������
	Task* taskQ;
	int queueCapacity;	//����
	int queueSize;		//��ǰ�������
	int queueFront;		//��ͷ -> ȡ����
	int queueRear;		//��β -> ������

	pthread_t managerID;	//	�������߳�ID
	pthread_t* threadIDs;	// �������߳�ID
	int minNum;				//��С�߳�����
	int maxNum;				//����߳�����
	int busyNum;			//æ���̵߳ĸ���
	int liveNum;			//�����̵߳ĸ���
	int exitNum;			//Ҫ���ٵ��̵߳ĸ���

	pthread_mutex_t mutexPool;	//���������̳߳�
	pthread_mutex_t mutexBusy;	//��busyNum����
	pthread_cond_t notFull;		//��������ǲ�������
	pthread_cond_t notEmpty;	//��������ǲ��ǿ���

	int shutdown;		//�ǲ���Ҫ�����̳߳أ�����Ϊ1��������Ϊ0
};

//�̳߳صĴ���
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
		//�߳�id��ʼ��Ϊ0����ʾ���idΪ0��û�д����߳�
		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);
		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;
		pool->exitNum = 0;
		pool->shutdown = 0;

		//��ʼ�������������������������ʼ��ʧ�ܣ�����ֵ��Ϊ0�����������־
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 || pthread_mutex_init(&pool->mutexBusy, NULL) != 0
			|| pthread_cond_init(&pool->notEmpty, NULL) != 0 || pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or condition init fail...\n");
			break;
		}

		//������ʼ���������
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		//�����������߳�
		pthread_create(&pool->managerID, NULL, manager, pool);

		//���������ߣ������ߣ��߳�
		for (int i = 0; i < min; ++i) {
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}

		//���ش������̳߳�
		return pool;
	} while (0);

	//�������̳߳�/�������߳�/�������ʧ��ʱ��ǰ���Ѿ������ɹ�malloc���ڴ��ͷ�
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

	//���̳߳��е�shutdown��Ϊ1
	pool->shutdown = 1;

	//�رչ������߳�
	pthread_join(pool->managerID, NULL);
	//�����������������߳�
	for (int i = 0; i < pool->liveNum; i++) {
		pthread_cond_signal(&pool->notEmpty);
	}
	//�ͷŶ��ڴ�
	if (pool->taskQ) {
		free(pool->taskQ);
	}
	if (pool->threadIDs) {
		free(pool->threadIDs);
	}

	//���ٻ���������������
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
		//��������������ˣ���Ҫ���������߳�
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown ) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//�����������񣺽�����ָ�븳ֵ
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
			//��ǰ�������Ϊ�գ������̳߳�û�йر�
			//������������notEmpty�ͻ�����mutexPool�������߳�
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);
			//�������߳̽���֮����Ҫ�ж��Ƿ�Ҫ��������
			if (pool->exitNum > 0) {
				pool->exitNum--;
				if (pool->liveNum > pool->minNum) {
					//��������̴߳�����С�߳����������߳�����
					pool->liveNum--;
					//����
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);
				}
			}
		}

		//�ж��̳߳��Ƿ񱻹ر���
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}

		//�������߳̿�ʼ�����������������ִ������
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		//�ƶ�ͷ���
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		//�̷߳��������񣬽����߳�
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);

		//�߳̿�ʼִ������
		printf("thread %ld start working...\n", pthread_self());
		//�ı�æ�̵߳���������Ҫ�õ�������
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		//�߳�ִ������,��ִ��������������ڴ�
		task.function(task.arg);
		free(task.arg);
		task.arg = NULL;
		//����ִ����ϣ��޸�busyNum
		printf("thread %ld end working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);
	}
	return NULL;
}

//�������̵߳�������
//1.�ж��̳߳��Ƿ�رգ�δ�رգ���ÿ3����һ�µ�ǰ�����̣߳������̣߳���������еĹ�ϵ
//2.���ݴ����̣߳������̣߳���������У����е��ȣ������̻߳��������̣߳�
void manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown) {
		//ÿ��3����һ��
		sleep(3);

		//ȡ���̳߳��е����������������̣߳���æ�̵߳�����
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//�������������̵߳���
		//�����̣߳��������>����̸߳���&& �����߳���<�����߳���
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

		//�����߳�:æ���߳�*2<�����߳�&&����߳�>��С�߳���
		if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			//ͨ��������������notEmpty���ù����߳��Զ�����
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