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

#ifndef Lockfree_VECTOR4
#define Lockfree_VECTOR4

#include <cstdlib>
#include <cstring> 
#include <atomic>
#include <mutex>
#include <memory>

#define SENTINEL 0

template<typename T = uint32_t>
class LockfreeVector4 {
public:
    class ManagedMemory {
    public:
        T* memory;
        
        std::atomic<uint32_t> capacity;
        std::array<std::atomic<uint32_t>, 2> counter;
        uint8_t active;

        /**
         * Adds 1 to counter[A] and returns true, iff the following restrictions are met:
         * If B is true: expects counter[A] to be greater than 0, otherwise does nothing and returns false
         * If B is false: expexts counter[A] to be zero, otherwise does nothing and retruns false
         * */
        template<unsigned int A, bool B>
        bool atomic_add() {
            uint32_t current = counter[A].load(std::memory_order_relaxed);
            do {
                if (B != (current > 0)) {
                    return false;
                }
            } 
            while (!counter[A].compare_exchange_weak(current, current + 1, std::memory_order_relaxed, std::memory_order_relaxed));
            return true;
        }

        // returns true iff counter[A] is greater than zero after the substraction
        template<unsigned int A>
        bool atomic_sub() {
            return counter[A].fetch_sub(1) > 1;
        }

    public:
        ManagedMemory(uint32_t n) : capacity(n + 1), counter(), active(0) {
            memory = (T*)std::calloc(capacity, sizeof(T));
            atomic_add<0, false>();
        }

        ~ManagedMemory() {
            free((void*)memory);
        }

        // get pointer to active memory, when it is still in use
        unsigned int acquire_active() {
            while (true) {
                if (active == 0 && atomic_add<0, true>()) {
                    return 0;
                }
                else if (active == 1 && atomic_add<1, true>()) {
                    return 1;
                }
            }
        }

        // get pointer to inactive memory, when it not used anymore
        bool acquire_inactive() {
            while (true) {
                if (active == 1 && atomic_add<0, false>()) {
                    return true;
                }
                else if (active == 0 && atomic_add<1, false>()) {
                    return true;
                }
            }
        }

        void release(unsigned int act, T* mem) {
            if (act == 0 && !atomic_sub<0>()) free(mem);
            if (act == 1 && !atomic_sub<1>()) free(mem);
        }

        void set(uint32_t pos, T value) {
            while (true) {
                uint32_t cap = capacity.load(std::memory_order_relaxed);
                if (pos+1 < cap) { // GATE 1
                    memory[pos] = value;
                    return;
                } 
                else if (pos+1 == cap && acquire_inactive()) { // GATE 2
                    T* old = memory;
                    T* fresh = (T*)calloc(cap * 2, sizeof(T));
                    
                    for (unsigned int i = 0; i < cap-1; i++) {
                        if (old[i] != SENTINEL) fresh[i] = old[i];
                        else i--;
                    }

                    memory = fresh;
                    capacity.store(cap * 2, std::memory_order_relaxed); // open GATE 1
                    active ^= 1;
                    release(active^1, old); // open GATE 2
                } 
            }
        }
    };

    class const_iterator {
        ManagedMemory& memory;
        unsigned int act;
        T* pos;
        T* mem;

    public:
        const_iterator(ManagedMemory& memory_) : memory(memory_) { 
            act = memory.acquire_active(); // as soon as active memory is protected by the counter it might as well change
            pos = memory.memory; // can be more recent than acquired counter, but it does not hurt due to cyclicity of active-flag
        }

        ~const_iterator() { 
            memory.release(act, mem);
        }

        inline const T operator * () const {
            return *pos;
        }

        inline const_iterator& operator ++ () {
            ++pos;            
            return *this;
        }

        inline bool done() {
            return *pos == SENTINEL;
        }
    };

private:
    // Cursor and memory support atomic access: for lock-free insert
    // Pointer to memory is managed: for iterator-validity on realloc
    ManagedMemory memory;
    std::atomic<uint32_t> cursor;

    LockfreeVector4(LockfreeVector4 const&) = delete;
    void operator=(LockfreeVector4 const&) = delete;
    LockfreeVector4(LockfreeVector4&& other) = delete;

public:
    LockfreeVector4(uint32_t n) : cursor(0), memory(n) { }

    ~LockfreeVector4() { }

    inline uint32_t capacity() const {
        return memory.capacity();
    }

    inline uint32_t size() const {
        return cursor.load(std::memory_order_relaxed);
    }

    void push(T value) {
        uint32_t pos = cursor.fetch_add(1, std::memory_order_relaxed);
        memory.set(pos, value);
    }

    inline const_iterator iter() {
        return const_iterator(memory);
    }

};

#endif