test: test.o LockFreeQueue.o
	g++ test.o LockFreeQueue.o -lpthread -ltcmalloc -lprofiler -o test

test.o: test.cpp
	g++ -std=c++11 -c test.cpp

LockFreeQueue.o: LockFreeQueue.cpp
	g++ -std=c++11 -c LockFreeQueue.cpp

clean:
	rm test *.o
