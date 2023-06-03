#ifndef _THREADPOOL_H
#define _THREADPOOL_H
typedef struct ThreadPool ThreadPool;
//�����̳߳ز���ʼ��
ThreadPool* threadPoolCreate(int min, int max, int queueSize);

//�����̳߳�
int threadDestory(ThreadPool* pool);

//���̳߳��������
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

//��ȡ�̳߳��й������̵߳ĸ���
int threadPoolBusyNum(ThreadPool* pool);

//��ȡ�̳߳��д����̵߳ĸ���
int threadPoolLiveNum(ThreadPool* pool);


//�������̣߳��������̣߳���������
void worker(void* arg);

//�������̵߳�������
void manager(void* arg);

//�����̵߳��Ƴ�
void threadExit(ThreadPool* pool);

#endif  // _THREADPOOL_H