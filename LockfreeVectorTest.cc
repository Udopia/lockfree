
#include <thread>
#include <vector>
#include <iostream>
#include <cassert>
#include <tbb/concurrent_vector.h>

#include "LockfreeVector.h"

void tbb_produce_numbers(tbb::concurrent_vector<uint32_t>& arr, uint32_t num, size_t amount) {
    for (size_t i = 0; i < amount; i++) {
        assert(num > 0);
        arr.push_back(num);
    }
}

void tbb_read_numbers(tbb::concurrent_vector<uint32_t>& arr, size_t max_threads, bool verbose) {
    uint32_t size = 0;
    std::vector<unsigned int> test { };
    test.resize(max_threads+1);
    uint32_t count = 0;
    while (size < arr.size()) {
        size = arr.size();
        for (uint32_t lit : arr) { 
            //assert(lit > 0);
            //assert(lit <= max_threads);
            //if (lit == 0 || lit > max_threads) std::cout << "Read " << lit << ", ";
            test[lit]++;
        }
        if (verbose) {
            std::cout << "Found " << test[0] << " Zeros" << std::endl;
            for (size_t i = 1; i <= max_threads; i++) {
                std::cout << "Found " << test[i] << " Entries of Thread " << i << std::endl;
            }
        }
        std::fill(test.begin(), test.end(), 0);
    }
}


void produce_numbers(LockfreeVector<uint32_t>& arr, uint32_t num, size_t amount) {
    for (size_t i = 0; i < amount; i++) {
        arr.push(num);
    }
}


void alt_produce_numbers(LockfreeVector<uint32_t>& arr, uint32_t num, size_t amount) {
    for (size_t i = 0; i < amount; i++) {
        arr.alt_push(num);
    }
}

void read_numbers(LockfreeVector<uint32_t>& arr, size_t max_threads, bool verbose) {
    uint32_t size = 0;
    std::vector<unsigned int> test { };
    test.resize(max_threads+1);
    uint32_t count = 0;
    while (size < arr.size()) {
        size = arr.size();
        for (auto it = arr.iter(); !it.done(); ++it) { 
            //assert(*it > 0);
            //assert(*it <= max_threads);
            //if (*it == 0 || *it > max_threads) std::cout << "Read " << *it << std::endl;
            test[*it]++;
        }
        if (verbose) {
            std::cout << "Found " << test[0] << " Zeros" << std::endl;
            for (size_t i = 1; i <= max_threads; i++) {
                std::cout << "Found " << test[i] << " Entries of Thread " << i << std::endl;
            }
        }
        std::fill(test.begin(), test.end(), 0);
    }
}

void run_mine(size_t max_numbers, size_t max_readers, size_t max_writers, int mode = 0) {
    std::vector<std::thread> threads { };
    LockfreeVector<uint32_t> arr(10);
    for (uint32_t n = 0; n < std::max(max_readers, max_writers); n++) {
        if (n < max_writers) {
            if (mode == 0) threads.push_back(std::thread(produce_numbers, std::ref(arr), n+1, max_numbers));
            else if (mode == 1) threads.push_back(std::thread(alt_produce_numbers, std::ref(arr), n+1, max_numbers));
        }
        if (n < max_readers) threads.push_back(std::thread(read_numbers, std::ref(arr), max_writers, false));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    read_numbers(std::ref(arr), max_writers, true);
}

void run_tbb(size_t max_numbers, size_t max_readers, size_t max_writers) {
    std::vector<std::thread> threads { };
    tbb::concurrent_vector<uint32_t> arr(10);
    for (uint32_t n = 0; n < std::max(max_readers, max_writers); n++) {
        if (n < max_writers) threads.push_back(std::thread(tbb_produce_numbers, std::ref(arr), n+1, max_numbers));
        if (n < max_readers) threads.push_back(std::thread(tbb_read_numbers, std::ref(arr), max_writers, false));
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    tbb_read_numbers(std::ref(arr), max_writers, true);
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
        run_mine(max_numbers, max_readers, max_writers);
    }
    else if (mode == 1) {
        run_mine(max_numbers, max_readers, max_writers, 1);
    }
    else if (mode == 2) { 
        run_tbb(max_numbers, max_readers, max_writers);
    }

    auto end = std::chrono::steady_clock::now();
    std::cout << "Time elapsed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << " ms" << std::endl;

    return 0;
}