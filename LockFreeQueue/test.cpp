#include <iostream>
#include <thread>
#include <sys/time.h>
#include <unistd.h>

#include "LockFreeQueue.h"

//把数调大了特别忐忑

using namespace std;

void pushFunc()
{
    char bufIn[] = "wangchongaaa";

    LockFreeQueue q;
    q.init(2000000 + 8);

    struct timeval tv1, tv2;
	gettimeofday(&tv1, NULL);

    for(int i = 0; i < 10000; ++i)
    {
        q.push(bufIn, 12);
    }

    gettimeofday(&tv2, NULL);

    cout << "push cost:" << (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec) << endl;
}

void popFunc()
{
    char bufOut[20];
    int len;

    LockFreeQueue q;
    q.init(2000000 + 8);

    struct timeval tv1, tv2;
	gettimeofday(&tv1, NULL);

    for(int i = 0; i < 10000; ++i)
    {
        q.pop(bufOut,len);
    }

    gettimeofday(&tv2, NULL);

    cout << "pop cost:" << (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec) << endl;
}

void processFunc()
{
    thread t1(pushFunc);
    thread t2(popFunc);

    t1.join();
    t2.join();
}

int main()
{
    for(int i = 0; i < 2; ++i)
    {
        pid_t pid = fork();

        if(pid == 0)
        {
            processFunc();
            break;//不这样的话,会执行第二个循环吧
        }
        else if (pid > 0)
        {
            cout << "pid:" << pid << endl;
        }
        else
        {
            cout << "fork error" << errno << endl;
        }
    }

    return 0;
}