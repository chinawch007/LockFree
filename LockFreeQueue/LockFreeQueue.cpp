/*
    除了初始状态外,不允许在push后新tail大于等于head,在这种条件的约束下队列buffer中会留有小空隙;pop时,在head和tail相等的情况下返回空

    突然想到的,你刚改完tail,还没写完数据,其他线程来pop了该怎么办?---没想到这个问题,说明还是对数据复制和tail增加的原子性的必要性没理解号

    链表来处理写覆盖读是行不同的,除非你的链表也是个多线程公共结构,且存在同步问题
    用控制绕后之后的tail同head距离的方式可以应对写覆盖读,但没办法应对读覆盖写,毕竟你不能让用户不读数据...
    缓冲区尾部空白的问题,关键是读写间要协调好,这里以能否容得下8字节为准,就是说你得能够确定writetoken字段和长度字段从哪里找

    多进程测试---单进程可以用个单例,多进程的同步创建怎么搞啊---shmget可以看成是原子的,故而这是os的问题
    gperf测试---没啥搞头

    核心矛盾在于,head的增加,表示获取了读的权力,但此时没有读完的情况下,写线程的tail可以占有这部分
    ---当前读的时候会看是否写完,相应的,我们写的时候也应该看是否读完

    //头部添加个标识共享内存是否已经初始化的变量
*/
#include "LockFreeQueue.h"

#include <sys/shm.h>
#include <string.h>
#include <iostream>
#include <string>
#include <google/profiler.h>

using namespace std;

#define ATOMIC_INT_SIZE (sizeof(atomic<int>))
#define DATA (buffer + 4 * ATOMIC_INT_SIZE)
#define DATA_SIZE (bufferSize - 4 * ATOMIC_INT_SIZE)

#define WRITE_OVER_TOKEN (0xFFFFFFFF)
#define INITED (0xFFFFFFFF)

int LockFreeQueue::init(int bufferSize)
{
    this->bufferSize = bufferSize;
    shmId = shmget((key_t)0x12345678, bufferSize, IPC_CREAT|0777);
    if(shmId <= 0)
    {
        cout << "shmget error:" << errno << endl;
        return -1;
    }

    buffer = shmat(shmId, NULL, 0);
    if(buffer == NULL)
    {
        cout << "shmat error" << endl;
        return -1;
    }

    inited = (atomic<int>*)buffer;
    int initedValue = inited->load();
    cout << "init:" << initedValue << endl;
    if(initedValue == 0)
    {
        bool b = atomic_compare_exchange_strong(inited, &initedValue, 1);
        if(b)
        {
            cout << "get init lock success" << endl;

            head = new (buffer + ATOMIC_INT_SIZE) atomic<int>;
            tail = new (buffer + 2*ATOMIC_INT_SIZE) atomic<int>;
            headRead = new (buffer + 3*ATOMIC_INT_SIZE) atomic<int>;

            head->store(0);
            tail->store(0);
            headRead->store(0);
        }
    }
    else
    {
        head = (atomic<int>*)(buffer + ATOMIC_INT_SIZE);
        tail = (atomic<int>*)(buffer + 2*ATOMIC_INT_SIZE);
        headRead = (atomic<int>*)(buffer + 3*ATOMIC_INT_SIZE);
    }

    //memset(DATA, 0, DATA_SIZE);

    return 0;
}

int LockFreeQueue::push(void* data, int len)
{
    //ProfilerStart("tmp");

    //问题来了,核心是要把数据的复制和tail的修改打包原子地完成
    //占位子,把tail处的空间抢到手,就能进行copy
    //int tailValue, tailValueNew, headValue;
    int tailValue, tailValueNew, headReadValue;
    int casTimes = 0;//查看冲突剧烈程度

    do
    {
        ++casTimes;

        tailValue = tail->load();
        //headValue = head->load();
        headReadValue = headRead->load();//相对head必然滞后,用它判断即可

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

        //缓冲区满了,记住条件判断
        if( 
            ( 
                (tailValue < headReadValue) ||
                (tailValue > headReadValue && tailValue + 8 + len > DATA_SIZE) 
            )
            && tailValueNew >= headReadValue
         )
        {
            cout << "push:" << "buffer is full:" << tailValue << "|" << tailValueNew << "|" << headReadValue << endl;
            return -1;
            //continue;
        }
        //cout << "after continue" << endl;

    }
    while(!atomic_compare_exchange_strong(tail, &tailValue, tailValueNew));
    
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

    //ProfilerStop();
    
    return 0;
}

int LockFreeQueue::pop(void* data, int& len)
{
    int headValue, headValueNew, tailValue;
    int headReadValue, headReadValueNew;
    do
    {
        headValue = head->load();
        tailValue = tail->load();

        if(headValue == tailValue)
        {
            cout << "pop:headValue equal to tailValue, empty:" << headValue << endl;
            return -1;
        }

        if(headValue + 8 > DATA_SIZE)
        {
            //返回还是等?
            if( *(int*)DATA != WRITE_OVER_TOKEN )
            {
                cout << "pop:" << "write is not over:" << headValue << endl;
                return -1;
                //continue;
            }

            len = *(int*)(DATA + 4);
            headValueNew = 8 + len;
        }
        else
        {
            if( *(int*)(DATA + headValue) != WRITE_OVER_TOKEN )
            {
                cout << "pop:" << "write is not over:" << headValue << endl;
                return -1;
                //continue;
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

    headReadValue = headRead->load();
    headReadValueNew = headValueNew;

    while(!atomic_compare_exchange_strong(headRead, &headReadValue, headReadValueNew ));

    //cout << "pop:" << headValue << "|" << headValueNew << "|" << string((char*)data, len) << endl;

    return 0;
}
