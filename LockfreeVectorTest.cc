
#include <thread>
#include <vector>
#include <iostream>
#include <cassert>
#include <tbb/concurrent_vector.h>

#include "LockfreeVector.h"

typedef LockfreeVector<uint32_t> myvec;
typedef tbb::concurrent_vector<uint32_t> tbbvec;


template<class T>
void read(T& arr, std::vector<unsigned int>& test, bool mode) { }

template<class T>
void push(T& arr, uint32_t elem, bool mode) {}

template<>
void read<myvec>(myvec& arr, std::vector<unsigned int>& test, bool mode) {
    for (auto it = arr.iter(); !it.done(); ++it) { 
        if (*it >= 0 && *it < test.size()) test[*it]++;
        else std::cout << *it << " ";
    }
}

template<>
void push<myvec>(myvec& arr, uint32_t elem, bool mode) {
    arr.push(elem);
}

template<>
void read<tbbvec>(tbbvec& arr, std::vector<unsigned int>& test, bool mode) {
    for (uint32_t lit : arr) { 
        if (lit >= 0 && lit < test.size()) test[lit]++;
        else std::cout << lit << " ";
    }
}

template<>
void push<tbbvec>(tbbvec& arr, uint32_t elem, bool mode) {
    arr.push_back(elem);
}


template<class T>
void producer(T& arr, uint32_t num, uint32_t amount, bool mode) { 
    for (unsigned int i = 0; i < amount; i++) {
        push<T>(arr, num, mode);
    }
}

template<class T>
void consumer(T& arr, size_t max_threads, bool verbose) {
    uint32_t size = 0;
    std::vector<unsigned int> test { };
    test.resize(max_threads+1);
    uint32_t count = 0;
    while (size < arr.size()) {
        size = arr.size();
        read(arr, test, false);
        if (verbose) {
            std::cout << "Found " << test[0] << " Zeros" << std::endl;
            for (size_t i = 1; i <= max_threads; i++) {
                std::cout << "Found " << test[i] << " Entries of Thread " << i << std::endl;
            }
        }
        std::fill(test.begin(), test.end(), 0);
    }
}

template<class T>
void run_test(uint32_t max_numbers, size_t max_readers, size_t max_writers, bool mode = false) {
    std::vector<std::thread> threads { };
    T arr(1000);
    for (uint32_t n = 0; n < std::max(max_readers, max_writers); n++) {
        if (n < max_writers) {
            threads.push_back(std::thread(producer<T>, std::ref(arr), n+1, max_numbers, mode));
        }
        if (n < max_readers) {
            threads.push_back(std::thread(consumer<T>, std::ref(arr), max_writers, false));
        }
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    consumer<T>(std::ref(arr), max_writers, true);
}

template<class T>
void run_test2(uint32_t max_numbers, size_t max_readers, size_t max_writers, bool mode = false) {
    std::vector<std::thread> threads { };
    T arr(1000);
    for (uint32_t n = 0; n < max_writers; n++) {
        threads.push_back(std::thread(producer<T>, std::ref(arr), n+1, max_numbers, mode));
    }
    for (uint32_t n = 0; n < max_readers; n++) {
        threads.push_back(std::thread(consumer<T>, std::ref(arr), max_writers, false));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    consumer<T>(std::ref(arr), max_writers, true);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cout << "Usage: " << argv[0] << " [n_numbers] [n_readers] [n_writers]" << std::endl;
        return 0;
    }

    size_t max_numbers = atoi(argv[1]);
    size_t max_readers = atoi(argv[2]);
    size_t max_writers = atoi(argv[3]);
    int mode = 0;
    if (argc > 4) mode = atoi(argv[4]);

    std::cout << "Running " << max_readers << " threads for reading and " << max_writers << " threads for writing " << max_numbers << " numbers to my concurrent vector" << std::endl;
    auto begin = std::chrono::steady_clock::now();

    if (mode == 0) {
        run_test<myvec>(max_numbers, max_readers, max_writers);
    }
    else if (mode == 1) { 
        run_test<tbbvec>(max_numbers, max_readers, max_writers);
    }
    else if (mode == 2) {
        run_test2<myvec>(max_numbers, max_readers, max_writers);
    }
    else if (mode == 3) { 
        run_test2<tbbvec>(max_numbers, max_readers, max_writers);
    }

    auto end = std::chrono::steady_clock::now();
    std::cout << "Time elapsed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << " ms" << std::endl;

    return 0;
}