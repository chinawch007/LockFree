#include <iostream>
#include <thread>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#include "LockFreeList.h"
#include "LockCache.h"

using namespace std;

//#define TEST_INSERT
//#define TEST_INSERT_AND_REMOVE
//#define TEST_REF_AND_RECLAIM
#define TEST_CLOCK_CACHE_GET

#define SetSize 1000

class TestT
{
    public:
        unsigned long k;
        unsigned long key()
        {   
            return k;
        };

        void clear()
        {
            cout << dec << "clear:" << k << endl;
        };

        void writeBack()
        {
            cout << dec << "write back:" << k << endl;
        };
};

class TestSet
{
    public:
        TestSet(int size = SetSize)
        {
            for(int i = 0; i < size; ++i)
            {
                v.push_back(TestT());
                v[i].k = i;
            }
        };

        void getItem(unsigned long i, TestT* pt, unsigned long* pk)
        {
            *pt = v[i];
            *pk = i;
        };

        vector<TestT> v;
};

LockFreeList<TestT> list;
typedef LockFreeList<TestT>::Node Node;
ClockCache<TestT, TestSet> cc;
atomic<int> b[1000];


/*
HashTable<TestT> table(8);

void func(int j)
{
    static int t = 0;
    for(int i = 0; i < 2; ++i)
    {
        typename LockFreeList<TestT>::Node *n = new (typename LockFreeList<TestT>::Node);
        (n->t).k = ++t;
        n->i = t;
        list.insert(n, &(list.dummyHead));
    }
}

void funcHash()
{
    TestT t;
    t.k = 10;
    table.insert(t);
    table.remove(10);

    cout << table.find(10) << "|" << table.find(5) << endl;
}

void processFunc()
{
    thread t1(func, 1);
    thread t2(func, 2);

    t1.join();
    t2.join();

    thread t3(funcHash);

    t3.join();
}
*/

/*
void *p1, *p2;

void funcTestHp(HP<int>& hp)
{
    cout << "ThreadId:" << hp.getThreadId() << endl;
    hp.inRef(0, p1);

}

void processTestHP(HP<int>& hp)
{
    thread t(funcTestHp, std::ref(hp) );

    t.join();
}
*/


#ifdef TEST_INSERT

void funcTestInsert(int u)
{
    typedef LockFreeList<TestT>::Node Node;
    for(int i = 0; i < u; ++i)
    {
        //cout << "before insert, iter:" << i << endl;

        TestT t;
        t.k = i;

        Node* n = new Node;
        n->t = t;

        int ret = list.insert(n);

        //cout << "thread " << (list.hp)->getThreadId() << " iter "<< i << " insert ret:" << ret << endl;
    }
    
}

//症状:一个线程插入没问题,两个线程插入,但第二个线程插入的值跟第一个都是重复的,所以插入都应该是失败的,链表的值确实变化的.
//这个流程中其实没有走到insert的,锅应该是find的,但我总是不确定,直到只调find,不调insert,才发现问题
//倒确实没想到,incRef造成的问题,修改next有bug,指向了一个未知地址

//症状:第一个线程插入0,1;第二个线程插入0,1 core---先插0后插1有问题,只插1倒没问题
//---估计还是ref的问题,两次操作出了问题,试验下两次find---确定是第二个find出的问题
void processTestInsert()
{
    thread t1(funcTestInsert, 1000);
    thread t2(funcTestInsert, 1000);
    thread t3(funcTestInsert, 1000);
    thread t4(funcTestInsert, 1000);

    t1.join();
    t2.join();
    t3.join();
    t4.join();

    list.print();
}
#endif

#ifdef TEST_INSERT_AND_REMOVE

void funcTestInsert(int s, int e)
{
    typedef LockFreeList<TestT>::Node Node;
    for(int i = s; i < e; ++i)
    {
        TestT t;
        t.k = i;

        Node* n = new Node;
        n->t = t;
        int ret = list.insert(n);
        //cout << "thread " << (list.hp)->getThreadId() << " iter "<< i << " insert ret:" << ret  << endl;
    }
    
}

void funcTestRemove(int s, int e)
{
    typedef LockFreeList<TestT>::Node Node;
    for(int i = s; i < e; ++i)
    {
        int ret = list.remove(i);
        //cout << "thread " << (list.hp)->getThreadId() << " iter "<< i << " remove ret:" << ret << endl;
    }
}

//症状:先插入0,1;再删除0,1;插入成功了,删除却进入死循环了---确定是卡在remove中了,但在函数开始处放return都没法结束循环?
//---删除0时,remove返回-5,然后没有删除1的日志,不动了---有引用计数就删除失败,之前就说过,这地方要加个bool选项
//---删除首先会无效化节点,remove失败后会重试,而之后的find遇到无效节点会重试,无限...
//---修改上述错误,删除1,无限循环,remove返回-2---在只删除0的情况下,发现remove中cas的目标值,最低位bit被设置1?表示before被无效化了?
//---no,因为我刚刚把目标节点无效化了,取next地址时就把这个bit给带上了
void processTestInsertAndRemove()
{
    thread t1(funcTestInsert, 0, 2000);
    thread t11(funcTestInsert, 2000, 4000);

    thread t2(funcTestRemove, 0, 2000);
    thread t22(funcTestRemove, 2000, 4000);

    t11.join();
    t1.join();
    t2.join();
    t22.join();

    list.print();
}

#endif

#ifdef TEST_REF_AND_RECLAIM

void funcTestReclaim()
{

    std::this_thread::sleep_for(std::chrono::seconds(1));

    list.reclaim();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    list.reclaim();

}

void funcRef()
{
    for(int i = 0; i < 10000; ++i)
    {
        Node* node;
        Node* pre;

        list.find( (unsigned long)(rand())%10, &node, &pre);

        list.incRef(node);
    }
}

void funcUnref()
{
    for(int i = 0; i < 10000; ++i)
    {
        Node* node;
        Node* pre;
        list.find( (unsigned long)(rand())%10, &node, &pre);

        list.decRef(node);
    }
}

void funcUnreflru()
{
    for(int i = 0; i < 20000; ++i)
    {
        Node* node;
        Node* pre;
        list.find( (unsigned long)(rand())%10, &node, &pre);

        list.decLruRef(node);
    }
}

void processTestRefAndReclaim()
{
    HP<Node> hp;
    hp.init(4, 10, 10);
    list.init(&hp);

    for(int i = 0; i < 10; ++i)
    {
        Node* n = new Node;

        TestT tt;
        tt.k = i;
        n->t = tt;

        list.insert(n);
    }

    thread t1(funcRef);
    thread t2(funcUnref);
    thread t4(funcUnreflru);
    thread t3(funcTestReclaim);


    t1.join();
    t2.join();
    t4.join();
    t3.join();

    list.print();
}

#endif

#ifdef TEST_CLOCK_CACHE_GET

void funcGet(int s, int n, int seed)
{
    srand(seed);
    while(1)
    {
        
        int i = s + rand()%n;
        //cout << "thread "<< cc.table.table[0].hp->getThreadId() << ", i:" << i << endl;
        if(b[i].load() == 0)
            {
                int tmp = b[i].load();
                if(tmp != 0)continue;
                bool ret = b[i].compare_exchange_strong(tmp, 1);
                if(!ret)continue;

                cout << "thread "<< cc.table.table[0].hp->getThreadId() << " get " << i << endl;
                cc.get(i);
                cout << "incr " << i << endl;
                (cc.table.table)[0].print();
            }
        else if(b[i].load() == 1)
            {
                int tmp = b[i].load();
                if(tmp != 1)continue;
                bool ret = b[i].compare_exchange_strong(tmp, 2); 
                if(!ret)continue;

                cout << "thread "<< cc.table.table[0].hp->getThreadId() << " release " << i << endl;
                cc.release(i);
                cout << "dec " << i << endl;
                (cc.table.table)[0].print();
            }  
    }
}

void funcPrint()
{
    int i = 0;
    while(1)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        cout << "times:" << i++ << " reclaim" << endl;
        cc.reclaim();
        //cc.table.print();
        cout << endl;
        cout << endl;
        cout << endl;
    }
}

void processTestClockCache()
{   
    for(int i = 0; i < 1000; ++i)
    {
        b[i].store(0);
    }

    thread t1(funcGet, 0, 1000, 1);
    thread t2(funcGet, 0, 1000, 2);
    //thread t3(funcPrint);

    t1.join();
    t2.join();
    //t3.join();

}

#endif

int main()
{
    typedef LockFreeList<TestT>::Node Node;

    /*
    processFunc();
    list.print();
    LockFreeList<TestT>::Node* tmp;
    cout << list.find(4, &tmp) << "|" << list.find(5, &tmp) << endl;
    */

    /*
    p1 = (void*)(new int);
    p2 = (void*)(new int);

    HP<int> hp;
    hp.init(2, 2, 2);
    processTestHP(hp);
    hp.del(p1);
    hp.del(p2);
    */


    #ifdef TEST_INSERT
    HP<Node> hp;
    hp.init(4, 1000, 2);
    list.init(&hp);
    processTestInsert();
    #endif

    #ifdef TEST_INSERT_AND_REMOVE
    HP<Node> hp;
    hp.init(4, 4000, 200);
    list.init(&hp);
    processTestInsertAndRemove();

    #endif

    #ifdef TEST_REF_AND_RECLAIM

    processTestRefAndReclaim();

    #endif

    #ifdef TEST_CLOCK_CACHE_GET

    processTestClockCache();
    //funcGet(0, 1, 1);

    #endif

    return 0;
}



