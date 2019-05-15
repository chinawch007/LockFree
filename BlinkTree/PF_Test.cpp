#include "PF.h"
#include <iostream>

using namespace std;
using namespace RedBase;


int main()
{
    //test hash
    HashTable table;
    PageNum pageNum1 = 0;
    int hashRet;
    table.hash(pageNum1, hashRet);
    cout << hashRet << endl;

    Page pages[200];

    for(int i = 0; i < 200; ++i)
    {
        pages[i].pageNum = i;
    }

    //test insert
    table.insert(pages + 0); 
    table.insert(pages + HASH_TABLE_SIZE);
    table.insert(pages + 2 * HASH_TABLE_SIZE);

    Page *p = (table.pageList)[0].hashNext;
    while(p)
    {
        cout << "before remove:" << p->pageNum << endl;
        p = p->hashNext;
    }

    //test find
    Page** pFind;
    table.find(HASH_TABLE_SIZE, &pFind);

    cout << "find pageNum:" << *pFind - pages << endl;

    //test remove
    table.remove(HASH_TABLE_SIZE);

    p = (table.pageList)[0].hashNext;
    while(p)
    {
        cout << "after remove:" << p->pageNum << endl;
        p = p->hashNext;
    }


    //test FILE
    int ret = 0;

    PF_Manager manager;

/*    
    ret = manager.CreateFile("./test1");
    if(ret < 0)
    {
        cout << "CreateFile error:" << ret << endl;
        return 0;
    }
*/    

    PF_FileHandle file;
    ret = manager.OpenFile("./test1", file);
    if(ret < 0)
    {
        cout << "OpenFile error:" << ret << endl;
        return 0;
    }

    cout << "fd:" << file.fd << endl;

    manager.CloseFile(file);

    return 0;
}