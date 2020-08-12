#include <atomic>
#include <iostream>
#include <unistd.h>
#include <cstring>
#include "LockFreeList.h"

using namespace std;

#define FILE_PAGE_NUM (1024)
#define HASH_TABLE_SIZE (1)
#define CACHE_SIZE_LIMIT (10 * 1024)
#define MAX_SIZE (20)

template<class T>
class HashTable
{
    public:
        //把参数类直接给list了,不加调整
        typedef typename LockFreeList<T>::Node Node;

        HashTable(int size)
        {
            this->size = size;
            table = new LockFreeList<T>[size];
            circle = 0;
            
            for(int i = 0; i < size; ++i)
            {
                HP<Node> *hp = new HP<Node>;
                hp->init(4, 1000, 2);
                table[i].init(hp);
            }
            
        };

        int hash(unsigned long k)
        {
            return k % size;
        };

        //可得记住了,cache功能下,原节点被链子删除了倒没啥,似乎需要hp来挂一下
        Node* find(unsigned long k, bool ref = false)
        {
            int i = hash(k);
            LockFreeList<T>& list = table[i];

            Node* pre = NULL;
            Node* n = NULL;

            int ret = list.find(k, &n, &pre, ref);

            if(ret == 1)return NULL;

            return n;
        };

        int insert(Node* n, bool ref = false)
        {
            int i = hash((n->t).key());
            LockFreeList<T>& list = table[i];

            int ret = list.insert(n, ref);

            itemSize++;
            circle++;
            if(circle >= 2)
            {
                ret = 1;
                circle = 0;
            }

            return ret;
        };

        int remove(unsigned long k)
        {
            int i = hash(k);
            LockFreeList<T>& list = table[i];

            return list.remove(k);
        };

        //这地方就深深地体现出哲学的矛盾之处,你这个功能放到cache层要处理HashTable的内部结构,放在这里却要使用cache层定义节点的成员函数
        //此处暂时是底层list唯一的删除场景了
        //超出一个限度值的时候调用,这个限度值要在我这里设置,不能在list中,毕竟他那边是单链
        int reclaim(int i)
        {
            LockFreeList<T>& list = table[i];

            //1者考虑引用计数,2者考虑lru计数
            //---你这层考虑引用计数那我list层就不必考虑了

            //这边引用计数只读,所以注意下AR就行了
            //---refLru要不要cas变化
            //---引用计数和lru计数如果不在一起原子操作的话,你拿到的引用计数为0的元素,等你要删除的时候可能引用计数已经不是0了
            //---一边是查看T信息,调用lsit删除;一边是写T信息,调用list查询...如何搞原子性,保证我删除了你不能看,我查看的你别删除
            //---失败场景1:查询引用计数可以删除,另外有个线程读了节点并加了计数,结果给删了
            //---失败场景2:
            //---主要冲突在于引用计数同删除节点间的冲突,所以引用计数功能要下沉了
            //---另外想想其实也没啥,放哪都一样
            //---要么就对回收和查询强加锁...

            list.reclaim();

            return 0;
        };

        void reclaim()
        {
            int i = 0;
            do
            {
                reclaim(i);
                i = (i + 1) % size;
            }
            while(i);
        }

        void print()
        {
            for(int i = 0; i < size; ++i)
            {
                table[i].print();
            }
        }

        LockFreeList<T>* table;
        int size;
        int itemSize;
        int circle;
};

//我发现有个根本性的问题我搞错了...你存储的是void*,这没问题,但你查询时候用的是文件内偏移,即一个key值,这个key值不是void*生成的
//多线程对cache这个整体管理Page的结构是多线程并发,但对于单个页的插入,写入是要做并发控制的---
//我要么是对文件中所有位置的页做bit索引----要么是自己管理Node结构,在初始化时直接申请好全部内存,比较下---
//你自己管理,多个线程插入一个页你怎么做调度呢?毕竟只能用pageNum来索引

//T表示元素,S标识元素的后备集合
//T要有WriteBack,S要有getItem
template<class T, class S>
class ClockCache
{
    public:
        //那这个node就是下层list的T了
        class Node
        {
            public:
                //Node(T& t, unsigned long k):t(t), k(k), dirty(false){};
                Node():dirty(false){};
                unsigned long key(){return k;};

                T t;
                unsigned long k;
                
                bool dirty;
                //第一个bit用于latch,剩下7bit用于version
                atomic<unsigned long> latchVersion;
                
                void clear()
                {
                    t.clear();
                };

                void writeBack()
                {
                    t.writeBack();
                }

                //只能等的话那函数类型void也没啥
                void turnOnLatch()
                {
                    unsigned long now = 0, target = 0;

                    do
                    {
                        now = latchVersion.load();
                        
                        //cas上锁恐怕只能睡一会?看看论文
                        if(GET_BIT(now, 1))
                        {
                            //睡一会
                            continue;
                        }

                        target = SET_BIT_1(now, 1);

                    }while( !latchVersion.compare_exchange_strong(now, target) );

                };

                //按照论文上的论述,解锁顺便融合增加version的操作
                //---那你得保证一定修改了数据---其实页没必要,仅仅是改了下版本,最多是相关读重试下
                //不用循环cas,上了锁,只有我子处理这个值
                bool turnOffLatch()
                {
                    latchVersion.store(latchVersion.load() + 1);
                };

                unsigned long getVersion()
                {
                    return latchVersion.load() >> 1;
                };
        };

        T* get(unsigned long k, bool wLock = false)
        {
            typename LockFreeList<Node>::Node* p = table.find(k, true);

            if(!p)
            {
                p = new typename LockFreeList<Node>::Node;

                is.getItem(k, &((p->t).t), &((p->t).k));

                int ret = table.insert(p, true);//第一次插入当然是要引用的啦

                if(ret == 1)
                {
                    reclaim();
                }
            }

            //有点绕,我这层的Node是下层的T
            return &((p->t).t);
        };

        //本层的release用于解锁,调用下层release清计数
        void release(unsigned long k, bool wLock = false)
        {
            typename LockFreeList<Node>::Node* p = table.find(k);
            //typename LockFreeList<T>::Node* p = table.find(k);
            
            int ret = LockFreeList<Node>::decRef((typename LockFreeList<Node>::Node*)p);
        }

        //第一轮清理的时候会把ref=1变成0,所以说第二轮才会真正回收,想办法应对这种情况
        //---把扫描触发的阀值设置的低些
        void reclaim()
        {
            bool b = reclaimLock.load();
            if(b) return;

            bool ret = reclaimLock.compare_exchange_strong(b, true);
            if(!ret)return;

            //测试完后看看那种模式更好些
            //thread tRelaim(funcReclaim);
            funcReclaim();
            return;
        }


        void funcReclaim()
        {
            table.reclaim();
            //函数有天然的内存屏障功能
            reclaimLock.store(false);
        };

        //ClockCache(string fileName, int size = HASH_TABLE_SIZE) : table(size), size(size), lastI(0), fileName(fileName)
        ClockCache(int size = HASH_TABLE_SIZE) : table(size)
        {
            reclaimLock.store(false);
        };

        HashTable<Node> table;
        atomic<bool> reclaimLock;
        S is;

        //线程想要插入或是修改特定页是都要cas bit上锁---这个地方可能要跟锁协议区隔开来
        //你这个地方要是用cache来管理页锁,那么就得有有页号到锁的索引,即你得自己管理cache
        //这地方功能能不能搞到下边list的bit位里边...---下边太复杂了---
        //---这地方同步要做好,功能得是个读写锁,你只用一个bit仅仅是个写锁功能
        //---要保持持有写锁的时间很短,所以要先持有读锁,再上升,会有实现原理上边说的问题?---死锁
        //atomic<unsigned long> bitsOccupy[PAGE_NUM / 64];

        //---改为用std的读写锁
        //相应的,跟读写相关的cache函数该怎么定义
        //得有专门的回写函数
        //---得在B树的层次想好具体使用cache的场景
        //---读的时候必然是全读锁
        //---删除的时候需要类似并发cas的机制,不能随意覆盖,要保证一致性的状态变更,可以直接加写锁
        //---插入的时候
        //---我为啥搞那个升级锁来着,想不起来了,这3种情景,有一个写锁就可以了


        //---其实我一直在想这个问题,自增的场景跟原先的值无关,但需要根据基础值做变化的并发...


        //页锁的索引跟键值不是一个事
        //---看来我最初的想法是用文件页数来索引
        //---1,你这文件额多大啊      2,要增加页怎么办
        //---另外就是我一直预谋的,空间都先申请好,用一个缓存大小的锁数组,这个其实更合理
        //---写完上边这句话想到了...sb啊...直接放Node里不就完了...
        //shared_time_mutex mu[PAGE_NUM];

        //写下文件读写相关的代码,需要哪些功能
        //---你真正cache的是一个指针,那么这个页里的实际内容你究竟存在哪里
};
