all: test debug

test: LockfreeVectorTest.cc LockfreeVector*.h LockfreeMap.h
	g++ -mcx16 -O2 -flto -pthread -g -o test LockfreeVectorTest.cc -ltbb -std=c++11

debug: LockfreeVectorTest.cc LockfreeVector*.h LockfreeMap.h
	g++ -pthread -g -o dtest LockfreeVectorTest.cc -ltbb -std=c++11

clean:
	rm test

