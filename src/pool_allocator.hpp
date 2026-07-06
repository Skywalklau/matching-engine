#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

// ---------------------------------------------------------------------------
// PoolAllocator<T, N>
//
// A fixed-capacity memory pool for objects of type T.
// Storage is heap-allocated so large N values do not overflow the stack.
//
// Design:
//   - Two heap arrays: object storage (N slots) and a free-list stack.
//   - allocate()  — pops from the free list: O(1), zero heap involvement.
//   - deallocate()— pushes to the free list: O(1), zero heap involvement.
//   - construct() — allocate + placement new.
//   - destroy()   — destructor + deallocate.
//
// Constraints:
//   - Not thread-safe. The matching engine is single-threaded on the hot path.
//   - Fixed capacity. allocate() returns nullptr if the pool is exhausted.
//   - Non-copyable, non-movable.
// ---------------------------------------------------------------------------

template <typename T, size_t N>
class PoolAllocator {
public:
    static_assert(N > 0, "Pool capacity must be positive");

    PoolAllocator(): storage_(new Slot[N]), free_list_(new size_t[N]) {
        for (size_t i = 0; i < N; ++i) free_list_[i] = i;
        top_ = N;
    }

    ~PoolAllocator() = default;

    PoolAllocator(const PoolAllocator&)            = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&)                 = delete;
    PoolAllocator& operator=(PoolAllocator&&)      = delete;

    // Returns raw uninitialised memory for one T. Prefer construct().
    [[nodiscard]] T* allocate() noexcept {
        if (top_ == 0) return nullptr;
        const size_t idx = free_list_[--top_];
        return reinterpret_cast<T*>(&storage_[idx]);
    }

    // Returns a slot to the pool. Does NOT call T's destructor.
    void deallocate(T* ptr) noexcept {
        assert(ownsPointer(ptr) && "Pointer does not belong to this pool");
        free_list_[top_++] = slotIndex(ptr);
    }

    // Allocate + placement new. Not noexcept — T's constructor may throw.
    template <typename... Args>
    [[nodiscard]] T* construct(Args&&... args) { // Args&& is a forward reference (maintains lvalue/rvalue info)
        T* ptr = allocate();
        if (!ptr) return nullptr;
        try {
            // even if you passed an rvalue, args has a name now
            // so args is treated as an lvalue here
            // foward looks up whether args was originally lvalue or rvalue
            // casts it back to what it originally was
            new (ptr) T(std::forward<Args>(args)...);
        } catch (...) {
            deallocate(ptr);
            throw;
        }
        return ptr;
    }

    // Destructor + deallocate.
    void destroy(T* ptr) noexcept {
        assert(ptr != nullptr);
        ptr->~T();
        deallocate(ptr);
    }

    size_t capacity()  const noexcept { return N; }
    size_t available() const noexcept { return top_; }
    size_t used()      const noexcept { return N - top_; }
    bool   empty()     const noexcept { return top_ == N; }
    bool   full()      const noexcept { return top_ == 0; }

private:
    struct alignas(alignof(T)) Slot {
        unsigned char data[sizeof(T)];
    };

    bool ownsPointer(const T* ptr) const noexcept {
        const auto p     = reinterpret_cast<uintptr_t>(ptr);
        const auto begin = reinterpret_cast<uintptr_t>(storage_.get());
        const auto end   = reinterpret_cast<uintptr_t>(storage_.get() + N);
        return p >= begin && p < end;
    }

    size_t slotIndex(const T* ptr) const noexcept {
        const ptrdiff_t diff =
            reinterpret_cast<const Slot*>(ptr) - storage_.get(); // pointer arithmetic divides by element size automatically
        assert(diff >= 0 && "Slot index must be non-negative");
        return static_cast<size_t>(diff);
    }

    std::unique_ptr<Slot[]>   storage_;
    std::unique_ptr<size_t[]> free_list_;
    size_t                    top_;
};