# LockFree

## LockFreeQueue   
可多进程多线程并发尾部push,头部pop的队列,操作元素是任意长度字节内容.环形缓冲区,每个线程首先cas抢占缓冲区头尾位置再进行安全读写.注意防范缓冲区填满造成的尾部写踩踏头部读和缓冲区数据被读完造成的读取正在被写的数据.

## LockFreeList   
不同于在头尾做无锁pop和push,单链表内部做无锁并行插入删除很困难.难点在于没法用一个cas操作使链表做原子化的状态变迁,相邻位置的删除和插入难以处理."https://docs.rs/crate/crossbeam/0.2.4/source/hash-and-skip.pdf" 利用由于内存对齐而没被使用的地址低位bit参与cas操作,用一个bit位来标识单链表的某个节点是否处于已被删除而无效的状态,从而实现了类似DoubleWordCas的效果.

## ClockCache   
基于上述无锁链表实现的一个应用时钟算法(https://en.wikipedia.org/wiki/Page_replacement_algorithm#Clock) 无锁LRU Cache.
当前64位linux只使用低48位的bit作为地址,空余大量bit位可供使用.拓展上述LockFreeList的思路:从其中取1bit作为时钟算法所需的LRU标识;取8bit做引用计数实现cache功能,有线程引用的结构不参与LRU的扫描淘汰.       

本意是为B树提供一个读取磁盘块的Cache,外部使用类需实现一个从后备存储空间获取某个元素的成员函数,例如在B树中取一个节点.