test: TestLockFreeList.o
	g++ -g TestLockFreeList.o -lpthread -o test

TestLockFreeList.o: TestLockFreeList.cpp
	g++ -g -std=c++11 -c TestLockFreeList.cpp

TestHP.o: TestHP.cpp
	g++ -g -std=c++11 -c TestHP.cpp

clean:
	rm test *.o
