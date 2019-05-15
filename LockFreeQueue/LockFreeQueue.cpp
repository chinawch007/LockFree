/*
    如果一个原子变量都不在多线程中出现,当然就不必发愁memory barrier的问题
    在只有head和tail两个位置指针的情况下如何判定队列满是个问题
    搞个标识是否满的变量,但没法跟tail,head一起原子操作
    push:新tail位置等于head
    pop:新head位置等于tail

    除了初始状态外,不允许在push后新tail大于等于head,在这种条件的约束下队列buffer中会留有小空隙;pop时,在head和tail相等的情况下返回空

    突然想到的,你刚改完tail,还没写完数据,其他线程来pop了该怎么办?---没想到这个问题,说明还是对数据复制和tail增加的原子性的必要性没理解号

    链表来处理写覆盖读是行不同的,除非你的链表也是个多线程公共结构,且存在同步问题
    用控制绕后之后的tail同head距离的方式可以应对写覆盖读,但没办法应对读覆盖写,毕竟你不能让用户不读数据...
    缓冲区尾部空白的问题,关键是读写间要协调好,这里以能否容得下8字节为准,就是说你得能够确定writetoken字段和长度字段从哪里找

    多进程测试---单进程可以用个单例,多进程的同步创建怎么搞啊---shmget可以看成是原子的,故而这是os的问题
    gperf测试---没啥搞头
*/
#include "LockFreeQueue.h"

#include <sys/shm.h>
#include <string.h>
#include <iostream>
#include <string>
#include <google/profiler.h>

using namespace std;

#define ATOMIC_INT_SIZE (sizeof(atomic<int>))
#define DATA (buffer + 2 * ATOMIC_INT_SIZE)
#define DATA_SIZE (bufferSize - 2 * ATOMIC_INT_SIZE)

#define WRITE_OVER_TOKEN (0xFFFFFFFF)

int LockFreeQueue::init(int bufferSize)
{
    this->bufferSize = bufferSize;
    shmId = shmget((key_t)0x123465678, bufferSize, IPC_CREAT|0777);
    if(shmId <= 0)cout << "shmget error:" << errno << endl;
    buffer = shmat(shmId, NULL, 0);
    if(buffer == NULL)cout << "shmat error" << endl;

    head = new (buffer) atomic<int>;
    tail = new (buffer + ATOMIC_INT_SIZE) atomic<int>;

    //多进程的话这地方明显要改下
    head->store(0);
    tail->store(0);

    memset(DATA, 0, DATA_SIZE);

    return 0;
}

//别推满了
//也搞个断言看看有没有问题
int LockFreeQueue::push(void* data, int len)
{
    ProfilerStart("tmp");

    //问题来了,核心是要把数据的复制和tail的修改打包原子地完成---这句话总结的蛮不错的嘛
    //占位子,把tail处的空间抢到手,就能进行copy
    int tailValue, tailValueNew, headValue;
    int casTimes = 0;//查看冲突剧烈程度

    do
    {
        ++casTimes;

        //这个步骤可以放到循环外边,但那样cas成功率明显会变低
        tailValue = tail->load();
        headValue = head->load();

        if(tailValue + 8 > DATA_SIZE)
        {
            tailValueNew = 8 + len;
        }
        else if(tailValue + 8 + len > DATA_SIZE)
        {
            tailValueNew = len;
        }
        else
        {
            tailValueNew = tailValue + 8 + len;
        }

        //cout << tailValue << "|" << len << "|" << DATA_SIZE << "|" << tailValueNew << "|" << headValue << endl;

        //缓冲区满了---这个条件你写的这么简单,可见没有仔细思考
        //if(tailValueNew >= headValue)
        if( 
            ( 
                (tailValue < headValue) ||
                (tailValue > headValue && tailValue + 8 + len > DATA_SIZE) 
            )
            && tailValueNew >= headValue
         )
        {
            cout << "push:" << "buffer is full:" << tailValue << "|" << tailValueNew << "|" << headValue << endl;
            return -1;
        }

    }
    while(!atomic_compare_exchange_strong(tail, &tailValue, tailValueNew));

    //cout << "push:" << tailValue << "|" << tailValueNew << endl;
    
    //因为环形缓冲区的问题,所以长度字段确实得确定个大端或者是小端的标准
    //差点写错了,当然要用旧的tail值啊
    if(tailValue + 8 > DATA_SIZE)
    {
        *(int*)(DATA + 4) = len;
        memcpy(DATA + 8, data, len);
        *(int*)DATA = WRITE_OVER_TOKEN;
    }
    else if (tailValue + 8 + len > DATA_SIZE)
    {
        *(int*)(DATA + tailValue + 4) = len;
        memcpy(DATA, data, len);
        *(int*)(DATA + tailValue) = WRITE_OVER_TOKEN;
    }
    else
    {
        *(int*)(DATA + tailValue + 4) = len;
        memcpy((DATA + tailValue + 8), data, len);
        *(int*)(DATA + tailValue) = WRITE_OVER_TOKEN;
    }

    ProfilerStop();
    
    return 0;
}

int LockFreeQueue::pop(void* data, int& len)
{
    int headValue, headValueNew, tailValue;
    int casTimes = 0;

    do
    {
        ++casTimes;

        headValue = head->load();
        tailValue = tail->load();

        if(headValue == tailValue)
        {
            cout << "pop:headValue equal to tailValue, empty:" << headValue << endl;
            return -1;
        }

        if(headValue + 8 > DATA_SIZE)
        {
            if( *(int*)DATA != WRITE_OVER_TOKEN )
            {
                cout << "pop:" << "write is not over:" << headValue << endl;
                return -1;
            }

            len = *(int*)(DATA + 4);
            headValueNew = 8 + len;
        }
        //else if (headValue + 4 + len > DATA_SIZE)---这种问题是怎么出现的...
        else
        {
            if( *(int*)(DATA + headValue) != WRITE_OVER_TOKEN )
            {
                cout << "pop:" << "write is not over:" << headValue << endl;
                return -1;
            }

            len = *(int*)(DATA + headValue + 4);

            if (headValue + 4 + len > DATA_SIZE)
            {
                headValueNew = len;
            }
            else
            {
                headValueNew = headValue + 8 + len;
            }
        }

    }
    while(!atomic_compare_exchange_strong(head, &headValue, headValueNew ));

    if(headValue + 8 > DATA_SIZE)
    {
        memcpy(data, DATA + 8, len);
    }
    else if (headValue + 8 + len > DATA_SIZE)
    {

        memcpy(data, DATA, len);
    }
    else
    {
        memcpy(data, DATA + headValue + 8, len);
    }

    cout << "pop:" << headValue << "|" << headValueNew << "|" << string((char*)data, len) << endl;

    return 0;
}
