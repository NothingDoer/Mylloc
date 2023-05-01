#include "heap.h"
#include "tested_declarations.h"
#include "rdebug.h"

struct memory_manager_t memory_manager;

void heap_fences(struct memory_chunk_t *chunk) {
    for (int i = 0; i < FencesSize; i++) {
        *((char *) (chunk + 1) + i) = 0;
        *((char *) (chunk + 1) + chunk->size + FencesSize + i) = 0;
    }
}

int heap_setup(void) {
    memory_manager.memory_start = custom_sbrk(0);
    if (memory_manager.memory_start == (void *) -1) {
        return -1;
    }
    memory_manager.first_memory_chunk = NULL;
    memory_manager.last_memory_chunk = NULL;
    memory_manager.last_memory_byte = NULL;
    return 0;
}

void heap_clean(void) {
    custom_sbrk((long) -((char *) memory_manager.last_memory_byte - (char *) memory_manager.first_memory_chunk));
    memory_manager.memory_start = NULL;
    memory_manager.first_memory_chunk = NULL;
    memory_manager.last_memory_chunk = NULL;
    memory_manager.last_memory_byte = NULL;
}

void *heap_malloc(size_t size) {
    if (heap_validate() || size < 1) {
        return NULL;
    }

    struct memory_chunk_t *prev_chunk, *found_chunk;

    prev_chunk = found_chunk = memory_manager.first_memory_chunk;
    while (found_chunk) {
        if (found_chunk->free && found_chunk->size >= size) {
            break;
        }
        prev_chunk = found_chunk;
        found_chunk = found_chunk->next;
    }

    if (found_chunk) {
        found_chunk->size = size;
        found_chunk->free = 0;
        heap_fences(found_chunk);
        return (char *) (found_chunk + 1) + FencesSize;
    }

    found_chunk = (struct memory_chunk_t *) custom_sbrk((long) (size + sizeof(struct memory_chunk_t) + FencesSize * 2));
    if (found_chunk == (void *) -1) {
        return NULL;
    }
    memory_manager.last_memory_chunk = found_chunk;
    memory_manager.last_memory_byte = (char *) found_chunk + sizeof(struct memory_chunk_t) + size + 2 * FencesSize;
    found_chunk->size = size;
    found_chunk->free = 0;
    found_chunk->next = NULL;
    found_chunk->prev = prev_chunk;
    if (prev_chunk != NULL) {
        prev_chunk->next = found_chunk;
    } else {
        memory_manager.first_memory_chunk = found_chunk;
    }
    heap_fences(found_chunk);

    return (char *) (found_chunk + 1) + FencesSize;
}

void *heap_calloc(size_t number, size_t size) {
    char *chunk = heap_malloc(number * size);
    if (chunk == NULL) {
        return NULL;
    }
    for (unsigned long i = 0; i < (number * size); i++) {
        *(chunk + i) = 0;
    }
    return chunk;
}

void *heap_realloc(void *memblock, size_t count) {
    if (heap_validate()) {
        return NULL;
    }

    if (count < 1) {
        heap_free(memblock);
        return NULL;
    }

    if (get_pointer_type(memblock) == pointer_null) {
        memblock = heap_malloc(count);
        return memblock;
    }

    if (get_pointer_type(memblock) != pointer_valid) {
        return NULL;
    }

    struct memory_chunk_t *chunk = (struct memory_chunk_t *) ((char *) ((struct memory_chunk_t *) memblock - 1) -
                                                              FencesSize);
    struct memory_chunk_t *newChunk;
    struct memory_chunk_t *nextChunk = chunk->next;

    if (count == chunk->size) {
        return memblock;
    }

    if (count < chunk->size) {
        chunk->size = count;
        heap_fences(chunk);
        return memblock;
    }

    if (nextChunk == NULL) {
        newChunk = (struct memory_chunk_t *) custom_sbrk((long) (count - chunk->size));
        if (newChunk == (void *) -1) {
            return NULL;
        }
        chunk->size = count;
        heap_fences(chunk);
        memory_manager.last_memory_byte =
                (char *) chunk + sizeof(struct memory_chunk_t) + chunk->size + 2 * FencesSize;
        return memblock;
    }

    if (nextChunk->free && (char *) nextChunk - (char *) chunk + nextChunk->size >= count - chunk->size) {
        nextChunk->next->prev = chunk;
        chunk->next = nextChunk->next;
        chunk->size = count;
        heap_fences(chunk);
        return memblock;
    }

    newChunk = heap_malloc(count);
    if (newChunk == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < chunk->size; i++) {
        *((char *) newChunk + i) = *((char *) (chunk + 1) + FencesSize + i);
    }

    heap_free(memblock);
    return newChunk;
}

void delete_frees() {
    struct memory_chunk_t *chunk = memory_manager.first_memory_chunk;
    struct memory_chunk_t *next_chunk;

    while(chunk)
    {
        next_chunk = chunk->next;
        while (next_chunk && chunk->free && next_chunk->free) {
            chunk->size += next_chunk->size + sizeof(struct memory_chunk_t) + 2 * FencesSize;
            next_chunk = next_chunk->next;
            if(next_chunk)
            {
                next_chunk->prev = chunk;
            }
            chunk->next = next_chunk;
            if(next_chunk == NULL)
            {
                memory_manager.last_memory_chunk = chunk;
            }
        }
        chunk = chunk->next;
    }

}

void delete_end_memory()
{
    struct memory_chunk_t *chunk = memory_manager.last_memory_chunk;
    struct memory_chunk_t *prev_chunk;
    char * new_last_memory_byte;

    while(chunk && chunk->free){
        prev_chunk = chunk->prev;
        if(prev_chunk == NULL)
        {
            custom_sbrk((long) -(memory_manager.last_memory_byte - (char *)memory_manager.memory_start));
            memory_manager.last_memory_byte = NULL;
            memory_manager.first_memory_chunk = NULL;
            memory_manager.last_memory_chunk = NULL;
            return;
        }
        new_last_memory_byte = (char *)prev_chunk + sizeof(struct memory_chunk_t) + prev_chunk->size + 2 * FencesSize;
        custom_sbrk((long) -(memory_manager.last_memory_byte - new_last_memory_byte));
        memory_manager.last_memory_byte = new_last_memory_byte;
        prev_chunk->next = NULL;
        chunk = prev_chunk;
        memory_manager.last_memory_chunk = chunk;
    }
}

void heap_free(void *memblock) {
    if (get_pointer_type(memblock) != pointer_valid) {
        return;
    }

    struct memory_chunk_t *chunk = memory_manager.first_memory_chunk;

    while (chunk && chunk != (struct memory_chunk_t *) ((char *) memblock - FencesSize) - 1) {
        chunk = chunk->next;
    }

    if (chunk == NULL) {
        return;
    }

    chunk->free = 1;
    if (chunk->next) {
        chunk->size = (char *) chunk->next - (char *) chunk - sizeof(struct memory_chunk_t) - 2 * FencesSize;
    }
    delete_frees();
    delete_end_memory();
}

size_t heap_get_largest_used_block_size(void) {
    if (heap_validate()) {
        return 0;
    }
    size_t size = 0;
    struct memory_chunk_t *chunk = memory_manager.first_memory_chunk;

    while (chunk != NULL) {
        if (!chunk->free && chunk->size > size) {
            size = chunk->size;
        }
        chunk = chunk->next;
    }

    return size;
}

enum pointer_type_t get_pointer_type(const void *const pointer) {
    if (pointer == NULL) {
        return pointer_null;
    }

    if (heap_validate()) {
        return pointer_heap_corrupted;
    }

    char *chunk = (char *) memory_manager.first_memory_chunk;
    const char *const charPointer = pointer;

    while (chunk != NULL) {
        if (((struct memory_chunk_t *) chunk)->free) {
            chunk = (char *) ((struct memory_chunk_t *) chunk)->next;
            continue;
        }
        if (charPointer >= chunk && charPointer < chunk + sizeof(struct memory_chunk_t)) {
            return pointer_control_block;
        }
        if (charPointer >= chunk + sizeof(struct memory_chunk_t) &&
            charPointer < chunk + sizeof(struct memory_chunk_t) + FencesSize) {
            return pointer_inside_fences;
        }
        if (charPointer == chunk + sizeof(struct memory_chunk_t) + FencesSize) {
            return pointer_valid;
        }
        if (charPointer > chunk + sizeof(struct memory_chunk_t) + FencesSize &&
            charPointer <
            chunk + sizeof(struct memory_chunk_t) + FencesSize + ((struct memory_chunk_t *) chunk)->size) {
            return pointer_inside_data_block;
        }
        if (charPointer >=
            chunk + sizeof(struct memory_chunk_t) + FencesSize + ((struct memory_chunk_t *) chunk)->size &&
            charPointer <
            chunk + sizeof(struct memory_chunk_t) + 2 * FencesSize + ((struct memory_chunk_t *) chunk)->size) {
            return pointer_inside_fences;
        }

        chunk = (char *) ((struct memory_chunk_t *) chunk)->next;
    }

    return pointer_unallocated;
}

int heap_validate(void) {
    if (memory_manager.memory_start == NULL) {
        return 2;
    }
    struct memory_chunk_t *chunk = memory_manager.first_memory_chunk;
    struct memory_chunk_t *prev_chunk, *next_chunk;

    while (chunk != NULL) {
        if (chunk->free != 0 && chunk->free != 1) {
            return 3;
        }
        if (((char *) (chunk + 1) + chunk->size + FencesSize * 2) > memory_manager.last_memory_byte) {
            return 3;
        }
        if (chunk->free == 0) {
            for (int i = 0; i < FencesSize; i++) {
                if (*((char *) (chunk + 1) + i) != 0 || *((char *) (chunk + 1) + chunk->size + FencesSize + i) != 0) {
                    return 1;
                }
            }
        }
        prev_chunk = chunk->prev;
        next_chunk = chunk->next;
        if ((prev_chunk != NULL &&
             (prev_chunk < memory_manager.first_memory_chunk || prev_chunk > memory_manager.last_memory_chunk)) ||
            (next_chunk != NULL &&
             (next_chunk < memory_manager.first_memory_chunk || next_chunk > memory_manager.last_memory_chunk))) {
            return 3;
        }
        if (prev_chunk != NULL && prev_chunk->next != chunk) {
            return 3;
        }
        if (next_chunk != NULL && next_chunk->prev != chunk) {
            return 3;
        }
        chunk = chunk->next;
    }

    return 0;
}
