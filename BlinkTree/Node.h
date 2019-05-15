

typedef unsigned long BlockPointer;
typedef int PageNum;
#define PAGE_NUM_SIZE 4

class Record
{

    public:
        static int size;
    private:


};

class InnerRecord: public Record
{

    public:

    private:

};

class LeafRecord: public Record
{

    public:

    private:

};

//对Record类做约束，要提供key成员的功能
//比较大小的功能可以放到Record中，而不必要特意拿出来到模板的参数里
//我使用你这个DataType类,必然对你这个类有个要求,比如说输出内容到缓冲区之类的
template<class DataType, class Compator> 
class Node
{
    public:
        int intsert(DataType data);

        int readRecord();

        //忘了这俩用来干啥的了
        //int readBuffer(PageNum pageNum);

        //int WriteBuffer(PageNum pageNum);

    private:
        //看看，平时不觉得，这个时候你这个BlockPointer用什么形式好，定义个类？还是？
        //忘记了最开始这部分代码什么时候写的了,现在看的话就是个页ID
        //BlockPointer bp;
        PageNum pageNum;

};

//参数统一都搞成一个类，至于你怎么排布，是你自己的问题
template<class DataType> 
class InnerNode : public template<class DataType> Node
{

};

template<class DataType> 
class LeafNode : public template<class DataType> Node
{

};