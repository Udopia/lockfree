all: test 

test: LockfreeVectorTest.cc LockfreeVector.h
	g++ -O3 -pthread -g -o test LockfreeVectorTest.cc -ltbb -std=c++11

clean:
	rm test

