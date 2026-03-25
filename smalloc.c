#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdbool.h>
#include "smalloc.h"



typedef struct block{
    int block_size;
    int allocated;
    struct block *next_free;
    struct block *prev_free;
    char payload[];
} block_t;

uintptr_t heap_start_addr = 0;
block_t *head = NULL;

/*
 * my_init() is called one time by the application program to to perform any
 * necessary initializations, such as allocating the initial heap area.
 * size_of_region is the number of bytes that you should request from the OS using
 * mmap().
 * Note that you need to round up this amount so that you request memory in
 * units of the page size, which is defined as 4096 Bytes in this project.
 */
int my_init(int size_of_region) {
    int multiple = size_of_region / 4096;
    if (size_of_region % 4096 != 0) {
        multiple ++;
    } 
    int fd = open("/dev/zero", O_RDWR);
    if (fd == -1) {
        return -1;
    }
    void *mapped_space = mmap(NULL, multiple * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd,0);
    close(fd);
    if (mapped_space == MAP_FAILED) {
        return -1;
    }
    heap_start_addr = (uintptr_t) mapped_space;
    head = (block_t *) mapped_space;
    head->block_size = multiple * 4096;
    head->next_free = NULL;
    head->prev_free = NULL;
    head->allocated = 0;

    return 0;
}

/*
 * get_head_pointer() returns an offset to the start of head block (not the payload)
 * relative to the start of the heap.
 * This should be (unsigned long)head_pointer - (unsigned long)start_of_heap.
 * If there is no free block, return -1.
 */
int get_head_pointer(Pointer_Status *status) {
    if (head) {
        status->success = 3;
        status->block_size = head->block_size;
    } else {
        status->success = 2;
        status->block_size = -1;
    }
    return (uintptr_t)head - heap_start_addr;
}

/*
 * get_next_pointer() takes as input a pointer to the payload of a block
 * and returns an offset to the payload of the next free block in the
 * free list (relative to the start of the heap).
 * If there is no next free block, return -1.
 * NOTE: "curr" points to the start of the payload of a block.
 */
int get_next_pointer(void *curr) {
    char *ptr = (char *) curr;
    ptr -= 24;
    block_t *block = (block_t *)ptr;
    if (block->next_free == NULL) {
        return -1;
    }
    return (uintptr_t)block->next_free - heap_start_addr;

}

/*
 * get_prev_pointer() takes as input a pointer to the payload of a block
 * and returns an offset to the payload of the previous free block in the
 * free list (relative to the start of the heap).
 * If there is no previous free block, return -1.
 * NOTE: "curr" points to the start of the payload of a block.
 */
int get_prev_pointer(void *curr) {
    char *ptr = (char *) curr;
    ptr -= 24;
    block_t *block = (block_t *)ptr;
    if (block->prev_free == NULL) {
        return -1;
    }
    return (uintptr_t)block->prev_free - heap_start_addr;
}

/*
 * smalloc() takes as input the size in bytes of the payload to be allocated and
 * returns a pointer to the start of the payload. The function returns NULL if
 * there is not enough contiguous free space within the memory allocated
 * by my_init() to satisfy this request.
 */
void *smalloc(int size_of_payload, Malloc_Status *status) {
    bool found = false;
    int size_needed = size_of_payload + 24 + (8-size_of_payload%8)%8;
    block_t *prev = NULL;
    block_t * curr = head;
    void *payload_ptr = NULL;
    int hops = 0;
    while (curr != NULL && !found) {
        if (curr->block_size >= size_needed) {
            found = true;
            payload_ptr = (char *)curr + 24;
            curr->allocated = 1;
            int remaining_size = curr->block_size - size_needed;
            if (remaining_size < 32) {
                //not big enough for another alloc, so include as padding
                //(so no need to update block size)
                if (prev) {
                    //we need to remove this from the list, and attach
                    //prev to our next
                    prev->next_free = curr->next_free; 
                    if (curr->next_free != NULL) {        
                        curr->next_free->prev_free = curr->prev_free;
                    }
                } else {
                    //we're at the start, so our next becomes the new head
                    head = curr->next_free;
                    if (curr->next_free != NULL) {    
                        curr->next_free->prev_free = NULL;
                    }
                }
                
                curr->next_free = NULL;
                curr->prev_free = NULL;
            } else {
                //split block into two
                curr->block_size = size_needed;
                
                block_t *new_block_ptr = (block_t *) ((char *)curr + size_needed);
                new_block_ptr->allocated = 0;
                new_block_ptr->block_size = remaining_size;
                new_block_ptr->next_free = curr->next_free;
                if (prev) {
                    prev->next_free = new_block_ptr;
                    new_block_ptr->prev_free = curr->prev_free;
                } else {
                    //we're the new head
                    head = new_block_ptr;
                    new_block_ptr->prev_free = NULL;
                }
                curr->next_free = NULL;
                curr->prev_free = NULL;
            }
        } else {
            prev = curr;
            curr = curr->next_free;
            hops ++;
        }
    }
    
    if (found) {
        status->hops = hops;
        status->payload_offset = (uintptr_t)payload_ptr - heap_start_addr;
        status->success = 1;
        return payload_ptr;
    } else {
        status->hops = -1;
        status->payload_offset = -1;
        status->success = 0;
        return NULL;
    }
}


/*
 * check if a given block is adjacent to another block. 
 */
bool is_next_to(block_t *block_1, block_t *block_2) {
    uintptr_t addr_1 = (uintptr_t) block_1;
    uintptr_t addr_2 = (uintptr_t) block_2;
    int size_1 = block_1->block_size;
    int size_2 = block_2->block_size;

    if (addr_1 > addr_2) {
        if (addr_2 + size_2 == addr_1) {
            return true;
        } else {
            return false;
        }
    } else {
        if (addr_1 + size_1 == addr_2) {   
            return true;
        } else {
            return false;
        }
    }
}


//grabs block address
uintptr_t addr( block_t *block) {
    return (uintptr_t)block;
}

/*
 * sfree() frees the target block. "ptr" points to the start of the payload.
 * NOTE: "ptr" points to the start of the payload, rather than the block (header).
 */
void sfree(void *ptr) {
    if (ptr == NULL) {return;}
    block_t *allocated_block = (block_t *) ((char *)ptr - 24);
    uintptr_t alloc_block_addr = (uintptr_t)allocated_block;
    if (allocated_block->allocated == 0) {return;}

    bool found = false;
    block_t *prev = NULL;
    block_t *curr = head;
    if (curr == NULL && prev == NULL) {
        allocated_block->allocated = 0;
        allocated_block->prev_free = NULL;
        allocated_block->next_free = NULL;
        head = allocated_block;
        return;
    }
    while ((curr != NULL || prev != NULL) && !found) {
        if (curr) {
            //we are moving through the list, and haven't reached the end
            if (addr(curr) > alloc_block_addr && (!prev || addr(prev) < alloc_block_addr)) {
                //we've found our place
                found = true;
                bool coalesce_left = (prev != NULL && is_next_to(prev,allocated_block));
                bool coalesce_right = is_next_to(allocated_block,curr);
                
                allocated_block->allocated = 0;
                if (prev != NULL) {
                    block_t *working_unit = NULL;
                    if (coalesce_left) {
                        prev->block_size += allocated_block->block_size;
                        working_unit = prev;
                    } else {
                        prev->next_free = allocated_block;
                        allocated_block->prev_free = prev;
                        allocated_block->next_free = curr;
                        curr->prev_free = allocated_block;
                        working_unit = allocated_block;
                    }

                    if (coalesce_right) {
                        working_unit->block_size += curr->block_size;
                        working_unit->next_free = curr->next_free;
                        if (curr->next_free != NULL) {   
                            curr->next_free->prev_free = working_unit;
                        }
                    }
                } else {
                    //we're the new head block, and no need to coalesce left
                    head = allocated_block;
                    allocated_block->prev_free = NULL;

                    if (coalesce_right) {
                        allocated_block->block_size += curr->block_size;
                        allocated_block->next_free = curr->next_free;
                        if (curr->next_free != NULL) {   
                            curr->next_free->prev_free = allocated_block;
                        }
                    } else {
                        allocated_block->next_free = curr;
                        curr->prev_free = allocated_block;
                    }
                }
            } else {
                //continue searching
                prev = curr;
                curr = curr->next_free;
            }

        } else {
            //we're at the end of the list, and only have a prev
            if (addr(prev) < alloc_block_addr) {
                //we've found our place
                found = true;
                bool coalesce_left = is_next_to(prev,allocated_block);

                allocated_block->allocated = 0;
                if (coalesce_left) {
                    prev->block_size += allocated_block->block_size;
                } else {
                    prev->next_free = allocated_block;
                    allocated_block->prev_free = prev;
                    allocated_block->next_free = NULL;
                }

            } else {
                //terminate loop
                prev = NULL;
            }
        }
    }
    return;
}
