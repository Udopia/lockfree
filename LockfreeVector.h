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
        std::atomic<T>* pos;
        std::atomic<T>* mem;

    public:
        const_iterator(std::atomic<T>* mem_, uint32_t start) { 
            mem = mem_;
            pos = mem + start;
        }

        ~const_iterator() { 
            ManagedMemory::release(mem);
        }

        inline const T operator * () const {
            return pos->load(std::memory_order_relaxed);
        }

        inline const_iterator& operator ++ () {
            ++pos;            
            return *this;
        }

        inline bool done() {
            return pos->load(std::memory_order_relaxed) == 0;
        }
    };

    class ManagedMemory {
        std::atomic<uintptr_t> memory;
        std::atomic<uint32_t> capacity;
        std::atomic<bool> lock_; // realloc

        void realloc_lock() {
            for (;;) {
                if (!lock_.exchange(true, std::memory_order_acquire)) {
                    break;
                }
                while (lock_.load(std::memory_order_relaxed)) { }
            }
        }

        void realloc_unlock() {
            lock_.store(false, std::memory_order_release);
        }

        std::atomic<T>* alloc(uint32_t size, std::atomic<T>* old = nullptr, uint32_t old_size = 0) {
            std::atomic<T>* mem = (std::atomic<T>*)std::calloc(size, sizeof(T));
            if (old != nullptr) std::memcpy((void*)mem, (void*)old, old_size * sizeof(T));
            mem[COUNTER].store(1);
            return mem;
        }

        void safe_free(std::atomic<T>* mem) {
            while (((std::atomic<T>*)mem)[COUNTER].load(std::memory_order_acquire) != 0) { }
            free((void*)mem);
        }

        void grow(uint32_t pos) {
            if (pos >= capacity.load(std::memory_order_relaxed)-1) {
                realloc_lock();
                if (pos >= capacity.load(std::memory_order_acquire)-1) {
                    uintptr_t memory2 = (uintptr_t)alloc(pos + 10, (std::atomic<T>*)memory.load(), capacity);

                    // for (;;) {std::cout << "-";
                    //     if (0 == (1 & memory.fetch_or(1, std::memory_order_acquire))) {
                    //         break;
                    //     }
                    //     while (1 == (1 & memory.load(std::memory_order_relaxed))) { std::cout << "_"; }
                    // }

                    memory2 = ~1 & memory.exchange(memory2, std::memory_order_release);

                    capacity.store(pos + 10, std::memory_order_release);

                    release((std::atomic<T>*)memory2);
                    safe_free((std::atomic<T>*)memory2);
                }
                realloc_unlock();
            }
        }

    public:
        ManagedMemory(uint32_t n) : lock_(false) {
            capacity = OFFSET + n + 1;
            memory = (uintptr_t)alloc(capacity);
        }

        ~ManagedMemory() {
            free((void*)memory.load());
        }

        uint32_t demand(uint32_t need) {
            grow(need);
            return capacity.load(std::memory_order_acquire)-1;
        }

        std::atomic<T>* acquire() {
            uintptr_t mem = memory;
            // for (;;) {std::cout << ":";
            //     mem = memory.fetch_or(1, std::memory_order_acquire);
            //     if (0 == (1 & mem)) {
            //         break;
            //     }
            //     while (1 == (1 & memory.load(std::memory_order_relaxed))) { std::cout << "."; }
            // }
            ((std::atomic<T>*)mem)[COUNTER].fetch_add(1, std::memory_order_acq_rel);
            // memory.fetch_and(~1, std::memory_order_release);
            return (std::atomic<T>*)mem;
        }

        static void release(std::atomic<T>* mem) {
            mem[COUNTER].fetch_sub(1, std::memory_order_acq_rel);
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
        T sentinel = SENTINEL;
        uint32_t pos = cursor.load(std::memory_order_acquire);
        uint32_t capa = memory.demand(pos);
        std::atomic<T>* mem = memory.acquire();
        while (!mem[pos].compare_exchange_strong(sentinel, value, std::memory_order_acq_rel)) {
            sentinel = SENTINEL;
            pos = cursor.load(std::memory_order_acquire);
            if (pos >= capa) {
                memory.release(mem);
                capa = memory.demand(pos);
                mem = memory.acquire();
            }
        }
        cursor.fetch_add(1, std::memory_order_release);
        memory.release(mem);
    }

    // error prone: realloc while stores are pending
    // need to keep old memory for readers and copy+flip as soon as writers are done
    void alt_push(T value) {
        uint32_t pos = cursor.fetch_add(1, std::memory_order_acq_rel);
        memory.demand(pos);
        std::atomic<T>* mem = memory.acquire();
        mem[pos].store(value, std::memory_order_release);
        memory.release(mem);
    }

    // inline const T operator [] (uint32_t i) const {
    //     assert(i < size());
    //     return memory[OFFSET + i].load();
    // }

    inline const_iterator iter() {
        return const_iterator(memory.acquire(), OFFSET);
    }

};

#endif