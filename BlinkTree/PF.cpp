#include "PF.h"
#include <iostream>
#include <shared_mutex>

using namespace RedBase;
using namespace std;

int HashTable::hash(PageNum pageNum, int& hashRet)
{
    hashRet = pageNum % HASH_TABLE_SIZE;

    return 0;
}

//page*就是一个page的地址,page**就是page指针即next这个字段的地址
//返回判断的是*p是否是空的--你实际想返回的是个**,但若按照改地址的原则,你要返回的是***...
//这样返回的优势,既能定位到所需元素,又拿到了上游next指针,方便插入
int HashTable::find(PageNum pageNum, Page*** page)
{
    int hashRet;
    hash(pageNum, hashRet);

    //加个dummy head吧,代码好写一些    
    Page** p = &(pageList[hashRet].hashNext);

    //这么搞其实正合适,dummy head是个空元素
    while( *p != NULL)
    {
        if( (*p)->pageNum == pageNum)
        {
            break;
        }

        p = &((*p)->hashNext);
    }
    *page = p;
    
    return 0;
}

int HashTable::insert(Page* page)
{
    Page** p;
    find(page->pageNum, &p);

    //重复的页
    if(*p != NULL)
    {
        cout << "dup page" << endl;
        return 1;
    }

    //插在头部,会找得更快些
    page->hashNext = pageList[hashRet].hashNext;
    pageList[hashRet].hashNext = page;

    return 0;
}

int HashTable::remove(PageNum pageNum)
{
    Page** p;
    find(pageNum, &p);

    cout << "in remove find" << (*p)->hashNext->pageNum << endl;

    if(*p == NULL)
    {
        cout << "not find the page" << endl;
        return 1;
    }

    Page* del = *p;
    *p = del->hashNext;

    //此处给Page弄个reset函数?
    del->hashNext = NULL;

    return 0;
}

PageCache::PageCache(string fileName)
{
    //从堆中申请,同直接mmap匿名映射有区别?
    pages = new Page[PAGE_BUFFER_SIZE];

    //分2个循环写还是一个循环写-----一个，page数组可以利用cache line
    pages[0].pre = NULL;
    pages[0].next = pages + 1;
    for(int i = 1; i < PAGE_BUFFER_SIZE - 1; ++i)
    {
        pages[i].next = pages + i + 1;
        pages[i].pre = pages + i - 1;
    }
    pages[PAGE_BUFFER_SIZE - 1].pre = pages + PAGE_BUFFER_SIZE - 1 - 1;
    pages[PAGE_BUFFER_SIZE - 1].next = NULL;

    freeList = pages;
    lruList.next = NULL;
    lruList.pre = NULL;

    this->fileName = fileName;
    fd = open(fileName.c_str(), O_CREAT | O_RDWR, 0777);
}

//你作为缓存区,找不着是不是得从文件里拿---要保证页码是有效的
//没办法,只能根据
int PageCache::find(PageNum pageNum, Page** page)
{
    Page** p = page;

    

    //想想hash表中跟同步有关的---如果同时有hash表中的remove,那么hash表也要加锁了
    //关键看情景,如果PageCache层的调用情景保证,PageCache读的时时候hash表只读,PageCache写的时候hash表只写,那么PageCache自己用锁就够了
    table.find(pageNum, &p);

    if(*p == NULL)
    {
        GetPageFromFile(pageNum, page)

        return 1;
    }

    page = p;

    return 0;
}

//得保证传入的PageNum是有效的
int PageCache::GetPageFromFile(PageNum pageNum, Page** page)
{
    int ret = 0;
    //要做判空
    if(freeList == NULL)return -1;
    page = freeList;
    freeList = page->next;

    page->pageNum = pageNum;
    page->refCount = 0;
    page->dirty = false;
    
    //位移是否合法?
    //多线程读一个文件的话,是否公用一个fd,如果公用的话修改位移就悲剧了---同步问题先不考虑
    lseek64(fd, pageNum * PAGE_SIZE, 0);
    ret = read(fd, page->data, PAGE_SIZE);
    if(ret != PAGE_SIZE)
    {
        return -1;
    }

    //插入头部
    page->next = lruList.next;
    page->next->pre = page;

    page->pre = &lruList;
    lruList.next = page;

    //插入到hash表中

    return 0;    
}

//看看find中查找页面的时候,需不需要顺便地去调用这个
//将来加锁是必然的,考虑下,如果当真每次查找都加锁,查询操作变串行了---最坏情况下这地方还是得加锁,其他可能也有类似的地方
int PageCache::refresh(Page* page)
{
    //移除,无论是lru还是free
    //这地方多线程会出问题吗?切换粒度应该是汇编语句
    if(page->pre)
    {
        page->pre->next = page->next;
    }
    if(page->next)
    {
        page->next->pre = page->pre;
    }

    //插入头部
    page->next = lruList.next;
    page->next->pre = page;

    page->pre = &lruList;
    lruList.next = page;

    return 0;
}

//要考虑引用计数的问题
//一加引用计数,一串函数都要改,注意这种代码架构变更行为---后来又看了下,哈哈哈
//这sb玩意怎么维护啊,都用一个锁妥妥要死锁啊,不用一个咋处理并发啊...
//怎么没有转移到freeList的步骤呢
//因为Page中的内容清掉了,hash表那边是不是也要remove一下
//lruList和freeList要约定好加锁顺序,千万别死锁
int PageCache::weedOut(int num)
{
    Page* p = &lruList;

    for(int i = 0; i < num; ++i)
    {
        p = p->pre;

        p->writeBack();

        if(p == &lruList)
        {
            break;
        }
    }

    p = p->pre;
    p->next = &lruList;
    lruList.pre = p;

    return 0;
}

//调用方处理加锁
int PageCache::writeBack(Page* page)
{
    int ret;
    ret = lseek64(fd, page->pageNum * PAGE_SIZE, 0);
    ret = write(fd, data, PAGE_SIZE);
            
    if(ret != PAGE_SIZE)
    {
        return -1;
    }

    return 0;
}

//暂时只考虑只服务于一个文件的情况
//初始是newPage是真的newPage,后来的话要检查是否有空page
//要明确语义,这个函数的功能是要创建一个文件中没有的新页,是有可能用到文件中的空页的,但同cache中的页不管
int PageCache::newPage()
{
    //获取文件当前页数,也就是读取文件first page中的页数字段
    

    
}

//看看有没有必要把headPage的指针存成成员变量;
//需不需要把根节点页固定在第3个?那样根节点迁移的话需要加锁换页处理
int PageCache::getFileProperty(FileProperty& property)
{
    Page** page;
    find(0, &page);

    property.cntPages = (int)((*p)->data);
    property.cntFreePages = (int)((*p)->data + 8);
    property.root = (int)((*p)->data + 16);

    return 0;
}
