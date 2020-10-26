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

#ifndef Lockfree_VECTOR9
#define Lockfree_VECTOR9

#include <cstdlib>
#include <cstring> 
#include <atomic>
#include <memory>
#include <vector>

/**
 * T is the content type and must be integral
 * N elements per page
 * S sentinel element
 * B counter bits, assert B <= 16, N < 2^B
 * */
template<typename T = uint32_t, unsigned int N = 1000, T S = 0, unsigned int B = 16>
class LockfreeVector9 {
public:
    class const_iterator {
        T* pos;
        T** cpe; // current page end

        inline void hop() { 
            // hop from cpe to next page begin
            if (pos == (T*)cpe) { 
                pos = *cpe; 
                if (pos != nullptr) cpe = (T**)(pos + N); 
            }
        }

    public:
        const_iterator(T* mem) : pos(mem), cpe((T**)(mem + N)) { }
        ~const_iterator() { }

        inline const T operator * () { 
            return *pos; 
        }

        inline const_iterator& operator ++ () { 
            do {
                ++pos; 
                hop();
            } while (pos != nullptr && *pos == S);
            return *this; 
        }

        inline bool operator != (const const_iterator& other) { // page end and next page begin are equal
            return pos != other.pos && (pos != (T*)cpe || *cpe != other.pos);
        }

        inline bool operator == (const const_iterator& other) const {
            return !(*this != other);
        }
    };
    

private:
    T* memory;
    std::atomic<uintptr_t> pos;

    inline unsigned int get_index(uintptr_t pos) {
        return pos & ((1 << B) - 1);
    }

    inline T* get_page(uintptr_t pos) {
        return (T*)(pos >> B);
    }

    LockfreeVector9(LockfreeVector9 const&) = delete;
    void operator=(LockfreeVector9 const&) = delete;
    LockfreeVector9(LockfreeVector9&& other) = delete;

public:
    LockfreeVector9() {
        memory = (T*)std::malloc(N * sizeof(T) + sizeof(T*));
        pos.store((uintptr_t)memory << B, std::memory_order_relaxed);
        std::fill(memory, memory + N, S);
        T** cpe = (T**)(memory + N);
        *cpe = nullptr; // to glue the segments together
    }

    ~LockfreeVector9() { 
        T* mem = memory;
        while (mem != nullptr) {
            memory = *(T**)(mem + N);
            free(mem);
            mem = memory;
        }
    }

    void push(T value) {
        assert(value != S);
        while (true) {
            uintptr_t cur = pos.load(std::memory_order_acquire);
            unsigned int i = get_index(cur);
            if (i <= N) { // block pos++ during realloc (busy-loop)
                cur = pos.fetch_add(1, std::memory_order_acq_rel);
                i = get_index(cur);
                T* mem = get_page(cur);
                if (i < N) { 
                    mem[i] = value;
                    return;
                }
                else if (i == N) { // all smaller pos are allocated
                    T* fresh = (T*)std::malloc(N * sizeof(T) + sizeof(T*));
                    std::fill(fresh, fresh + N, S);
                    T** cpe = (T**)(fresh + N);
                    *cpe = nullptr;
                    //^^^^^^ until here it's uncritical
                    cpe = (T**)(mem + N);
                    *cpe = fresh; //now readers know about the new page
                    pos.store((uintptr_t)fresh << B, std::memory_order_release);
                } // loop to construct first element in new page
            }
        }
    }

    inline const_iterator begin() {
        return const_iterator(memory);
    }

    inline const_iterator end() {
        uintptr_t cur = pos.load(std::memory_order_acquire);
        unsigned int i = get_index(cur);
        T* mem = get_page(cur);
        if (i < N && (i == 0 || mem[i-1] != S)) { 
            return const_iterator(mem + i);
        }
        // we are either in realloc or the last value is still unconstructed
        // in that case, we just use the end of the last know page, indicated by nullptr 
        // we might have to skip all trailing unconstructed values in that case
        return const_iterator(nullptr);
    }

};

#endif