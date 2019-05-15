#include <stddef.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <errno.h>

using namespace std;

#define PAGE_SIZE (4 * 1024)
#define PAGE_BUFFER_SIZE (40)
#define ALL_PAGES (-1)
#define HASH_TABLE_SIZE 16  

typedef int RC;
typedef int PageNum;

//多线程同步操作filehandle的话,不可避免对首页进行同步,所以只给上层使用1个handle还是多handle多fd-----有枷锁的多线程是否一定会提升性能
//6回写策略是怎样的?等到空闲页不够时一并写回还是平时就处理一些?...
//7缓存可拔插替换,编译选项选择缓存方案...

//非常奇怪啊王宠,你一直在追求无敌高效率,而当你接近这个层次的时候,你竟然害怕了,害怕自己的进步了...害怕啊...害怕尽力了却失败啊...
class Page
{
    public:
        Page():pageNum(-1), pre(NULL), next(NULL), hashNext(NULL), dirty(false), refCount(0){};
        //不加大括号的会,会未定义引用
        ~Page(){};

        void ref(){++refCount;};
        void unref(){--refCount;};

        //如果只是最近访问过的就移位置到队头，似乎不用引用计数？----是的，不是最优，先把功能完成
        //看下如果搞成私有的,怎么才能改动方便
        PageNum pageNum;
        Page* pre;
        Page* next;
        Page* hashNext;
         //是直接存数组好，还是存个指针，指针指向另外的地方好？对比下区别。   
        char data[PAGE_SIZE];

        bool dirty;
        int refCount;//多个handle引用一个页的情况---不需要handle这个代理,一样可以操作引用计数,
};

class HashTable
{
    public:
        HashTable(){};
        ~HashTable(){};

        int hash(PageNum pageNum, int& hashRet);

        //这么多*,强化记忆一下
        int find(PageNum pageNum, Page*** page);

        //对于重复值是怎么处理的？
        int insert(Page* page);

        //remove逼迫find要返回一个**
        int remove(PageNum pageNum);
              
        //你要是想做的通用搞成void指针也没问题
        Page pageList[HASH_TABLE_SIZE];
};

//对BufferCache的要求
//1、固定大小，初始化后不再申请内存---想扩大之后再说吧,暂时先这样
//2、用PageNum快速定位到页的位置，返回指针

//leveldb里边的，inuse，不严格区分谁先谁后了

//妥妥是要多线程操作的---如果我在这层做多线程同步了,在b树层还需要做吗?---多线程同步问题交个上层b树,其有自己的同步协议,我只是一个文件页面的代理,当前状态下不涉及同步问题
//排查PageCache中需要做线程间同步的操作点---不是读就是写,都尼玛需要---没啥好办法,只能加,跟协议层的锁隔离开
//一定要保证树协议锁要先于PageCache锁拿到,避免造成死锁
class PageCache
{
    public:
        //其实你应该想到的，成员是指针还是变量，初始化的方式也是个不同点
        PageCache();

        ~PageCache()
        {
            delete[] pages;   
        };

        int find(PageNum pageNum, Page** page);

        //分情况,可能需要freeList的锁
        int refresh(Page* page);

        //lru表和freeList表各一个锁
        int weedOut(int num);

        //暂时只有weedOut时会调用
        //这地方加锁怎么加啊?没办法每个页加个锁,可回写的时候在申请锁,肯定会造成读写并发---引用计数可以做到吗?
        //所以你得保证都是批量读写,这样就可以直接用lruList的锁了
        int writeBack(Page* page);

        //新搞出一个page,页号用一个文件还没分配的或者拿一个没用过的freePage
        //添加了这个页之后,回写那边肯定要加一些逻辑,想象下这个编程套路
        //这地方也需要同步
        int newPage();

        //HeadPage可以单独用一个锁
        int getFileProperty(FileProperty& property);

        HashTable table;

        Page* pages;
        Page lruList;
        //free表只用一个next就够了
        Page* freeList;

        //先把功能跑通,再改成读写锁
        mutex lruMutex;
        mutex freeMutex;
        mutex headPageMutex;//头结点锁,headPage都要要加锁

        string fileName;
        int fd;
};

struct FileProperty
{
    int cntPages;
    int cntFreePages;
    PageNum root;
};

//不同的handle操作文件,同步怎么搞--比如原子+磁盘中的值?那每次用这个值都要现从数据页中读吗?--能通知吗...
//如果要多线程同步的话,那么只能同步缓冲池中的文件首页了
//新时期,这个sb类依然有用,他的功能是一个管理Page的文件代理
class PF_FileHandle 
{
  public:
    PF_FileHandle  (){};                                  // Default constructor
    ~PF_FileHandle (){};                                  // Destructor

    //这个页码的含义,暂时先按照实际文件中的页码,而不是仅仅数据页的页码
    RC GetThisPage(PageNum pageNum, PF_PageHandle &pageHandle) const
    {
        int ret = 0;

        int count;
        getPageCount(count);
        if(count <= 3)
        {
            return 1;
        }
     
        //这么生产一个handle明显不太合规格
        ret = cache->find(pageNum, &(pageHandle.pPage));
        //如果cache中没有找到,要先到文件里读出来
        if(ret == 1)
        {
            

            lseek64(fd, pageNum * PAGE_SIZE, 0);
            ret = read(fd, (pageHandle.pPage)->data, PAGE_SIZE);
            if(ret != PAGE_SIZE)
            {
                return -1;
            }

            (pageHandle.pPage)->fd = fd;
        }
    };
    
    RC MarkDirty(PageNum pageNum) const             // Mark a page as dirty
    {
        PF_PageHandle pageHandle;
        GetThisPage(pageNum, pageHandle);

        pageHandle.pPage->dirty = true;
    };

    //原来这是怎么写的啊?...给个普通指针就可以了,文件只关心回写的是什么数据
    RC ForcePages(Page* page)
    {       
        lseek64(fd, page->pageNum * PAGE_SIZE, 0);
        write(fd, page->data, PAGE_SIZE);

        page->dirty = false;
    };

    RC ForcePages(PageNum pageNum = ALL_PAGES) const
    {
        if(pageNum == ALL_PAGES)
        {
            Page* p = (cache->lruList).next;
            while(p != &(cache->lruList))
            {
                //此处写回全部脏页要遍历列表,能否优化
                if(p->dirty)
                {
                    lseek64(fd, p->pageNum * PAGE_SIZE, 0);
                    write(fd, p->data, PAGE_SIZE);
                    //标识的设置似乎有同步风险
                    //能否把dirty搞成一个同步变量类,直接使用,load,store.
                    //如果决定上层只有一个filehandle那就不用考虑这么多了,先把功能实现了吧
                    p->dirty = false;
                }
            }
        }
        else
        {
            Page* p;
            cache->find(pageNum, &p);
            
            lseek64(fd, p->pageNum * PAGE_SIZE, 0);
            write(fd, p->data, PAGE_SIZE);

            p->dirty = false;
        }
    };   

    int getPageCount(int& count) const
    {
        Page* page;
        cache->find(0, &page);

        count = *((long*)(page->data));

        return 0;
    };

    int setBitmap(PageNum pageNum)
    {
        int pos = pageNum;
        pos--;
        int pageNumBitmap = (pos / (8 * PAGE_SIZE + 1)) * (8 * PAGE_SIZE + 1) + 1;

        PF_PageHandle pageHandle;

        GetThisPage(pageNumBitmap, pageHandle);

        //这地方修改内存中的cache,get and set了
        int posList =  (pos % (8 * PAGE_SIZE + 1) - 1) / 8;
        int posBit =  (pos % (8 * PAGE_SIZE + 1) - 1) % 8;
        char bitItem = (pageHandle.pPage)->data[posList];

        bitItem = bitItem | (1 << posBit);

        (pageHandle.pPage)->data[posList] = bitItem;
    };

    int unsetBitmap(PageNum pageNum)
    {
        int pos = pageNum;
        pos--;
        int pageNumBitmap = (pos / (8 * PAGE_SIZE + 1)) * (8 * PAGE_SIZE + 1) + 1;

        PF_PageHandle pageHandle;

        GetThisPage(pageNumBitmap, pageHandle);

        //这地方修改内存中的cache,get and set了
        int posList =  (pos % (8 * PAGE_SIZE + 1) - 1) / 8;
        int posBit =  (pos % (8 * PAGE_SIZE + 1) - 1) % 8;
        char bitItem = (pageHandle.pPage)->data[posList];

        bitItem = bitItem & (~((char)1 << posBit));

        (pageHandle.pPage)->data[posList] = bitItem;
    };

    int fd;
    PageCache* cache;

};


