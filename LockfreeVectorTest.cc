
#include <thread>
#include <vector>
#include <iostream>
#include <cassert>

#include "LockfreeVector.h"

void produce_numbers(LockfreeVector<uint32_t>& arr, uint32_t num, size_t amount) {
    for (size_t i = 0; i < amount; i++) {
        arr.push(num);
    }
}

void read_numbers(LockfreeVector<uint32_t>& arr, size_t max_threads) {
    uint32_t size = 0;
    std::vector<unsigned int> test { };
    test.resize(max_threads+1);
    uint32_t count = 0;
    while (size < arr.size()) {
        size = arr.size();
        for (auto it = arr.iter(); !it.done(); ++it) { 
            assert(*it > 0);
            assert(*it <= max_threads);
            test[*it]++;
        }
        std::cout << "Found " << test[0] << " Zeros" << std::endl;
        for (size_t i = 1; i <= max_threads; i++) {
            std::cout << "Found " << test[i] << " Entries of Thread " << i << std::endl;
        }
        std::fill(test.begin(), test.end(), 0);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " [n_numbers] [n_threads]" << std::endl;
        return 0;
    }

    size_t max_numbers = atoi(argv[1]);
    size_t max_threads = atoi(argv[2]);
    size_t total_numbers = max_numbers * max_threads;

    std::vector<std::thread> threads { };
    //LockfreeVector<uint32_t> arr(total_numbers);
    LockfreeVector<uint32_t> arr(100);
    for (uint32_t n = 0; n < max_threads; n++) {
        threads.push_back(std::thread(produce_numbers, std::ref(arr), n+1, max_numbers));
        threads.push_back(std::thread(read_numbers, std::ref(arr), max_threads));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    read_numbers(std::ref(arr), max_threads);

    return 0;
}