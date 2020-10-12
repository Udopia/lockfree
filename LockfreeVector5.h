/*************************************************************************************************
LockfreeVector -- Copyright (c) 2020, Markus Iser, KIT - Karlsruhe Institute of Technology

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

#ifndef Lockfree_VECTOR5
#define Lockfree_VECTOR5

#include <cstdlib>
#include <cstring> 
#include <atomic>
#include <mutex>
#include <memory>

/**
 * T is the content type and must be integral
 * S is the sentinel element and must not occur in input
 * Q is the counter type and specifies cache-line behaviour of the counter
 * */
template<typename T = uint32_t, int S = 0, typename Q = uint64_t>
class LockfreeVector5 {
public:
    class const_iterator {
        T* pos;
        std::atomic<Q>& counter;

    public:
        const_iterator(T* mem, std::atomic<Q>& counter_) : pos(mem), counter(counter_) { }

        ~const_iterator() { counter.fetch_sub(1, std::memory_order_relaxed); }

        inline const T operator * () const { return *pos; }

        inline const_iterator& operator ++ () { ++pos; return *this; }

        inline bool done() { return *pos == S; }
    };
    

private:
    T* memory;

    // cyclic flag, pointing to active counter
    unsigned int active;
    std::array<std::atomic<Q>, 2> counter;

    std::atomic<unsigned int> cursor;
    volatile unsigned int capacity;

    /**
     * Adds 1 to counter[A] and returns true, iff the following conditions are met:
     * If B is true: expects counter[A] to be greater than 0, otherwise does nothing and returns false
     * If B is false: expexts counter[A] to be zero, otherwise does nothing and retruns false
     * */
    template<unsigned int A, bool B>
    bool atomic_add() {
        Q current = counter[A].load(std::memory_order_relaxed);
        do {
            if (B != (current > 0)) { return false; }
        } while (!counter[A].compare_exchange_weak(current, current + 1, std::memory_order_relaxed, std::memory_order_relaxed));
        return true;
    }

    /**
     * Reserve id of active memory, while it is still in use.
     * The atomic_add<id, true>() ensures that id is still in use while it is reserved. 
     */
    unsigned int acquire_active() {
        while (true) {
            if (active == 0) { 
                if (atomic_add<0, true>()) return 0;
            }
            else if (active == 1) { 
                if (atomic_add<1, true>()) return 1;
            }
        }
    }

    /**
     * Reserve id of inactive memory, while it is not in use anymore.
     * The atomic_add<id, false>() ensures that id is not in use while it is reserved. 
     */
    bool acquire_inactive() {
        while (true) {
            if (active == 1) {
                if (atomic_add<0, false>()) return true;
            }
            else if (active == 0) {
                if (atomic_add<1, false>()) return true;
            }
        }
    }

    void release_as_last(unsigned int act, T* mem) {
        Q expect = 1;
        while (!counter[act].compare_exchange_weak(expect, 0, std::memory_order_relaxed, std::memory_order_relaxed)) {
            expect = 1;
        }
        free(mem);
    } 

    LockfreeVector5(LockfreeVector5 const&) = delete;
    void operator=(LockfreeVector5 const&) = delete;
    LockfreeVector5(LockfreeVector5&& other) = delete;

public:
    LockfreeVector5(unsigned int n) : cursor(0), capacity(n + 1), counter(), active(0) {
        memory = (T*)std::calloc(capacity, sizeof(T));
        if (S != 0) memset(memory, S, capacity * sizeof(T));
        atomic_add<0, false>();
    }

    ~LockfreeVector5() { 
        free(memory);
    }

    inline unsigned int size() const {
        return cursor.load(std::memory_order_relaxed);
    }

    void push(T value) {
        uint32_t pos = cursor.fetch_add(1, std::memory_order_relaxed);
        while (true) {
            uint32_t cap = capacity;
            if (pos+1 < cap) { // GATE 1
                std::atomic_thread_fence(std::memory_order_acquire);
                memory[pos] = value;
                return;
            } 
            else if (pos+1 == cap && acquire_inactive()) { // GATE 2
                std::atomic_thread_fence(std::memory_order_acquire);
                T* old = memory;
                T* fresh = (T*)calloc(cap * 2, sizeof(T));
                if (S != 0) memset(fresh, S, cap * 2 * sizeof(T));
                
                for (unsigned int i = 0; i < cap-1; i++) {
                    if (old[i] != S) fresh[i] = old[i];
                    else i--;
                }

                memory = fresh;
                active ^= 1;
                std::atomic_thread_fence(std::memory_order_release);
                capacity *= 2; // open GATE 1
                release_as_last(active^1, old); // open GATE 2
            } 
        }
    }

    inline const_iterator iter() {
        unsigned int act = acquire_active();
        return const_iterator(memory, counter[act]);
    }

};

#endif