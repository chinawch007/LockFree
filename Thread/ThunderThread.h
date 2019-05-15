#include<pthread.h>
#include<iostream>

using namespace std;

/*
目标：
1、整理线程的使用
2、线程间同步的问题
3、gperf测性能
4、类的封装使用
*/

class ThunderThread
{
    public:
        void waitOver()
        {
            pthread_join(tid, NULL);
        };

        //你依然需要把函数指针传进来,人家都是直接继承的--评审下设计目标
        void init(void* func(void*), void* arg)
        {
            this->func = func;
            this->arg = arg;
            canStart = false;
            pthread_mutex_init(&mutex,NULL);
            pthread_cond_init(&cond, NULL);

            //然后我应该开始跑,并且阻塞在了我自己的锁上
            //如果我用的是个类的函数,类型是限定位ThunderThread中的,类型不符
            pthread_create(&tid, NULL, &prepare, this);

            //其实在你的主调线程正式调用start之前,create就已经开始跑了,但是你只能这样
        };

        //而static函数只能使用static成员----于是用了一个旁支的手段,
        //static void* prepare(void* arg)
        static void* prepare(void* thread)
        {
            /*
            pthread_mutex_lock(&mutex);

            while(!canStart)pthread_cond_wait(&cond, &mutex);

            pthread_mutex_unlock(&mutex);
            */

            cout << "prepare" << endl;

            ((ThunderThread*)thread)->innerPrepare();

        };

        void innerPrepare()
        {
            cout << "innerPrepare" << endl;
/*
            pthread_mutex_lock(&mutex);

            while(!canStart)pthread_cond_wait(&cond, &mutex);

            pthread_mutex_unlock(&mutex);
*/
            cout << "start func" << endl;

            //if(func == NULL){cout << "func empty" << endl;}
            func(arg);
        };

        int start()
        {
            pthread_mutex_lock(&mutex);
            canStart =true;
            pthread_mutex_unlock(&mutex);

            pthread_cond_signal(&cond);
        };

        //static void TreadEntry(ThunderThread* thread);


/*
        int lock();

        int wait();

        int signal();
*/
    private:
        pthread_t tid;
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        
        void* (*func)(void *);
        void* arg;

        bool canStart;
};