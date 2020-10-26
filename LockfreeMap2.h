/*************************************************************************************************
LockfreeMap2 -- Copyright (c) 2020, Markus Iser, KIT - Karlsruhe Institute of Technology

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

#ifndef Lockfree_Map2
#define Lockfree_Map2

#include <cstdlib>
#include <cstring> 
#include <atomic>
#include <mutex>
#include <memory>

/**
 * T is the content type and must be integral
 * N elements per page
 * S sentinel element
 * */
template<typename T = uint32_t, unsigned int N = 1000, T S = 0>
class LockfreeMap2 {
public:
    class const_iterator {
        T* pos;
        T** cpe; // current page end

        inline void hop() { 
            // hop from cpe to next page begin
            if (pos == (T*)cpe && *cpe != nullptr) { 
                pos = *cpe; 
                cpe = (T**)(pos + N); 
            }
        }

    public:
        const_iterator(T* mem) : pos(mem), cpe((T**)(mem + N)) { }
        ~const_iterator() { }

        inline const T operator * () { 
            hop(); 
            while (*pos == S) this->operator++();
            return *pos; 
        }

        inline const_iterator& operator ++ () { 
            hop(); 
            ++pos; 
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
    class LockfreeVector8 {
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
            std::fill(memory, (T*)cpe, S);
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

        void push(T value) {
            assert(value != S);
            while (true) {
                T* cur = pos.load(std::memory_order_acquire);
                if (cur <= (T*)cpe) { // block pos++ during realloc busy-loop
                    T* cpe_ = (T*)cpe;
                    cur = pos.fetch_add(1, std::memory_order_acq_rel); // careful: threads can stall here during realloc
                    if (cur < (T*)cpe_ && cur >= cpe_ - N) { // cur and cpe must fit together
                        // during realloc fresh and fresh_end must both be set together
                        // which is the reason for the full interval check above.
                        // the alternative would be a double-word CAS which is much less efficient
                        *cur = value;
                        return;
                    }
                    else if (cur == (T*)cpe) { // all smaller pos are allocated
                        T* fresh = (T*)std::malloc(N * sizeof(T) + sizeof(T*));
                        T** fresh_end = (T**)(fresh + N);
                        std::fill(fresh, (T*)fresh_end, S);
                        *fresh_end = nullptr;
                        //^^^^^^ until here it's uncritical
                        *cpe = fresh; //now readers know about the new page
                        //cpe = nullptr; // lock Gs (otherwise values can get lost)
                        //^^ the above is now obsolete due to the full interval check after pos++, 
                        //which does also capture possibly stalled threads right before pos++,
                        //that rare case was not captured by the above "lock by minimum value"
                        pos.store(fresh, std::memory_order_acq_rel);
                        cpe = fresh_end; // unlock Gs
                    } // loop to construct first element in new page
                }
            }
        }

        inline const_iterator begin() {
            return const_iterator(memory);
        }

        inline bool valid_position2(T* pos, T* end) {
            return pos > end - N && pos <= end;
        }

        inline const_iterator end() {
            T* pos_ ;
            do {
                pos_ = pos.load(std::memory_order_acquire);
                while (!valid_position2(pos_, (T*)cpe)) { 
                    // pos can be invalid during realloc
                    pos_ = pos.load(std::memory_order_acquire);
                }
                while (*(pos_-1) == S && valid_position2(pos_-1, (T*)cpe)) { 
                    // make sure to end after a constructed element (for termination)
                    // as iterator runs over unconstructed elements
                    pos_--; 
                }
            } while (*(pos_-1) == S);
            return const_iterator(pos_);
        }
    };

    LockfreeVector8* map; 
    const unsigned int size_;

    uintptr_t arena;
    uintptr_t eoa; // end of arena

    LockfreeMap2(LockfreeMap2 const&) = delete;
    void operator=(LockfreeMap2 const&) = delete;
    LockfreeMap2(LockfreeMap2&& other) = delete;

public:
    LockfreeMap2(unsigned int n) : size_(n) {
        map = (LockfreeVector8*)std::calloc(size_, sizeof(LockfreeVector8));
        for (unsigned int i = 0; i < size_; i++) {
            new ((void*)(&map[i])) LockfreeVector(n);
        }
    }

    ~LockfreeMap2() { 
        for (unsigned int i = 0; i < size_; i++) free(map[i].memory);
        free(map);
    }

    unintptr_t pagebytes() {
        return N * sizeof(T) + sizeof(T*);
    }

    T* allocate() {
        while (true) { // busy loop on realloc only
            if (arena - eoa > pagebytes()) {
                arena += pagebytes();
                return arena;
            }
        }
    }

    unsigned int size() const {
        return size_;
    }

    void push(T key, T value) {
        T* ptr = map[key].push(value);
        if (ptr != nullptr) safe_free(ptr);
    }

    inline const_iterator iter(T key, unsigned int thread_id) {
        assert(hazards[thread_id] == nullptr); // at most one iterator per thread-id
        return map[key].iter(hazards[thread_id]);
    }

};

#endif