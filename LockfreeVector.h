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

#ifndef Lockfree_VECTOR
#define Lockfree_VECTOR

#include <cstdlib>
#include <cstring> 
#include <atomic>
#include <mutex>
#include <memory>

#define COUNTER 0
#define OFFSET 1
#define SENTINEL 0

template<typename T = uint32_t>
class LockfreeVector {
public:
    class const_iterator {
        T* pos;
        T* mem;

    public:
        const_iterator(T* mem_) { 
            mem = mem_;
            pos = mem_ + OFFSET;
        }

        ~const_iterator() { 
            ManagedMemory::release(mem);
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

    class ManagedMemory {
        std::atomic<uintptr_t> memory;
        uint32_t capacity;

        uintptr_t atomic_mem_lock() {
            uintptr_t mem = memory;
            for (;;) {
                mem = memory.fetch_or(1, std::memory_order_acquire);
                if (0 == (1 & mem)) {
                    break;
                }
                while (1 == (1 & memory.load(std::memory_order_relaxed))) { }
            }
            return mem;
        }

        void atomic_mem_unlock() {
            memory.fetch_and(~1, std::memory_order_release);
        }

    public:
        ManagedMemory(uint32_t n) {
            capacity = OFFSET + n + 1;
            memory = (uintptr_t)std::calloc(capacity, sizeof(T));
        }

        ~ManagedMemory() {
            free((void*)memory.load());
        }

        T* acquire() {
            uintptr_t mem = atomic_mem_lock();
            ((std::atomic<T>*)mem)[COUNTER].fetch_add(1, std::memory_order_relaxed);
            atomic_mem_unlock();
            return (T*)mem;
        }

        static void release(T* mem) {
            ((std::atomic<T>*)mem)[COUNTER].fetch_sub(1, std::memory_order_relaxed);
        }

        void set(uint32_t pos, T value) {
            uintptr_t mem = atomic_mem_lock();
            if (pos >= capacity-1) {
                uintptr_t memory2 = (uintptr_t)calloc(pos *2, sizeof(T));
                std::memcpy((void*)memory2, (void*)mem, capacity * sizeof(T));
                capacity = pos *2;
                ((T*)memory2)[COUNTER] = 0;
                ((T*)memory2)[pos] = value;
                memory.exchange(memory2, std::memory_order_release); //unlock
                while (((std::atomic<T>*)mem)[COUNTER].load(std::memory_order_relaxed) != 0) { }
                free((void*)mem);
            }
            else {
                ((T*)mem)[pos] = value;
                atomic_mem_unlock();
            }
        }
    };

private:
    // Cursor and memory support atomic access: for lock-free insert
    // Pointer to memory is managed: for iterator-validity on realloc
    ManagedMemory memory;
    std::atomic<uint32_t> cursor;

    LockfreeVector(LockfreeVector const&) = delete;
    void operator=(LockfreeVector const&) = delete;
    LockfreeVector(LockfreeVector&& other) = delete;

public:
    LockfreeVector(uint32_t n) : cursor(OFFSET), memory(n) { }

    ~LockfreeVector() { }

    inline uint32_t capacity() const {
        return memory.capacity();
    }

    inline uint32_t size() const {
        return cursor.load(std::memory_order_relaxed) - OFFSET;
    }

    void push(T value) {
        uint32_t pos = cursor.fetch_add(1, std::memory_order_relaxed);
        memory.set(pos, value);
    }

    inline const_iterator iter() {
        return const_iterator(memory.acquire());
    }

};

#endif