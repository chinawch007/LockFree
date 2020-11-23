#include<atomic>

using namespace std;

/*
    队列构成:
    4字节head:4字节tail:item:item:item...

    item构成:
    标识此部分数据已经写入完成的4字节WRITE_OVER_TOKEN:4字节长度len:内容
*/

//如果每次都到共享内存头部去取head和tail,那么没必要搞成volatile类型的,毕竟都用不到cache
class LockFreeQueue
{
    public:
        int init(int bufferSize);

        int destroy();

        int push(void* data, int len);

        int pop(void* data, int& len);

        void* buffer;
        int bufferSize;

        atomic<int>* head;
        atomic<int>* tail;
        //读线程原子增大head,抢到读部分缓冲区的权力,但可能没有读完的时候写线程来追尾了,
        //---需要有个限制告知写线程它要写的缓冲区部分是否有线程在读
        atomic<int>* headRead;
        atomic<int>* inited;

        int shmId;
};