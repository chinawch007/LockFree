/*
    节点内容构成:
    HeadPage:BitMapPage:DataPage...


    //多线程操作公共字段的话,会成为热点
    HeadPage:
    文件中页数:空闲页数:根节点页码

    //暂时只考虑一个页的位图就能容纳下所有的页的情况
    BitmapPage:

    //需要本页页号吗?
    //叶子节点里存具体数据还是指针?存数据的话少一步磁盘读取,不差钱买硬盘的话,就存在节点中吧
    DataPage:
    4字节节点类型:元组数目

*/

#include "Node.h"
#include "PF.h"

typedef int RID;

enum NODE_TYPE
{
    NODE_TYPE_INNER = 1,
    NODE_TYPE_LEAF
};

//这些字段搞成4字节还是8字节?
#define NODE_TYPE_LENGTH 8
#define NODE_RECORD_COUNT_LENGTH 8
#define HEAD_LENGTH (NODE_TYPE_LENGTH + NODE_RECORD_COUNT_LENGTH)

//就是说b树只需要跟文件代理类打交道,直接从文件里拿东西,这在b树的定义上是自然的,至于缓冲区的处理,让去搞好了
//还是说我直接跟缓冲区打交道,缓冲区跟把b树同磁盘内容隔开,这样从层次上来说也是很自然的
template<class Record, class Compare>
class BPlusTree
{
    public:
        //初始化一棵空的树,只有前两个page
        //需要PageCache提供NewPage的功能
        int init()
        {

        }

        int restore()
        {

        };

        //这种类接口的定义，其实是对数据元素成员函数和成员变量的约束？
        int insert(Record record);

        int del(RID rid);

        int get(RID rid, Record& record)
        {
            Page page;

            PageNum pageNum = 2;
            fileHandle->getThisPage(pageNum, pageHandle);

            //总结下循环编程模式

            NODE_TYPE nodeType = (unsigned long)(pageHandle->pData);
            unsigned long recordCount = (unsigned long)(pageHandle->pData + NODE_TYPE_LENGTH);

            //在此定义下吧,内部节点的key,为下一层节点的第一个key
            //内部节点的key可不可能被修改或是其他变动?---会随着删除消失,仅此而已
            int start = 0, end = recordCount - 1;

            //先处理下特殊情况,小于最小值或大于等于最大值
            RID ridStart = (RID)(pageHandle->pData + HEAD_LENGTH);
            RID ridEnd = (RID)(pageHandle->pData + HEAD_LENGTH + end * record.size);
            RID ridRight = (RID)(pageHandle->pData + HEAD_LENGTH + end * record.size);

            if(nodeType == NODE_TYPE_INNER)
            {
                if(rid < ridStart)
                {
                    pageNum = (PaegNum)(pageHandle->pData + HEAD_LENGTH + recordCount * record.size);
                }
                else if(rid >= ridStart && rid < ridEnd)
                {
                    int mid = (start + end) / 2;
                    RID midRid;
                    while(end - start > 1)
                    {
                        midRid = (RID)(pageHandle->pData + HEAD_LENGTH + mid * record.size);
                        if(midRid <= rid)
                        {
                            start = mid;
                        }
                        else
                        {
                            end = mid;
                        }
                    }
                }
                //因为右链接是分裂引起的,所以key值理应是上升上去的那个节点的值,即右兄弟字数的最小值
                else if(rid >= ridEnd && rid < ridRight)
                {
                    pageNum = (PaegNum)(pageHandle->pData + HEAD_LENGTH + recordCount * record.size + recordCount * PAGE_NUM_SIZE);
                }
                else
                {
                    pageNum = (PaegNum)(pageHandle->pData + HEAD_LENGTH + recordCount * record.size + (recordCount + 2) * PAGE_NUM_SIZE);
                }
            }
            else if(nodeType == NODE_TYPE_LEAF)
            {
                if(rid < ridStart)
                {
                    return -1;
                }
                else if(rid >= ridStart && rid < ridEnd)
                {
                    int mid = (start + end) / 2;
                    RID midRid;
                    while(end - start > 1)
                    {
                        midRid = (RID)(pageHandle->pData + HEAD_LENGTH + mid * record.size);
                        if(midRid <= rid)
                        {
                            start = mid;
                        }
                        else
                        {
                            end = mid;
                        }
                    }
                }
                //因为右链接是分裂引起的,所以key值理应是上升上去的那个节点的值,即右兄弟字数的最小值
                //此处边界条件与内部节点不同
                else if(rid > ridEnd && rid < ridRight)
                {
                    return -1;
                }
                else
                {
                    pageNum = (PaegNum)(pageHandle->pData + HEAD_LENGTH + recordCount * record.size + (recordCount + 2) * PAGE_NUM_SIZE);
                }
            }
        };

        //写回的时机？----这是缓冲区管理的，我只管写到缓冲区即可----需不需要仔细想想，或许有更合适的方案？
        int split();

        //牵涉到核心问题,怎么在文件中append内容
        //魔鬼在于细节,我应该暂时不需要真的在文件中增加一页来,在缓冲区生成个新页即可
        int newNode(PageNum pageNum, const vector<Record>& recoreds)
        {
            
        };

        //这地方设计成读父类好还是子类复用？
        int ReadNode(BlockPointer bp, Node* pNode);

        //用一个PageNum来实现所有的你之前以为的节点类要实现的功能.
        int getRecord(RID rid, Record& record)
        {

        };

        //内部节点的话需要另外传个新节点的页号
        int insertRecord(Record record, PageNum rightPageNum)
        {
            
        };

        bool isFull()
        {

        };

        //处理完这个节点有生成新节点，分几步？用锁保持原子性？----包括父节点指针，３步？
        int splitNode(PageNum pageNum, vector<Record>& records)
        {

        };

    private:

        //需要一个首页的指针,
        PF_FileHandle* fileHandle;

        PageCache* cache;
};

/*

页节点内容:
1,record数目----由于假定记录大小固定,可在B树时计算最大值
2,record
3,指针,包括右边节点的,用页号标识
4,节点锁?----怎么锁,让缓冲区帮忙锁?bit位原子操作?---这部分放到锁管理器中
5,多版本并发控制?---

*/