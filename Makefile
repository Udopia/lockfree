all: test 

test: LockfreeVectorTest.cc LockfreeVector.h
	g++ -O3 -pthread -g -o test LockfreeVectorTest.cc -ltbb

clean:
	rm test

