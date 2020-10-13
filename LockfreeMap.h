/*************************************************************************************************
LockfreeMap -- Copyright (c) 2020, Markus Iser, KIT - Karlsruhe Institute of Technology

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 **************************************************************************************************/

#ifndef Lockfree_Map
#define Lockfree_Map

#include <cstdlib>
#include <cstring> 
#include <atomic>
#include <mutex>
#include <memory>

/**
 * T is the content type and must be integral
 * S is the sentinel element and must not occur in input
 * C maximal number of available hazards
 * */
template<typename T = uint32_t, int S = 0, unsigned int C = 8>
class LockfreeMap {
public:
    class const_iterator {
        T* pos;
        T** hazard;

    public:
        const_iterator(T** mem) : pos(*mem), hazard(mem) { }

        ~const_iterator() { *hazard = nullptr; }

        inline const T operator * () const { return *pos; }

        inline const_iterator& operator ++ () { ++pos; return *this; }

        inline bool done() { return *pos == S; }
    };
    

private:
    struct LockfreeVector {
        T* memory;
        std::atomic<unsigned int> cursor;
        volatile unsigned int capacity;

        LockfreeVector(unsigned int n) : cursor(0), capacity(n + 1) {
            memory = (T*)std::calloc(capacity, sizeof(T));
            if (S != 0) memset(memory, S, capacity * sizeof(T));
        }

        inline unsigned int size() const {
            return cursor.load(std::memory_order_relaxed);
        }

        T* push(T value) {
            uint32_t pos = cursor.fetch_add(1, std::memory_order_relaxed);
            while (true) {
                uint32_t cap = capacity;
                if (pos+1 < cap) { // GATE 1
                    std::atomic_thread_fence(std::memory_order_acquire);
                    memory[pos] = value;
                    return nullptr;
                } 
                else if (pos+1 == cap) {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    T* old = memory;
                    T* fresh = (T*)calloc(cap * 2, sizeof(T));
                    if (S != 0) memset(fresh, S, cap * 2 * sizeof(T));
                    
                    for (unsigned int i = 0; i < cap-1; i++) {
                        if (old[i] != S) fresh[i] = old[i];
                        else i--;
                    }

                    memory = fresh;
                    std::atomic_thread_fence(std::memory_order_release);
                    capacity *= 2; // open GATE 1
                    memory[pos] = value;
                    return old;
                } 
            }
        }

        inline const_iterator iter(T*& hazard) {
            while (hazard != memory) {
                hazard = memory;
                std::atomic_thread_fence(std::memory_order_acq_rel);
            }
            return const_iterator(&hazard);
        }
    };

    LockfreeVector* map; 
    const unsigned int size_;
    std::array<T*, C> hazards;

    void safe_free(T* mem) {
        std::atomic_thread_fence(std::memory_order_acquire);
        for (bool safe = false; !safe; ) {
            safe = true;
            for (T* p : hazards) {
                safe &= (p != mem);
            }
        }
        free(mem);
    }

    LockfreeMap(LockfreeMap const&) = delete;
    void operator=(LockfreeMap const&) = delete;
    LockfreeMap(LockfreeMap&& other) = delete;

public:
    LockfreeMap(unsigned int m, unsigned int n) : hazards(), size_(m) {
        map = (LockfreeVector*)std::calloc(size_, sizeof(LockfreeVector));
        for (unsigned int i = 0; i < size_; i++) {
            new ((void*)(&map[i])) LockfreeVector(n);
        }
        hazards.fill(nullptr);
    }

    ~LockfreeMap() { 
        for (unsigned int i = 0; i < size_; i++) free(map[i].memory);
        free(map);
    }

    unsigned int size() const {
        return size_;
    }

    void push(T key, T value) {
        T* ptr = map[key].push(value);
        if (ptr != nullptr) safe_free(ptr);
    }

    inline const_iterator iter(T key, unsigned int thread_id) {
        assert(hazards[thread_id] == nullptr); // at most one iterator per thread-id
        return map[key].iter(hazards[thread_id]);
    }

};

#endif