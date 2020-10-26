all: test debug

test: LockfreeVectorTest.cc LockfreeVector*.h LockfreeMap.h
	clang -O3 -mcx16 -lstdc++ -pthread -g -o test LockfreeVectorTest.cc -ltbb

debug: LockfreeVectorTest.cc LockfreeVector*.h LockfreeMap.h
	clang -mcx16 -lstdc++ -pthread -g -o dtest LockfreeVectorTest.cc -ltbb 

clean:
	rm test

