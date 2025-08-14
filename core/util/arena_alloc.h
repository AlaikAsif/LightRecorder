#ifndef ARENA_ALLOC_H
#define ARENA_ALLOC_H

#include <cstddef>
#include <cstdlib>
#include <new>
#include <cassert>

class ArenaAllocator {
public:
    ArenaAllocator(size_t size) {
        arenaSize = size;
        arena = static_cast<char*>(std::malloc(arenaSize));
        assert(arena != nullptr);
        current = arena;
        end = arena + arenaSize;
    }

    ~ArenaAllocator() {
        std::free(arena);
    }

    void* allocate(size_t size) {
        if (current + size > end) {
            return nullptr; // Out of memory
        }
        void* result = current;
        current += size;
        return result;
    }

    void reset() {
        current = arena; // Reset the allocator to the start of the arena
    }

private:
    char* arena;
    char* current;
    char* end;
    size_t arenaSize;
};

#endif // ARENA_ALLOC_H