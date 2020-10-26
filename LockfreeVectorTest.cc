
#include <thread>
#include <vector>
#include <numeric>
#include <iostream>
#include <cassert>
#include <tbb/concurrent_vector.h>

#include "LockfreeVector.h"
#include "LockfreeVector2.h"
#include "LockfreeVector3.h"
#include "LockfreeVector4.h"
#include "LockfreeVector5.h"
#include "LockfreeVector6.h"
#include "LockfreeVector7.h"
#include "LockfreeVector8.h"
#include "LockfreeVector9.h"
#include "LockfreeMap.h"

typedef LockfreeVector<uint32_t> myvec;
typedef LockfreeVector2<uint32_t> myvec2;
typedef LockfreeVector3<uint32_t> myvec3;
typedef LockfreeVector4<uint32_t> myvec4;
typedef LockfreeVector5<int32_t, 0> myvec5;
typedef LockfreeVector6<int32_t, 0, 50> myvec6;
typedef LockfreeVector7<uint32_t, 1000> myvec7;
typedef LockfreeVector8<uint32_t, 1000> myvec8;
typedef LockfreeVector9<uint32_t, 1000, 0, 16> myvec9;
typedef LockfreeMap<int32_t, 0, 50> mymap;
typedef tbb::concurrent_vector<uint32_t> tbbvec;


template<class T>
void read(T& arr, std::vector<unsigned int>& test, unsigned int consumer_id) {
    for (auto it = arr.iter(); !it.done(); ++it) test[*it]++;
}
template<> void read<myvec6>(myvec6& arr, std::vector<unsigned int>& test, unsigned int consumer_id) {
    for (auto it = arr.iter(consumer_id); !it.done(); ++it) test[*it]++;
}
template<> void read<myvec7>(myvec7& arr, std::vector<unsigned int>& test, unsigned int consumer_id) {
    for (uint32_t lit : arr) if (lit > 0 && lit < test.size()) test[lit]++; else std::cout << lit << " ";
}
template<> void read<myvec8>(myvec8& arr, std::vector<unsigned int>& test, unsigned int consumer_id) {
    for (uint32_t lit : arr) if (lit > 0 && lit < test.size()) test[lit]++; else std::cout << lit << " ";
}
template<> void read<myvec9>(myvec9& arr, std::vector<unsigned int>& test, unsigned int consumer_id) {
    for (uint32_t lit : arr) if (lit > 0 && lit < test.size()) test[lit]++; else std::cout << lit << " ";
}
template<> void read<mymap>(mymap& map, std::vector<unsigned int>& test, unsigned int consumer_id) {
    for (int i = 0; i < map.size(); i++) {
        for (auto it = map.iter(i, consumer_id); !it.done(); ++it) test[*it]++;
    }
}
template<> void read<tbbvec>(tbbvec& arr, std::vector<unsigned int>& test, unsigned int consumer_id) {
    for (uint32_t lit : arr) test[lit]++;
}

template<class T>
void push(T& arr, uint32_t elem) {
    arr.push(elem);
}
template<> void push<mymap>(mymap& map, uint32_t elem) {
    map.push(elem-1, elem);
}
template<> void push<tbbvec>(tbbvec& arr, uint32_t elem) {
    arr.push_back(elem);
}

template<class T>
void producer(T& arr, uint32_t num, uint32_t amount) { 
    for (unsigned int i = 0; i < amount; i++) {
        push<T>(arr, num);
    }
}

template<class T>
void consumer(T& arr, unsigned int consumer_id, size_t max_threads, size_t max_numbers) {
    std::vector<unsigned int> test { };
    test.resize(max_threads+1);
    uint32_t size = 0;
    while (size < max_numbers * max_threads) {
        read(arr, test, consumer_id);
        size += std::accumulate(test.begin(), test.end(), 0);
        std::fill(test.begin(), test.end(), 0);
    }
}

template<class T>
void final_count(T& arr, unsigned int consumer_id, size_t max_threads, size_t max_numbers) {
    std::cout << "Done. Checking..." << std::endl;
    std::vector<unsigned int> test { };
    test.resize(max_threads+1);
    read(arr, test, consumer_id);
    std::cout << "Found " << test[0] << " Zeros" << std::endl;
    for (size_t i = 1; i <= max_threads; i++) {
        std::cout << "Found " << test[i] << " Entries of Thread " << i << std::endl;
    }
}

template<class T>
void run_test(T& arr, uint32_t max_numbers, size_t max_readers, size_t max_writers) {
    std::vector<std::thread> threads { };
    for (uint32_t n = 0; n < max_writers; n++) {
        threads.push_back(std::thread(producer<T>, std::ref(arr), n+1, max_numbers));
    }
    for (uint32_t n = 0; n < max_readers; n++) {
        threads.push_back(std::thread(consumer<T>, std::ref(arr), n, max_writers, max_numbers));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    final_count<T>(std::ref(arr), 0, max_writers, max_numbers);
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

    std::cout << "Running " << max_readers << " threads for reading and " << max_writers << " threads for writing " << max_numbers << " numbers to concurrent vector" << std::endl;
    auto begin = std::chrono::steady_clock::now();

    if (mode == -1) { 
        std::vector<uint32_t> arr(1000);
        for (uint32_t n = 0; n < max_writers; n++) {
            for (uint32_t i = 0; i < max_numbers; i++) {
                arr.push_back(n+1);
            }
        }
        for (uint32_t n = 0; n < max_readers; n++) {
            std::vector<unsigned int> test { };
            test.resize(max_readers+1);
            for (uint32_t n : arr) test[n]++;
        }
    } if (mode == 0) { 
        tbbvec arr(1000);
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 1) {
        myvec arr(1000);
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 2) { 
        myvec2 arr(1000);
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 3) {
        myvec3 arr(1000); 
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 4) {
        myvec4 arr(1000); 
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 5) {
        myvec5 arr(1000); 
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 6) {
        myvec6 arr(1000); 
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 7) {
        myvec7 arr{}; 
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 8) {
        myvec8 arr{}; 
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 9) {
        myvec9 arr{}; 
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }
    else if (mode == 10) {
        mymap arr(max_writers, 1000); 
        run_test<>(arr, max_numbers, max_readers, max_writers);
    }

    auto end = std::chrono::steady_clock::now();
    std::cout << "Time elapsed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << " ms" << std::endl;

    return 0;
}