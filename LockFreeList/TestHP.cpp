#include<thread>
#include<iostream>
#include<chrono>
#include<mutex>
#include<condition_variable>

#include "HP.h"

using namespace std;

class TT
{
    public:
        void clear()
        {
            cout << "clear" << endl;
        };

        int i;
};
TT* tt;

HP<TT> hp;

std::mutex mu, mu2;
std::condition_variable cv, cv2;
bool flag = false;


void funcRef()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));

    //1
    hp.inRef(0, (void*)tt);
    hp.inRef(1, (void*)(tt+1));

    cout << "1" << endl;

    {
        std::unique_lock<std::mutex> lk(mu);
        flag = true;
        cv.notify_one();
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
        std::unique_lock<std::mutex> lk(mu2);
        while(!flag)
        {
            cv2.wait(lk);
        }
        flag = false;
    }

    hp.outRef(0);
    hp.outRef(1);

    cout << "3" << endl;

    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
        std::unique_lock<std::mutex> lk(mu);
        flag = true;
        cv.notify_one();
    }
}

void funcDel()
{
    {
        std::unique_lock<std::mutex> lk(mu);
        while(!flag)
        {
            cv.wait(lk);
        }
        flag = false;
    }

    //2
    hp.del((void*)tt);
    hp.del((void*)(tt+1));

    cout << "2" << endl;

    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
        std::unique_lock<std::mutex> lk(mu2);
        flag = true;
        cv2.notify_one();
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
        std::unique_lock<std::mutex> lk(mu);
        while(!flag)
        {
            cv.wait(lk);
        }
        flag = false;
    }

    hp.del((void*)tt);
    hp.del((void*)(tt+1));

    cout << "4" << endl;
}

void processFunc()
{
    thread t1(funcRef);
    thread t2(funcDel);

    t1.join();
    t2.join();

    cout << "well" << endl;
}

int main()
{
    hp.init(2, 2, 2);
    tt = new TT[2];
    processFunc();

    return 0;

}