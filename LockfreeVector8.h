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

#ifndef Lockfree_VECTOR8
#define Lockfree_VECTOR8

#include <cstdlib>
#include <cstring> 
#include <atomic>
#include <memory>
#include <vector>

/**
 * T is the content type and must be integral
 * N elements per page
 * */
template<typename T = uint32_t, unsigned int N = 1000>
class LockfreeVector8 {
public:
    class const_iterator {
        T* pos;
        T** cpe;

    public:
        const_iterator(T* mem) : pos(mem), cpe((T**)(mem + N)) { }
        ~const_iterator() { }

        inline const T operator * () const { 
            return pos < (T*)cpe ? *pos : **cpe; 
        }

        inline const_iterator& operator ++ () { 
            if (pos == (T*)cpe && *cpe != nullptr) { 
                pos = *cpe; 
                cpe = (T**)(pos + N); 
            }
            ++pos; 
            return *this; 
        }

        inline bool operator != (const const_iterator& other) {
            return pos != other.pos && (pos != (T*)cpe || *cpe != other.pos);
        }

        inline bool operator == (const const_iterator& other) const {
            return pos == other.pos || (pos == (T*)cpe && *cpe == other.pos);
        }
    };
    

private:
    T* memory;
    std::atomic<T*> pos;
    T** cpe; // current page end

    LockfreeVector8(LockfreeVector8 const&) = delete;
    void operator=(LockfreeVector8 const&) = delete;
    LockfreeVector8(LockfreeVector8&& other) = delete;

public:
    LockfreeVector8() {
        memory = (T*)std::malloc(N * sizeof(T) + sizeof(T*));
        pos.store(memory, std::memory_order_relaxed);
        cpe = (T**)(memory + N);
        *cpe = nullptr; // to glue the segments together
    }

    ~LockfreeVector8() { 
        T* mem = memory;
        while (mem != nullptr) {
            memory = *(T**)(mem + N);
            free(mem);
            mem = memory;
        }
    }

    inline unsigned int size() const {
        return pos.load(std::memory_order_relaxed);
    }

    inline bool valid_position(T* pos, T* end) {
        return pos >= end - N && pos < end;
    }

    void push(T value) {
        while (true) {
            T* cur = pos.load(std::memory_order_acquire);
            if (cur <= (T*)cpe) { // G
                T* cpe_ = (T*)cpe;
                cur = pos.fetch_add(1, std::memory_order_acq_rel);
                if (valid_position(cur, (T*)cpe_)) { // G
                    *cur = value;
                    return;
                }
                else if (cur == (T*)cpe) { // G
                    T* fresh = (T*)std::malloc(N * sizeof(T) + sizeof(T*));
                    T** fresh_end = (T**)(fresh + N);
                    *fresh_end = nullptr;
                    *cpe = fresh;
                    cpe = nullptr; // lock Gs
                    pos.store(fresh, std::memory_order_release);
                    cpe = fresh_end; // unlock Gs
                }
            }
        }
    }

    inline const_iterator begin() {
        return const_iterator(memory);
    }

    inline bool valid_position2(T* pos, T* end) {
        return pos >= end - N && pos <= end;
    }

    inline const_iterator end() {
        T* pos_ = pos.load(std::memory_order_acquire);
        while (!valid_position2(pos_, (T*)cpe)) {
            pos_ = pos.load(std::memory_order_acquire);
        }
        return const_iterator(pos_);
    }

};

#endif