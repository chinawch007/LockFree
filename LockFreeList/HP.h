#ifndef HP_H
#define HP_H

/*
    看看给外界提供的是什么样的接口
    insert时,对before->next进行cas,要保证before被其他线程删除但内存不能被回收

    我要搞个hash表,又不能用锁,无锁的就鸡生蛋蛋生鸡了...
    线程注册的时候也得有锁
    如果待回收结构统一管理的话,多线程又得加锁了

    我这边的内存回收是直接free还是怎么搞?

    这个hp能否解决aba问题
*/

//不用模板类,待回收的可认为都是指针

#include <atomic>
#include <cstddef>

using namespace std;

//这个值设为全局的好些还是只有单个线程的
//---是啊，后来想的时候，这个线程同步自增的啊
static __thread int cntFree = 0;

template<class T>
class HP
{
    public:

    //共n个线程,每个线程最多正在持有m个不可回收结构,待回收列表长度最长l
    int init(int n, int m, int l = 1)
    {
        this->n = n;
        this->m = m;
        this->l = l;

        ref = new void**[n];
        for(int i = 0; i < n; ++i)
        {
            ref[i] = new void*[m];

            for(int j = 0; j < m; ++j)
                ref[i][j] = NULL;
        }

        freeList = new void**[n];
        for(int i = 0; i < n; ++i)
        {
            freeList[i] = new void*[l];

            for(int j = 0; j < l; ++j)
                freeList[i][j] = NULL;
        }
        
    };

    int getThreadId()
    {
        static atomic<int> threadCount;
        static __thread int threadId = -1;//这个语句是每个线程只会执行一次?

        if(threadId != -1)return threadId;

        //不能直接原子加,不然线程切走了别人也加了一下就完了
        int tCount, expect;
        do
        {
            //注意回头看下这地方要不要添加AR
            tCount = threadCount.load();
            expect = tCount + 1;
        }
        //这参数直接引用吗?
        while(!threadCount.compare_exchange_strong(tCount, expect));

        threadId = expect - 1;

        return threadId;
    };


    //如果我们不能自己负责线程号的对应,那么就得让外部传进来线程号
    int inRef(int i, void* p)
    {
        int tid = getThreadId();

        ref[tid][i] = p;
    };

    //想了下这地方应该不涉及回收,回收是说我显式删除一个节点后,延迟free
    int outRef(int i)
    {
        int tid = getThreadId();

        ref[tid][i] = NULL;
    };

    //至少在我们的这个场景下,同一个节点不会挂在两个线程的待回收列表中***
    //---出现多线程都要删除的话,我控制不了了,只能上层处理
    //在我们这个场景下,能调用这个函数的自然就是真正执行删除节点的线程
    //---此步骤我只是存起来,并不涉及对某个元素判断它是否被应用二要进行删除
    //这个函数是HP功能的主体,ref啥的只是为了提醒使用者,有些空间不能回收,为这个函数做辅助,这个函数操作的是freeList
    int del(void* p)
    {
        int tid = getThreadId();

        cout << "thread id:" << tid  << " del " << p << endl;

/*
        for(int i = 0; i < l; ++i)
        {
            cout << "freelist " << i << ":" << freeList[tid][i] << endl;
        }
*/

        for(int i = 0; i < l; ++i)
        {
            if(freeList[tid][i] == NULL)
            {
                freeList[tid][i] = p;
                ++cntFree;
                break;
            }
        }

/*
        for(int i = 0; i < l; ++i)
        {
            cout << "freelist " << i << ":" << freeList[tid][i] << endl;
        }
*/

        if(cntFree >= l)
        {
            gc();
        }
    }

    //可不可能我正检查的时候没发现某个元素,结果要回收的时候,它来了...
    //这个地方其实是单线程的
    int gc()
    {

        cout << "gc" << endl;
    
        int tid = getThreadId();

        //对于我负责删除的每一个指针
        for(int i = 0; i < l; ++i)
        {
            if(!freeList[tid][i])continue;

            bool inUse = false;

            //查看所有线程是否还有使用该指针内容的
            //有没有同步的问题,毕竟是别的线程存的东西
            //---比如我刚检查完你就ref了一下
            //---在流程上要把控好,什么意思,就是说del之后不允许再ref!!!!!!
            for(int j = 0; j < n; ++j)
            {
                for(int k = 0; k < m; ++k)
                {
                    if(ref[j][k] == freeList[tid][i])
                    {
                        cout << "pointer is in use:" << ref[j][k] << endl;
                        inUse = true;
                        break;
                    }
                }
                if(inUse)break;
            }

            //cout << i << ":" << freeList[tid][i] << endl;

            if(!inUse)
            {
                //delete void*,不是类的普通结构没问题,类的话不会调用析构函数
                //这地方看下是不是要由我来进行内存回收---可以暂时这么搞
                //指针没类型会编译错误啊
                //cout << "delete pointer" << freeList[tid][i] << endl;
                //这地方不能直接delete掉,你这层删的是list的node,里边还包了一层cache的node,然后里边指针指向实际的页内容
                //---用这个指针,调用上层的释放函数,层层释放
                T* tmp = (T*)(freeList[tid][i]);
                cout << "hp thread " << tid << " clear:" << (tmp->t).k << endl;
                tmp->clear();
                //这地方注意下,究竟怎么才能delete掉内存
                //---一个new出来的类能够delete自己吗?
                //delete (T*)freeList[tid][i];
                freeList[tid][i] = NULL;
                cntFree--;
            }
        }
    };

    //搞成tls的话,只能用于一个调用方了吧
    void*** ref;
    void*** freeList;
    int n, m, l;

};

#endif