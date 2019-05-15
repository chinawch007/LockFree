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

    //private:
        void* buffer;
        int bufferSize;

        //多进程的话要从内存中取,多线程就可以单例搞了吗?---多进程的话就只当做是个临时变量了
        //int head;
        //int tail;

        atomic<int>* head;
        atomic<int>* tail;

        //int dataSize;

        int shmId;
};