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

#ifndef Lockfree_VECTOR2
#define Lockfree_VECTOR2

#include <cstdlib>
#include <cstring> 
#include <atomic>
#include <mutex>
#include <memory>

#define SENTINEL 0

template<typename T = uint32_t>
class LockfreeVector2 {
public:
    class ManagedMemory {
        std::array<T*, 2> memory;
        
        std::atomic<uint32_t> capacity;

        std::atomic<uint32_t> product;
        uint8_t active;

        /**
         * Atomically multiplies the factor F to global product, if the following restriction is met:
         * If B is true: expects F to already be a factor of product, otherwise does nothing and returns false
         * If B is false: expexts F not to be a factor of the product, otherwise does nothing and retruns false
         * */
        template<unsigned int F, bool B>
        bool atomic_multiply() {
            uint32_t current = product.load(std::memory_order_relaxed);
            do {
                if (B != (current % F == 0)) {
                    return false;
                }
            } 
            while (!product.compare_exchange_weak(current, current * F, std::memory_order_relaxed, std::memory_order_relaxed));
            return true;
        }

        bool atomic_multiply_2_true() {
            uint32_t current = product.load(std::memory_order_relaxed);
            do {
                if (current & 1 != 0) return false;
            } 
            while (!product.compare_exchange_weak(current, current << 1, std::memory_order_relaxed, std::memory_order_relaxed));
            return true;
        }

        bool atomic_multiply_2_false() {
            uint32_t current = product.load(std::memory_order_relaxed);
            do {
                if (current & 1 == 0) return false;
            } 
            while (!product.compare_exchange_weak(current, current << 1, std::memory_order_relaxed, std::memory_order_relaxed));
            return true;
        }

        // returns true iff F is still a factor of product after division
        template<unsigned int F>
        bool atomic_divide() {
            uint32_t current = product.load(std::memory_order_relaxed);
            while (!product.compare_exchange_weak(current, current / F, std::memory_order_relaxed, std::memory_order_relaxed));
            return current % (F*F) == 0;
        }

        bool atomic_divide_two() {
            uint32_t current = product.load(std::memory_order_relaxed);
            while (!product.compare_exchange_weak(current, current >> 1, std::memory_order_relaxed, std::memory_order_relaxed));
            return current % 4 == 0;
        }

    public:
        ManagedMemory(uint32_t n) : capacity(n + 1), product(7), active(0) {
            memory[active] = (T*)std::calloc(capacity, sizeof(T));
            atomic_multiply_2_false();//took shared ownership of 0
        }

        ~ManagedMemory() {
            free((void*)memory[active]);
        }

        // get pointer to active memory, when it is still in use
        T* acquire_active() {
            while (true) {
                //if (active == 0 && atomic_multiply<2, true>()) {
                if (active == 0 && atomic_multiply_2_true()) {
                    return (T*)memory[0];
                }
                if (active == 1 && atomic_multiply<3, true>()) {
                    return (T*)memory[1];
                }
            }
        }

        // get pointer to inactive memory, when it not used anymore
        T* acquire_inactive() {
            while (true) {
                //if (active == 1 && atomic_multiply<2, false>()) {
                if (active == 1 && atomic_multiply_2_false()) {
                    return (T*)memory[0];
                }
                if (active == 0 && atomic_multiply<3, false>()) {
                    return (T*)memory[1];
                }
            }
        }

        void release(T* mem) {
            if (mem == memory[0]) {
                if (!atomic_divide_two()) free(mem);
            } else {
                if (!atomic_divide<3>()) free(mem);
            }
        }

        void set(uint32_t pos, T value) {
            while (true) {
                uint32_t cap = capacity.load(std::memory_order_relaxed);
                if (pos+1 < cap) { // GATE 1
                    T* active_mem = acquire_active();
                    active_mem[pos] = value;
                    release(active_mem);
                    //memory[active][pos] = value;
                    return;
                } 
                else if (pos+1 == cap && acquire_inactive()) { // GATE 2
                    // prepare unused slot
                    memory[!active] = (T*)calloc(cap * 2, sizeof(T));
                    std::memcpy((void*)memory[!active], (void*)memory[active], cap * sizeof(T)); // <- might miss some here
                    active ^= 1; // switching active slot                    
                    capacity.store(cap * 2, std::memory_order_relaxed); // open GATE 1
                    
                    // copy possibly missing data
                    T* old = memory[!active];
                    unsigned int factor = !active == 0 ? 4 : 9;
                    while (product.load(std::memory_order_relaxed) % factor == 0); // wait until the new inactive is not used by others

                    for (unsigned int i = cap / 2; i < cap-1; i++) {
                        if (old[i] != 0) memory[active][i] = old[i];
                    }
                    release(old); // open GATE 2
                }
            }
        }
    };

    class const_iterator {
        ManagedMemory& memory;
        T* pos;
        T* mem;

    public:
        const_iterator(ManagedMemory& memory_) : memory(memory_) { 
            mem = memory.acquire_active();
            pos = mem;
        }

        ~const_iterator() { 
            memory.release(mem);
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

    LockfreeVector2(LockfreeVector2 const&) = delete;
    void operator=(LockfreeVector2 const&) = delete;
    LockfreeVector2(LockfreeVector2&& other) = delete;

public:
    LockfreeVector2(uint32_t n) : cursor(0), memory(n) { }

    ~LockfreeVector2() { }

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