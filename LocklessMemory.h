
#include <cstdlib>
#include <cstring> 
#include <atomic>

template<class T = uint32_t>
class LocklessMemory {
    size_t page_size;
    std::atomic<size_t> cursor;
    std::atomic<T>* memory;

    LocklessMemory(LocklessMemory const&) = delete;
    void operator=(LocklessMemory const&) = delete;
    LocklessMemory(LocklessMemory&& other) = delete;

public:
    LocklessMemory(size_t n_elem) : page_size(n_elem * sizeof(T)), cursor(0) {
        memory = (std::atomic<T>*)std::calloc(n_elem, sizeof(T));
    }

    ~LocklessMemory() {
        if (memory != nullptr) {
            std::free((void*)memory);
            memory = nullptr;
        }
    }

    void push(T value) {
        T sentinel = 0;
        size_t pos = this->cursor.load();
        while (!memory[pos].compare_exchange_weak(sentinel, value)) {
            sentinel = 0;
            pos = this->cursor.load();
        }
        this->cursor.fetch_add(1);
    }

    T get(size_t pos) {
        assert(pos < cursor);
        return memory[pos].load();
    }

};