all: test 

test: LockfreeVectorTest.cc LockfreeVector.h LockfreeVector2.h LockfreeVector3.h
	g++ -O3 -flto -pthread -g -o test LockfreeVectorTest.cc -ltbb -std=c++11

debug: LockfreeVectorTest.cc LockfreeVector.h LockfreeVector2.h
	g++ -pthread -g -o dtest LockfreeVectorTest.cc -ltbb -std=c++11

clean:
	rm test

