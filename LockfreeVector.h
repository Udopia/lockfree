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

template<class T = uint32_t>
class LockfreeVector {
public:
    class const_iterator {
        std::atomic<T>* pos;

        std::shared_ptr<std::atomic<T>> mem;

    public:
        const_iterator(std::shared_ptr<std::atomic<T>> memory_, uint32_t start) { 
            mem = std::atomic_load(&memory_);
            pos = mem.get() + start;
        }

        ~const_iterator() { }

        inline const T operator * () const {
            return pos->load();
        }

        inline const_iterator& operator ++ () {
            ++pos;            
            return *this;
        }

        inline bool operator == (const const_iterator& other) const {
            return pos == other.pos;
        }

        inline bool operator != (const const_iterator& other) const {
            return pos != other.pos;
        }

        inline bool operator < (const const_iterator& other) const {
            return pos < other.pos;
        }

    };

private:
    // Cursor and memory support atomic access: for lock-free insert
    // Pointer to memory is managed: for iterator-validity on realloc (make sure to use atomic-load when fetching the shared-ptr)
    std::shared_ptr<std::atomic<T>> memory;
    std::atomic<uint32_t> cursor;

    uint32_t capacity_; // capacity is safe by realloc mutex

    // Realloc needs mutex
    std::mutex write;

    uint32_t get_pos_or_grow() {
        uint32_t pos = this->cursor.load();
        if (pos == capacity_) {
            write.lock();
            if (pos == capacity_) {
                std::atomic<T>* field = (std::atomic<T>*)std::calloc(capacity_ * 2, sizeof(T));
                std::memcpy((void*)field, (void*)this->memory.get(), capacity_ * sizeof(T));
                std::atomic_store(&memory, std::shared_ptr<std::atomic<T>>(field, std::free));
                capacity_ *= 2;
            }
            write.unlock();
        }
        return pos;
    }

    LockfreeVector(LockfreeVector const&) = delete;
    void operator=(LockfreeVector const&) = delete;
    LockfreeVector(LockfreeVector&& other) = delete;

public:
    LockfreeVector(uint32_t n) : capacity_(n), cursor(0), memory() {
        std::atomic<T>* field = (std::atomic<T>*)std::calloc(capacity_, sizeof(T));
        memory = std::shared_ptr<std::atomic<T>>(field, std::free);
    }

    ~LockfreeVector() { }

    inline uint32_t capacity() const {
        return capacity_;
    }

    inline uint32_t size() const {
        return cursor.load();
    }

    void push(T value) {
        T sentinel = 0;
        uint32_t pos = get_pos_or_grow();
        std::shared_ptr<std::atomic<T>> mem = std::atomic_load(&memory);
        while (!mem.get()[pos].compare_exchange_weak(sentinel, value)) {
            sentinel = 0;
            pos = get_pos_or_grow();
            mem = std::atomic_load(&memory);
        }
        this->cursor.fetch_add(1);
    }

    inline const T operator [] (uint32_t pos) const {
        assert(pos < size());
        return std::atomic_load(&memory).get()[pos].load();
    }

    inline const_iterator begin() const {
        return const_iterator(memory, 0);
    }

    inline const_iterator end() const {
        return const_iterator(memory, size());
    }

};

#endif