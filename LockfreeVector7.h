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

#ifndef Lockfree_VECTOR7
#define Lockfree_VECTOR7

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
class LockfreeVector7 {
public:
    struct alignas(2*sizeof(void*)) cursor_t {
        T* pos;
        T** end;
    };

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

        inline bool operator != (const const_iterator& other) const {
            return pos != other.pos && (pos != (T*)cpe || *cpe != other.pos);
        }

        inline bool operator == (const const_iterator& other) const {
            return pos == other.pos || (pos == (T*)cpe && *cpe == other.pos);
        }
    };
    

private:
    alignas(2*sizeof(void*)) std::atomic<cursor_t> cursor;
    T* memory;

    LockfreeVector7(LockfreeVector7 const&) = delete;
    void operator=(LockfreeVector7 const&) = delete;
    LockfreeVector7(LockfreeVector7&& other) = delete;

public:
    LockfreeVector7() {
        assert(sizeof(T*) % sizeof(T) == 0); // protects naive implementation
        memory = (T*)std::malloc(N * sizeof(T) + sizeof(T*));
        T** cpe = (T**)(memory + N);
        *cpe = nullptr; // to glue the segments together
        cursor.store({ memory, cpe }, std::memory_order_relaxed);
    }

    ~LockfreeVector7() { 
        T* mem = memory;
        while (mem != nullptr) {
            memory = *(T**)(mem + N);
            free(mem);
            mem = memory;
        }
    }

    inline unsigned int size() const {
        return cursor.load(std::memory_order_relaxed).pos;
    }

    void push(T value) {
        while (true) {
            cursor_t cur = cursor.load(std::memory_order_relaxed);
            if (cur.pos <= (T*)cur.end && cursor.compare_exchange_weak(cur, { cur.pos + 1, cur.end }, std::memory_order_acq_rel)) {
                if (cur.pos < (T*)cur.end) {
                    *cur.pos = value;
                    return;
                }
                else if (cur.pos == (T*)cur.end) {
                    T* fresh = (T*)std::malloc(N * sizeof(T) + sizeof(T*));
                    T** fresh_end = (T**)(fresh + N);
                    *fresh_end = nullptr;
                    //std::cout << "ATOMIC STORE: cur.pos=" << fresh << ", cur.end=" << fresh_end << std::endl;
                    *cur.end = fresh;
                    cursor.store({ fresh, fresh_end }, std::memory_order_release);
                }
            }
        }
    }

    inline const_iterator begin() {
        return const_iterator(memory);
    }

    inline const_iterator end() {
        cursor_t cur = cursor.load(std::memory_order_acquire);        
        return const_iterator(cur.pos > (T*)cur.end ? (T*)cur.end : cur.pos);
    }

};

#endif