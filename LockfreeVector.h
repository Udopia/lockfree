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

#define STARVE 0
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
            return pos->load();
        }

        inline const_iterator& operator ++ () {
            ++pos;            
            return *this;
        }

        inline bool done() {
            return pos->load() == 0;
        }
    };

    struct ManagedMemory {
        std::atomic<T>* memory;

        ManagedMemory(uint32_t capacity) {
            memory = (std::atomic<T>*)std::calloc(capacity, sizeof(T));
            memory[COUNTER].store(1);
        }

        void grow(uint32_t capacity, uint32_t grow_factor = 1) {
            std::atomic<T>* memory2 = (std::atomic<T>*)std::calloc(capacity * grow_factor, sizeof(T));
            memory2[COUNTER].store(1);
            std::memcpy((void*)(memory2 + OFFSET), (void*)(memory + OFFSET), (capacity - OFFSET) * sizeof(T));

            memory[STARVE].store(1);

            std::swap(memory, memory2);

            release(memory2);
        }

        std::atomic<T>* acquire() {
            while (memory[STARVE].load() == 1);
            memory[COUNTER].fetch_add(1);
            return memory;
        }

        static void release(std::atomic<T>* mem) {
            int32_t prev = mem[COUNTER].fetch_sub(1);
            if (prev == 1) {
                free(mem);
            }
        }

        std::atomic<T>* refresh(std::atomic<T>* mem) {
            if (mem == memory) {
                return mem;
            } else {
                release(mem);
                return acquire();
            }
        }
    };

private:
    // Cursor and memory support atomic access: for lock-free insert
    // Pointer to memory is managed: for iterator-validity on realloc
    ManagedMemory memory;
    std::atomic<uint32_t> cursor;

    uint32_t capacity_; // capacity is safe by realloc mutex

    // Realloc needs mutex
    std::atomic<bool> spinlock;

    uint32_t get_pos_or_grow() {
        uint32_t pos = this->cursor.load();
        if (pos < capacity_-1) {
            return pos;
        } else {
            while (!ensure_capacity(pos));
            return pos;
        }
    }

    bool ensure_capacity(uint32_t pos) {
        if (pos < capacity_-1) {
            return true;
        } else {
            bool spinval = false;
            while (!spinlock.compare_exchange_weak(spinval, true)) {
                return false;
            }
            if (pos >= capacity_-1) {
                memory.grow(capacity_, 2);
                capacity_ *= 2;
            }
            spinlock.store(false);
            return true;
        }
    }

    LockfreeVector(LockfreeVector const&) = delete;
    void operator=(LockfreeVector const&) = delete;
    LockfreeVector(LockfreeVector&& other) = delete;

public:
    LockfreeVector(uint32_t n) : capacity_(OFFSET + n), cursor(OFFSET), memory(OFFSET + n), spinlock(false) {
    }

    ~LockfreeVector() { 
        memory.release(memory.memory);
    }

    inline uint32_t capacity() const {
        return capacity_ - OFFSET;
    }

    inline uint32_t size() const {
        return cursor.load() - OFFSET;
    }

    void push(T value) {
        T sentinel = SENTINEL;
        uint32_t pos = get_pos_or_grow();
        std::atomic<T>* mem = memory.acquire();
        while (!mem[pos].compare_exchange_weak(sentinel, value)) {
            sentinel = SENTINEL;
            pos = get_pos_or_grow();
            mem = memory.refresh(mem);
        }
        this->cursor.fetch_add(1);
        memory.release(mem);
    }

    void alt_push(T value) {
        uint32_t pos = cursor.fetch_add(1);
        while (!ensure_capacity(pos));
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