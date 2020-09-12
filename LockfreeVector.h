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

#define CAPACITY 0
#define COUNTER 1
#define OFFSET 2
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

    struct ManagedMemory {
        std::atomic<T>* memory;

        // realloc needs lock
        std::atomic<bool> lock_;

        ManagedMemory(uint32_t n) : lock_(false) {
            memory = (std::atomic<T>*)std::calloc(OFFSET + n + 1, sizeof(T));
            memory[CAPACITY].store(OFFSET + n + 1);
            memory[COUNTER].store(1);
        }

        ~ManagedMemory() {
            free(memory);
        }

        void lock() {
            for (;;) {
                if (!lock_.exchange(true, std::memory_order_acquire)) {
                    break;
                }
                while (lock_.load(std::memory_order_relaxed)) { }
            }
        }

        void unlock() {
            lock_.store(false, std::memory_order_release);
        }

        T capacity() {
            return memory[CAPACITY]-1-OFFSET;
        }

        void safe_free(std::atomic<T>* memory2) {
            while (memory2[COUNTER].load(std::memory_order_acquire) != 0) { }
            free(memory2);
        }

        void grow(uint32_t pos) {
            if (pos >= capacity()) {
                lock();
                if (pos >= capacity()) {
                    std::atomic<T>* memory2 = (std::atomic<T>*)std::calloc(pos * 2, sizeof(T));
                    std::memcpy((void*)memory2, (void*)memory, memory[CAPACITY] * sizeof(T));
                    memory2[CAPACITY].store(pos * 2);
                    memory2[COUNTER].store(1);

                    std::swap(memory2, memory);
                    release(memory2);
                    safe_free(memory2);
                }
                unlock();
            }
        }

        std::atomic<T>* acquire() {
            std::atomic<T>* mem = memory;
            mem[COUNTER].fetch_add(1, std::memory_order_acq_rel);
            return mem;
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
        memory.grow(pos);
        std::atomic<T>* mem = memory.acquire();
        uint32_t capa = mem[CAPACITY].load(std::memory_order_relaxed) - OFFSET - 1;
        while (!mem[pos].compare_exchange_strong(sentinel, value, std::memory_order_acq_rel)) {
            sentinel = SENTINEL;
            pos = cursor.load(std::memory_order_acquire);
            if (pos >= capa) {
                memory.release(mem);
                memory.grow(pos);
                mem = memory.acquire();
                capa = mem[CAPACITY].load(std::memory_order_relaxed) - OFFSET - 1;
            }
        }
        cursor.fetch_add(1, std::memory_order_release);
        memory.release(mem);
    }

    // error prone: realloc while stores are pending
    // need to keep old memory for readers and copy+flip as soon as writers are done
    void alt_push(T value) {
        uint32_t pos = cursor.fetch_add(1);
        memory.grow(pos);
        std::atomic<T>* mem = memory.acquire();
        mem[pos].store(value);
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