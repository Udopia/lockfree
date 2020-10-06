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

template<typename T = uint32_t, int S = 0>
class LockfreeVector5 {
public:
    class const_iterator {
        T* pos;
        unsigned int act;
        LockfreeVector5& vector;

    public:
        const_iterator(LockfreeVector5& vector_) : vector(vector_) { 
            act = vector.acquire_active(); // get id of active slot
            pos = vector.memory;
        }

        ~const_iterator() { 
            vector.release(act); // release previously allocated slot
        }

        inline const T operator * () const {
            return *pos;
        }

        inline const_iterator& operator ++ () {
            ++pos;            
            return *this;
        }

        inline bool done() {
            return *pos == S;
        }
    };
    

private:
    T* memory;

    std::atomic<unsigned int> cursor;
    std::atomic<unsigned int> capacity;

    // memory is managed: for iterator-validity on realloc
    std::array<std::atomic<unsigned int>, 2> counter;
    // cyclic flag, pointing to active counter
    unsigned int active;

    /**
     * Adds 1 to counter[A] and returns true, iff the following conditions are met:
     * If B is true: expects counter[A] to be greater than 0, otherwise does nothing and returns false
     * If B is false: expexts counter[A] to be zero, otherwise does nothing and retruns false
     * */
    template<unsigned int A, bool B>
    bool atomic_add() {
        uint32_t current = counter[A].load(std::memory_order_relaxed);
        do {
            if (B != (current > 0)) { return false; }
        } while (!counter[A].compare_exchange_weak(current, current + 1, std::memory_order_relaxed, std::memory_order_relaxed));
        return true;
    }

    /**
     * Substracts 1 from counter[A] and returns true, iff the following conditions are met:
     * If B is true: expects counter[A] to be zero after removal, otherwise does nothing and returns false
     * If B is false: expexts nothing, just substracts one and returns true
     * */
    template<unsigned int A, bool B>
    bool atomic_sub() {
        uint32_t current = counter[A].load(std::memory_order_relaxed);
        do {
            if (B && (current != 1)) { return false; }
        } while (!counter[A].compare_exchange_weak(current, current - 1, std::memory_order_relaxed, std::memory_order_relaxed));
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
        if (act == 0) while (!atomic_sub<0, true>());
        else if (act == 1) while (!atomic_sub<1, true>());
        free(mem);
    } 

    void release(unsigned int act) {
        if (act == 0) atomic_sub<0, false>();
        if (act == 1) atomic_sub<1, false>();
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
            uint32_t cap = capacity.load(std::memory_order_acquire);
            if (pos+1 < cap) { // GATE 1
                memory[pos] = value;
                return;
            } 
            else if (pos+1 == cap && acquire_inactive()) { // GATE 2
                T* old = memory;
                T* fresh = (T*)calloc(cap * 2, sizeof(T));
                if (S != 0) memset(fresh, S, cap * 2 * sizeof(T));
                
                for (unsigned int i = 0; i < cap-1; i++) {
                    if (old[i] != S) fresh[i] = old[i];
                    else i--;
                }

                memory = fresh;
                capacity.store(cap * 2, std::memory_order_release); // open GATE 1
                active ^= 1; // << maybe this needs a release memory order too
                release_as_last(active^1, old); // open GATE 2
            } 
        }
    }

    inline const_iterator iter() {
        return const_iterator(*this);
    }

};

#endif