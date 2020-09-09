all: test

test: LockfreeVectorTest.cc LockfreeVector.h
	g++ -pthread -g -o test LockfreeVectorTest.cc

clean:
	rm test

