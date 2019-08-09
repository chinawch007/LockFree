#ifndef LOCK_FREE_LIST_H
#define LOCK_FREE_LIST_H

/*
    对参数类的约束:
        要实现有意义的拷贝构造函数
        包含能返回key的成员函数

    先做判断再做cas的编程模式,先对cas根值做判断,没问题,说明当前状态符合要求,然后cas,失败再循环检查原因

    最低位bit为1标识无效---毕竟0标识正常的地址
    预计要留1bit来应对aba问题

    hp处理的是aba问题和多线程共享结构删除问题,有多个cas的第一参数引用的结构和多个线程共享的结构不能随意清除

    我这个list的定位,元素的内存管理是由外部操作的,我这边只负责个对传入参数插入排出
    考虑下你这个实现有没有aba的风险
*/

#include <atomic>
#include <iostream>
//#include "HP.h"

using namespace std;

#define GET_NEXT_NODE_ADDRESS(p) ( (Node*)( (unsigned long)((p->next).load()) & 0xFFFFFFFFFFFFFFFE ) )

//要不要搞成单例类
template<class T>
class LockFreeList
{   
    public:

    struct Node
    {
        T t;
        int i;
        atomic<void*> next;
    };

    //随机想想到的,上层要求同一个值的元素,只能插入一次---这地方hash层上得想办法处理

    //如果经常需要从头重新遍历的话,效率堪忧...
    //如果不用cas,我用的值可能是旧值,被改了,跳到了一个被删除的点上边?----其实没什么,只怕跑到一个失效节点上,反正也是重头再跑
    //---所担心的是我跑到你上了,你后来被失效了---
    //如果你不加锁,你就是返回了也可能被删掉啊,我引用的时候可能都是一个被回收空间,没有物理内存了---得挂hp了
    //所以你总纠结这些细节借口参数之类的,返回什么也不合适
    Node* find(unsigned long k, Node** pre)
    {
        //这种查询类似于一个状态转移,我从head的next出发,此时肯定是完全正确的,接下来一个一个的正确状态转移
        Node* p = &dummyHead;
        //此处是否有内存回收风险,n可不能回收啊
        //p和n之间有节点插入,也会有问题的---这种情况是没法阻止的,如果用锁来同步,你也看不到新的节点

        Node* n = GET_NEXT_NODE_ADDRESS(p);
        while(n)
        {
            //头节点有效,不怕死循环
            //if(!isValid(p))---你这么搞可能会n已经失效了,你还去看它的值
            if(!isValid(n))
            {
                //此种情况不从头再来可不可以,接着看下一个节点呗
                //---根据你具体实现,你这个节点被回收了,你所指向的节点不一定指哪呢?都不在链上了
                //--所以你得保证在彻底回收之前,指针链还是要保持有效的...
                //---没太大意义,你被删节点指向的节点可能也被删除了,想办法对这个指向值做个标识吧,0x1啥的
                //---其实类似于隔离性的保证就没有了,这么搞这个find是非常SB的
                //---这种情况下只能用来处理不太严格的需求,我们的每次find要保证有的话一定能找到,插入别重复
                p = &dummyHead;
                n = GET_NEXT_NODE_ADDRESS(p);
                continue;
            }

            //之前也想过用cas来做节点移动,铆钉住连接,但问题是cas是原子变量的专属,你一个普通的临时指针变量做不到
            //而且普通的查找你做cas,那效率基本上就放弃了

            if( (n->t).Key() == k )
            {
                *pre = p;
                return n;
            }

            //此处需要安全地拿到n节点的next
            //---此处需要mark一下,保证
            //---保证n不被删除往后移动就是安全的吗?next节点也可能被删除啊

            //这地方再做一次检查是不是就可以了,如果失效是不是可以认定已经被或者要被删除了,应该要重查
            //---其实也可以勉强往链上靠,毕竟还可能指向链,但指向节点可能也完了,要处理的情形比较复杂,简单化从头开始吧
            if(!isValid(n))
            {
                p = &dummyHead;
                n = GET_NEXT_NODE_ADDRESS(p);
                continue;
            }

            p = n;
            n = GET_NEXT_NODE_ADDRESS(p);
        }

        *pre = NULL;
        return NULL;
    };

    //hp要对before进行索引
    //考虑要不要对node进行索引---要索引的依据是会被删除或是aba,我这次是新插入的节点肯定不涉及删除,也不涉及aba
    //before->next是否涉及aba?---涉及,有可能这个节点被删除,然后T赋个新值又重新插进来了,这样可能会导致上层的语义发生变化,索引一下把
    //能否加个限制,标识说此时next节点不能被删?---但问题是我怎么检验这个标识,cas中不涉及到next->next?
    //---其实可以---不行,是说我在remove的时候,没法检验这个标识,那边我哪怕判断某个时刻没有这个标识,在cas之前又被别的线程设置了,我没法处理

    //你这种处理方式是说node一定要插入到before之后的,所以一直以before为基准进行重试
    int insert(Node* node, Node* before)
    {
        //统一用这个临时变量置换了,node和before都要指向这个,变化了重刷
        void* next;
        do
        {
            next = (before->next).load();
            if(!isValid(before))
            {
                return -1;
            }
            //倒是不用检查before的指向是否切换了

            (node->next).store(next);
        }
        //对before进行cas可以保证before的有效,但如何保证next的有效?next是无效节点是否影响插入
        //---不影响,但此时next节点的删除会失败,因为before变化了,要重新生成基准节点
        //---这个问题很严重***---此问题的核心在于你删除,不能把节点无效化之后就不管了
        while( !(before->next).compare_exchange_strong( next, node ) ); 

        return 0;
    };

    Node* getBefore(Node* node)
    {
        Node* p = &dummyHead;
        Node* n = GET_NEXT_NODE_ADDRESS(p);
        while(n)
        {
            //此函数当然是由删除时before节点变化所致,
            if(!isValid(p))
            {
                p = &dummyHead;
                n = GET_NEXT_NODE_ADDRESS(p);
                continue;
            }

            if(n == node)
            {
                return p;
            }

            p = n;
            n = GET_NEXT_NODE_ADDRESS(p);
        }
    };

    //跟上边一个问题,删除是针对before这个基准进行的,如果before和node之间关联变了,整体函数就失败了
    //删除失败后上边要换before蛮麻烦的---上边的cache清理调用是单线程操作,没啥问题
    //***考虑到上边标号的问题,我在把node无效化后,得负起责任把node删掉,不然find得总是从头跑---
    //---在上层重试吧---不行,因为invalid要先判断节点是否还有效防止并发删除,你不管了以后会一直失败.
    int remove(Node* node, Node* before)
    {        
        //此处两个要点,1杜绝多线程同时删除此节点,2防止后边插入或是删除节点
        if(!invalid(node))
        {
            return -1;
        }

        void *tmp;
        //后边的插入和删除都被杜绝了,所以next不用担心会被拒绝
        void *next = (node->next).load();
        do
        {
            tmp = (before->next).load();

            //对应上边的2个要点,我这边也要做两项检查,1是before没有被删除,2是before之后没有插入节点
            if(!isValid(before))
            {
                before = getBefore(node);
                continue;
            }
            //跟上边insert对比,这种情况算失败是因为我指定要删除node,before可能会发生变化 
            if( ( (unsigned long)tmp & 0xFFFFFFFFFFFFFFFE )  != (unsigned long)node)
            {
                before = getBefore(node);
                continue;
            }

        }
        //其实这种cas函数的格式就要求几乎首参要是临时变量,用地址,是要用它拿当前值的s
        while( !(before->next).compare_exchange_strong(tmp, next) );

        return 0;
    };

    //要明确下返回语义,true是我无效化成功,false是已经被别人无效化了,当前只有删除会调用无效化,那就是说别人已经进行删除了,虽然可能没完全结束
    //上层可能要等会
    bool invalid(Node* node)
    {
        void* next = ( (node)->next).load();
        unsigned long bits;
        void* target;

        //此处造成失败的原因有哪些?为什么要循环---可能指向节点发生了变化
        do
        {
            next = ( (node)->next).load();
            //bits = (unsigned long)( (node)->next).load();---像这么写会不会搞成2次不一样啊...
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

    bool isValid(Node* node)
    {
        unsigned long bits = (unsigned long)( ((node)->next).load() );
        if( (bits & 0x0000000000000001) == 1)
            return false;
        else 
            return true;
    };

    bool pin(Node* node)
    {
        void* next = ( (node)->next).load();
        unsigned long bits;
        void* target;

        //此处造成失败的原因有哪些?为什么要循环---可能指向节点发生了变化
        do
        {
            next = ( (node)->next).load();
            //bits = (unsigned long)( (node)->next).load();---像这么写会不会搞成2次不一样啊...
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

    bool isValid(Node* node)
    {
        unsigned long bits = (unsigned long)( ((node)->next).load() );
        if( (bits & 0x0000000000000001) == 1)
            return false;
        else 
            return true;
    };


    void print()
    {
        Node* p = &dummyHead;
        while(p)
        {
            cout << p->i << endl;
            void* tmp = (p->next).load();
            p = (Node*)( (unsigned long)(tmp) & 0xFFFFFFFFFFFFFFFE);
        }
    };

    //初始状态自己指向自己,那么为节点指向自己,似乎跟指向null没啥区别
    Node dummyHead;
};

#endif