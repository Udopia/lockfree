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
    
    struct cursor_sep_t {
        std::atomic<T*> pos;
        std::atomic<T**> end;
    };

    static_assert(sizeof(std::atomic<cursor_t>) == sizeof(cursor_sep_t));

    class const_iterator {
        T* pos;
        T** cpe;

    public:
        const_iterator(T* mem) : pos(mem), cpe((T**)(mem + N)) { }
        ~const_iterator() { }

        inline const T operator * () const { 
            return *pos; 
        }

        inline const_iterator& operator ++ () { 
            ++pos; 
            if (pos == (T*)cpe) {//} && *cpe != nullptr) { 
                pos = *cpe; 
                cpe = (T**)(pos + N); 
            }
            return *this; 
        }

        inline bool operator != (const const_iterator& other) const {
            std::cout << pos << " " << other.pos << std::endl;
            return pos != other.pos;
        }

        inline bool operator == (const const_iterator& other) const {
            std::cout << pos << " " << other.pos << std::endl;
            return pos == other.pos;
        }
    };
    

private:
    std::atomic<T*> reader;
    T* memory;

    //union {
    //   alignas(2*sizeof(void*)) atomic<cursor_t> cursor;
    //    cursor_sep_t cursor_sep;
    //};
    std::atomic<T*> cursor alignas(2*sizeof(void*));
    T** cpe; //current page end

    LockfreeVector7(LockfreeVector7 const&) = delete;
    void operator=(LockfreeVector7 const&) = delete;
    LockfreeVector7(LockfreeVector7&& other) = delete;

public:
    LockfreeVector7() {
        assert(sizeof(T*) % sizeof(T) == 0); // protects naive implementation
        memory = (T*)std::malloc(N * sizeof(T) + sizeof(T*));
        cursor.store(memory, std::memory_order_relaxed);
        reader.store(memory, std::memory_order_relaxed);
        cpe = (T**)(memory + N);
        *cpe = nullptr; // to glue the segments together
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
        return cursor.load(std::memory_order_relaxed);
    }

    void push(T value) {
        while (true) {
            std::atomic_thread_fence(std::memory_order_acquire);
            T* end = (T*)cpe;
            T* begin = end - N;
            T* cur = cursor.load(std::memory_order_acquire);
            if (cur <= (T*)cpe) {
                T* pos = cursor.fetch_add(1, std::memory_order_acquire);
                if (begin <= pos && pos < end) { 
                    *pos = value;
                    // T* expect = pos;
                    // while (!reader.compare_exchange_weak(expect, pos+1)) expect = pos;
                    return;
                }
                else if (pos == (T*)cpe) {
                    T* fresh = (T*)std::malloc(N * sizeof(T) + sizeof(T*));
                    T** fresh_cpe = (T**)(&fresh[N]);
                    *fresh_cpe = nullptr;
                    *cpe = fresh;
                    // Critical section:
                    cpe = nullptr;
                    cursor.store(fresh, std::memory_order_release);
                    cpe = fresh_cpe;
                    std::atomic_thread_fence(std::memory_order_release);
                    // T* expect = pos;
                    // while (!reader.compare_exchange_weak(expect, fresh)) expect = pos;
                }
            } 
        }
    }

    inline const_iterator begin() {
        return const_iterator(memory);
    }

    inline const_iterator end() {
        return const_iterator(cursor.load(std::memory_order_acquire));
    }

};

#endif