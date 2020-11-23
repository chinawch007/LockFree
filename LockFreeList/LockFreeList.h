#ifndef LOCK_FREE_LIST_H
#define LOCK_FREE_LIST_H

/*
    对参数类的约束:
        要实现有意义的拷贝构造函数
        包含能返回key的成员函数
        要有自我清除的clear函数

    先做判断再做cas的编程模式,先对cas根值做判断,没问题,说明当前状态符合要求,然后cas,失败再循环检查原因

    最低位bit为1标识无效---毕竟0标识正常的地址
    倒数第二位给LruRef
    由于上层需要引用计数来标识节点是否能删除;lru计数来处理lru;需要拿64位地址高8位来实现这个功能
    ---看看能不能吧lruRef同ref合并下---可以,引用时0跳2,其余情况正常加减1---其实应该想到的,这俩值其实在cas时都是协同行动的,先不动吧,规范些

    hp处理的是aba问题和多线程共享结构删除问题,有多个cas的第一参数引用的结构和多个线程共享的结构不能随意清除

    我这个list的定位,元素的内存管理是由外部操作的,我这边只负责个对传入参数插入排出

    因为要为上层提供hash功能,要有去重功能,故接口一般性添加key值参数
*/

#include <atomic>
#include <iostream>
#include "HP.h"

using namespace std;

#define GET_NEXT_NODE_ADDRESS(p) ( (Node*)( (unsigned long)((p->next).load()) & 0x0000FFFFFFFFFFFC ) )
#define GET_NODE_ADDRESS(p) ( (Node*)( (unsigned long)(p) & 0x0000FFFFFFFFFFFC ) )

#define getRef(p) ( ((unsigned long)( ((p)->next).load() ) & 0xFF00000000000000) >> 56 )
#define getLruRef(p) ( ( (unsigned long)( ((p)->next).load() ) & 0x0000000000000002 ) >> 1)

#define getRefFromBits(b) ( ((unsigned long)(b) & 0xFF00000000000000) >> 56 )
                                                  
#define getLruRefFromBits(b) ( ( (unsigned long)(b) & 0x0000000000000002 ) >> 1 )

#define setRefToBits(b, ref) ( ( (unsigned long)(b) & 0x00FFFFFFFFFFFFFF) | (ref << 56) )
#define setLruRefToBits(b, lruRef) ( ( (unsigned long)(b) & 0xFFFFFFFFFFFFFFFD) | (lruRef << 1) )

#define SET_BIT_1(b, w) ( b | (1<<w) )
#define GET_BIT(b, w) ( (b>>w) & 1)

#define HP_OUT_REF_2 hp->outRef(1);hp->outRef(0);

template<class T>
class LockFreeList
{   
    public:

    //本节点由下层hp删除,那么需要提供函数调用上层的函数功能来清理空间
    struct Node
    {
        //atomic<void*> next;
        T t;
        atomic<void*> next;
        void clear()
        {
            t.clear();
            //上层传过来的是个T,不是指针,意味着hp在delete的时候会把2层都delete掉了
        };
    };

    //没在这里new,是因为之前在里边new的时候编译有问题?
    void init(HP<Node>* hp)
    {
        this->hp = hp;
    };

/*
    cas前进和非cas前进的区别
    非:当前节点被删除,前进到空或下个节点;前进到了一个被删除节点;前进到插入之后的节点;
    ---这里相对应的,我头尾做两个有效性检查就能保证安全
    是:可探测当前节点是否为空;前进到一个被删除节点;可探测后边新插入的节点;

*/

    //如果经常需要从头重新遍历的话,效率堪忧...
    //如果你不加锁,你就是返回了也可能被删掉啊,我引用的时候可能都是一个被回收空间,没有物理内存了---得挂hp了
    //在find返回的内容不能百分百确定准确的情况下要做并发安全的防备

    //找到第一个比node的k大的点
    //pre和n都是要挂hp的
    int find(unsigned long k, Node** node, Node** pre, bool ref = false)
    {
START:
        Node* p = &dummyHead;
        //此处是否有内存回收风险,n可不能回收啊
        int ret = 0;
        //插入一个节点到空串中,因为这里不检查dummy的值,没啥问题
        Node* n = GET_NEXT_NODE_ADDRESS(p);

        hp->inRef(0, (void*)p);
        hp->inRef(1, (void*)n);
        if(GET_NEXT_NODE_ADDRESS(p) != n)
        {
            goto START;
        }

        while(n)
        {
            if(!isValid(n))
            {
                //关于遇到无效节点的处理讨论:
                //此种情况不从头再来可不可以,接着看下一个节点呗
                //---根据你具体实现,你这个节点被回收了,你所指向的节点不一定指哪呢?都不在链上了---后来想,其实没啥,可以继续以此类推
                //--所以你得保证在彻底回收之前,指针链还是要保持有效的...
                //---没太大意义,你被删节点指向的节点可能也被删除了,想办法对这个指向值做个标识吧,0x1啥的
                //---其实类似于隔离性的保证就没有了,这么搞这个find是非常SB的
                //---这种情况下只能用来处理不太严格的需求,我们的每次find要保证有的话一定能找到,插入别重复
                //p = &dummyHead;
                //n = GET_NEXT_NODE_ADDRESS(p);
                //continue;
                goto START;
            }

            //之前也想过用cas来做节点移动,铆钉住连接,但问题是cas是原子变量的专属,你一个普通的临时指针变量做不到
            if( (n->t).key() >= k )
            {
                *pre = p;
                *node = n;

                if((n->t).key() == k)
                {
                    if(ref)
                    {
                        if(incRef(n) == 0)
                        {
                            *pre = p;
                            *node = n;
                            return 0;
                        }   
        
                        p = &dummyHead;
                        n = GET_NEXT_NODE_ADDRESS(p);
                        continue;
                    }

                    *pre = p;
                    *node = n;
                    return 0;
                    
                }
                else
                {
                    return 1;
                }
            }

            //此处需要安全地拿到n节点的next
            //---此处需要mark一下,保证
            //---保证n不被删除往后移动就是安全的吗?next节点也可能被删除啊

            p = n;
            n = GET_NEXT_NODE_ADDRESS(p);
            hp->inRef(0, (void*)p);
            hp->inRef(1, (void*)n);
            if(GET_NEXT_NODE_ADDRESS(p) != n)//此处其实可以继续往下走
            {
                goto START;
            }

/*
            //这地方再做一次检查是不是就可以了,如果失效是不是可以认定已经被或者要被删除了,应该要重查
            //---注释下,此处是为了处理此种情况:我拿到的是无效节点p的下一个节点n,此时这个n是什么完全不确定
            //---其实也可以勉强往链上靠,毕竟还可能指向链,但指向节点可能也完了,要处理的情形比较复杂,简单化从头开始吧
            //此处检查的还是n,double check
            //---因为在前跳之前做这个,其实我已经做了最大努力了
            if( !isValid(p) )
            {
                p = &dummyHead;
                n = GET_NEXT_NODE_ADDRESS(p);
                continue;
            }
            //再回来看下其实没必要,只要被检查节点处于有效状态就行
*/
        }

        //没找着比node->key大的
        //注意下返回哈,node可是指空的
        *pre = p;
        *node = n;

        return 1;
    };

    int insert(Node* node, bool ref = false)
    {
        Node* pre;
        Node* n;
        int ret;

        //这边逻辑是这样,没找到的情况下,不断尝试插入---插入要具有判断是否重复的功能
        //---找到了,就算了
        do
        {
            //不能完全信任find返回,在返回过程中可能有其他变数
            //---下边的insert做了pre的next是否依然链接到n的检查
            //------n的有效性在此不重要
            ret = find((node->t).key(), &n, &pre);

            if(ret == 0)break;

            //不需要n参数了
            ret = insert(node, pre);
        }
        while(ret);//ref的改变当然会引起这里的失败

        //插入和增加引计不能分开搞,我刚插进去,还没增加计数就删掉了
        //if(ref)incRef(node);

        return 0;
    };

    //hp要对before进行索引,好让指针使用不会出错
    //考虑要不要对node进行索引---要索引的依据是会被删除或是aba,我这次是新插入的节点肯定不涉及删除,也不涉及aba
    //before->next是否涉及aba?---涉及,有可能这个节点被删除,然后T赋个新值又重新插进来了,这样可能会导致上层的语义发生变化,索引一下把
    //能否加个限制,标识说此时next节点不能被删?---但问题是我怎么检验这个标识,cas中不涉及到next->next?
    //---其实可以---不行,是说我在remove的时候,没法检验这个标识,那边我哪怕判断某个时刻没有这个标识,在cas之前又被别的线程设置了,我没法处理

    //你这种处理方式是说node一定要插入到before之后的,所以一直以before为基准进行重试
    //插入相同值算是失败
    //---插入的模式,cas只在固定两点之间操作,所以对后边的点做检验后,后边地址不动,可以保证不会插入重复的

    int insert(Node* node, Node* before)
    {
        //hp->inRef(0, (void*)node);//这里node其实是不用挂hp的
        //hp->inRef(1, (void*)before);

        //统一用这个临时变量置换了,node和before都要指向这个,变化了重刷
        void* next;

        //先取后检,真是好套路
        next = (before->next).load();
        if(!isValid(before))
        {
            HP_OUT_REF_2
            return -1;
        }
        //倒是不用检查before的指向是否切换了

        unsigned long tmp = (unsigned long)next;
        tmp = tmp | 0x0100000000000002;//在这里就把ref和lruRef做了
        void* target = (void*)(tmp & 0xFF00FFFFFFFFFFFE );//大哥啊,这里要用位运算的与,不能是逻辑的与

        (node->next).store(target);
        //要求find拿到的before一定要是小于node的
        //---要防止同样位置有其他节点插入的情况
        //------有其他节点插入也不是不可以,只是依然要保持有序
        if(GET_NEXT_NODE_ADDRESS(node) && GET_NEXT_NODE_ADDRESS(node)->t.key() <= node->t.key() )
        {
            HP_OUT_REF_2
            return -2;
        }

        void* addr;
        unsigned long need = (unsigned long)(next) & (0xFF00000000000003);//只取before我们需要的部分
        addr = (void*)(need | (unsigned long)node);

        //如果是ref相关的改变也造成了失败,倒是蛮遗憾的
        if( !(before->next).compare_exchange_strong( next, addr, memory_order_seq_cst ) )
        {
            HP_OUT_REF_2
            return -3;
        }

        HP_OUT_REF_2
        return 0;
    };

    int remove(unsigned long k)
    {
        Node* node;
        Node* pre;


        int ret = 0;
        do
        {
            ret = find(k, &node, &pre);

            if(ret != 0)break;

            ret = remove(node, pre);
        }
        //此处要求,节点已经不存在,你要返回失败
        //---即节点已经被别的线程删除了
        //---所以结束循环的条件,要么是我删除成功了,要么是已经不存在了
        //应用计数变化引起的cas失败怎么处理---跟被其他线程删除流程是类似的,第二次循环时检测出失败
        while(ret != 0);

        //hp->del((void*)node);回收内存的步骤不在这里做,由上层调用移除和回收

        //是不是任何情况下都可以返回0,毕竟不存在或是被别人删了,结果也是这个节点没有了
        return 0;
    };

    //跟上边一个问题,删除是针对before这个基准进行的,如果before和node之间关联变了,整体函数就失败了
    //删除失败后上边要换before蛮麻烦的---上边的cache清理调用是单线程操作,没啥问题
    //***考虑到上边标号的问题,我在把node无效化后,得负起责任把node删掉,不然find得总是从头跑---
    //---在上层重试吧---不行,因为invalid要先判断节点是否还有效防止并发删除,你不管了以后会一直失败.
    int remove(Node* node, Node* before)
    {        
        //hp->inRef(0, (void*)node);
        //hp->inRef(1, (void*)before);

        cout << "thread " << hp->getThreadId() << " remove " << (node->t).k << endl;

        //此处两个要点,1杜绝多线程同时删除此节点,2防止后边插入或是删除节点
        if(!invalid(node))
        {
            HP_OUT_REF_2
            return -1;
        }

        if(getRef(node) > 0)
        {
            HP_OUT_REF_2
            valid(node);
            return -5;
        }

        //后边的插入和删除都被杜绝了,所以next不用担心会被拒绝
        void *next = (node->next).load();
        next = (void*) ( ((unsigned long)next) & 0xFFFFFFFFFFFFFFFE );

        void* tmp = (before->next).load();

        //对应上边的2个要点,我这边也要做两项检查,1是before没有被删除,2是before之后没有插入节点
        if(!isValid(before))
        {
            HP_OUT_REF_2
            valid(node);
            return -2;
        }

        //跟上边insert对比,这种情况算失败是因为我指定要删除node,before可能会发生变化 
        //---发生插入了
        if( ( (unsigned long)tmp & 0x0000FFFFFFFFFFFC )  != (unsigned long)node)
        {
            HP_OUT_REF_2
            valid(node);
            return -3;
        }

        void* addr;
        unsigned long need = (unsigned long)(tmp) & (0xFF00000000000003);//只取before我们需要的部分
        addr = (void*)(need | (unsigned long)next);

        if( !(before->next).compare_exchange_strong(tmp, addr) )
        {
            HP_OUT_REF_2
            valid(node);
            return -4;
        }

        HP_OUT_REF_2
        return 0;
    };

    void reclaim()
    {
        cout << "list recliam" << endl;

        Node* p = &dummyHead;
        Node* n = GET_NEXT_NODE_ADDRESS(p);
        while(n)
        {
            if( getRef(n) == 0 )
            {
                if( getLruRef(n) == 0)
                {
                    //此处出现错误的情况,在于此点先被其他线程删除,再插入
                    //---考虑到只有一个线程回收,没这个问题
                    cout << "thread " << hp->getThreadId() << " write back:" << endl;
                    (n->t).writeBack();
                    remove( (n->t).key());
                    hp->del((void*)n);
                    
                    //此处删掉了一个节点,所以用p做前置
                    n = GET_NEXT_NODE_ADDRESS(p);
                }
                else if( getLruRef(n) == 1 )
                {
                    decLruRef(n);

                    p = n;
                    n = GET_NEXT_NODE_ADDRESS(n);
                }
            }
            else
            {
                p = n;
                n = GET_NEXT_NODE_ADDRESS(n);
            }

        }
        
    };

    //要明确下返回语义,true是我无效化成功,false是已经被别人无效化了,当前只有删除会调用无效化,那就是说别人已经进行删除了,虽然可能没完全结束
    //上层可能要等会
    bool invalid(Node* node)
    {
        void* next;
        unsigned long bits;
        void* target;

        //此处造成失败的原因有哪些?为什么要循环---可能指向节点发生了变化
        do
        {
            next = ( (node)->next).load();
            bits = (unsigned long)next;

            //先判断,再cas,无比严密的流程
            if( (bits & 0x0000000000000001) == 1)
            {
                return false;
            }

            bits |= 0x0000000000000001;
            target = (void*) bits;
        }
        while( !((node)->next).compare_exchange_strong(next, target) );

        return true;
    };

    //用于中断删除过程,恢复初始状态
    bool valid(Node* node)
    {
        void* next = (node->next).load();
        unsigned long bits = (unsigned long)next;
        void* target;

        //先判断,再cas,无比严密的流程
        if( bits & 0x0000000000000001 == 0)
        {
            return false;
        }

        bits &= 0xFFFFFFFFFFFFFFFE;
        target = (void*) bits;

        //没人会另外再有效化这个节点,不用搞循环了
        (node->next).compare_exchange_strong(next, target);

        return true;
    };

    //连带着一起处理LruRef
    static int incRef(Node* node)
    {
        void* next;
        unsigned long bits, ref, lruRef;
        void* target;

        next = ( (node)->next).load(memory_order_seq_cst);
        bits = (unsigned long)next;

        do
        {
            if(!isValid(node))
            {
                return -1;
            }

            //后边的判断都要以这个值为准,不要重新去节点里取了
            next = ( (node)->next).load();
            bits = (unsigned long)next;

            ref = getRefFromBits(bits);
            lruRef = getLruRefFromBits(bits);

            bits = setRefToBits(bits, ref + 1);
            if(lruRef == 0)//此时似乎不会存在ref不为0的情况吧
            {
                bits = setLruRefToBits(bits, 1);
            }

            target = (void*) bits;
        }
        //会因为什么原因失败:
        //---next指向节点变化,可重试
        //---被删除,重试检查失败
        //---Ref,LruRef变化,可重试
        while( !((node)->next).compare_exchange_strong(next, target) );

        if(!isValid(node))
        {
            return -1;
        }

        return 0;
    };


    //为了给cache层用搞成了static
    static int decRef(Node* node)
    {
        void* next;
        unsigned long bits, ref, lruRef;
        void* target;

        cout << "in decRef node addr:" << (unsigned long)node << endl;
        do
        {
            //这步当道下一句后边倒也可以,我大概是为了直接用isValid的宏
            //删除了就不能再处理引用计数的问题了吗?---为了简化流程可以这样,再重新插入吧
            //---能够减引用计数说明当前引用计数是大于0的,会杜绝删除,一般不会出现被删的情况
            if(!isValid(node))
            {
                return -1;
            }

            //后边的判断都要以这个值为准,不要重新去节点里取了
            next = (node->next).load();
            bits = (unsigned long)next;

            //降引用计数不涉及lru
            ref = getRefFromBits(bits);
            if(ref == 0)
            {
                return -2;
            }

            bits = setRefToBits(bits, ref - 1);
            target = (void*) bits;
        }
        //会因为什么原因失败:
        //---next指向节点变化,可重试
        //---被删除,重试检查失败
        //---Ref,可重试
        while( !((node)->next).compare_exchange_strong(next, target) );

        if(!isValid(node))
        {
            return -1;
        }

        return 0;
    };

    static bool decLruRef(Node* node)
    {
        void* next;
        unsigned long bits, ref, lruRef;
        void* target;

        cout << "in decLruRef, ref:" << getRef(node) << ", key:" << (node->t).key() << endl;

        do
        {
            if(!isValid(node))
            {
                return -1;
            }

            if(getRef(node) > 0)
            {
                return -2;
            }

            next = ( (node)->next).load();
            bits = (unsigned long)next;

            lruRef = getLruRefFromBits(bits);

            if(lruRef != 1)
            {
                return 1;
            }

            bits = setLruRefToBits(bits, 0);
            target = (void*) bits;
        }
        //会因为什么原因失败:
        //---next指向节点变化,可重试
        //---被删除,重试检查失败
        //---Ref变化,重试检验失败
        while( !((node)->next).compare_exchange_strong(next, target) );

        cout << "in decLruRef, ref:" << getRef(node) << ",key:" << (node->t).key() << endl;

        return 0;
    };

    static bool isValid(Node* node)
    {
        unsigned long bits = (unsigned long)( (node->next).load() );
        return !(bits & 0x0000000000000001);
    };


    void print()
    {
        int i = 0;

        cout << "thread " << hp->getThreadId() << " print begin" << endl;
        Node* p = (Node*)( (dummyHead.next).load() );
        while(p)
        {
            cout << dec << i++ << ":" << (p->t).k << ":" << getRefFromBits( (p->next).load() ) << ":" << getLruRefFromBits( (p->next).load() ) << endl;
            p = GET_NEXT_NODE_ADDRESS(p);
        }

        cout << "thread " << hp->getThreadId() << " print end" << endl;
        cout << endl;

    };

    //初始状态自己指向自己,那么为节点指向自己,似乎跟指向null没啥区别
    //按理说是应该放个值的,但find的时候其实都是从第一个节点开始比较,所以其实不用管
    Node dummyHead;
    HP<Node>* hp;
};

#endif