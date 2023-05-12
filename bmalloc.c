#include <unistd.h>
#include <stdio.h>
#include "bmalloc.h"
#include <sys/mman.h> // for mmap and munmap
#include <stdint.h> // for uintptr_t
#include <string.h> // for memcpy

//bm_mode = tFit ; //BestFit --> Ffit
bm_option bm_mode = BestFit;
bm_header bm_list_head = {0, 0, 0x0 } ;

void * sibling (void * h)
{
    size_t block_size = 1 << ((bm_header_ptr) h - 1)->size;
    return (void *) ((uintptr_t) h ^ block_size);
}

int fitting (size_t s)
{
        // Round up s to the nearest power of 2
    size_t power_of_two = 1;
    while (power_of_two < s) {
        power_of_two <<= 1;
    }
    // Determine the size field value for a block of size power_of_two
    int size = 0;
    while ((power_of_two >> size) > 1) {
        size++;
    }
    return size;
}

void * bmalloc (size_t s)
{
        // Determine the size of the block needed to accommodate s bytes
    int block_size = fitting(s);

    // Traverse the list of free blocks to find a fitting block
    bm_header_ptr curr, prev;
    for (curr = &bm_list_head, prev = NULL; curr != NULL; prev = curr, curr = curr->next) {
        if (!curr->used && curr->size >= block_size) {
            // Found a fitting block; split it if necessary
            if (curr->size > block_size) {
                bm_header_ptr buddy = (bm_header_ptr) (((char *) curr) + (1 << (curr->size - 1)));
                buddy->used = 0;
                buddy->size = curr->size - 1;
                buddy->next = curr->next;
                curr->next = buddy;
            }
            curr->used = 1;
            return ((char *) curr) + sizeof(bm_header);
        }
    }

    // No fitting block found; allocate a new page
    int page_size = 1 << 12;
    bm_header_ptr page = (bm_header_ptr) mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (page == MAP_FAILED) {
        return NULL;
    }
    page->used = 0;
    page->size = 12;
    page->next = bm_list_head.next;
    bm_list_head.next = page;

    // Split the new block if necessary and mark it as used
    if (page->size > block_size) {
        bm_header_ptr buddy = (bm_header_ptr) (((char *) page) + (1 << (page->size - 1)));
        buddy->used = 0;
        buddy->size = page->size - 1;
        buddy->next = page->next;
        page->next = buddy;
    }
    page->used = 1;
    return ((char *) page) + sizeof(bm_header);
}

void bfree (void * p)
{
        bm_header_ptr h = (bm_header_ptr) (p - sizeof(bm_header));
    h->used = 0;

    bm_header_ptr s = sibling(h);
    while (s && !s->used && s->size == h->size) {
        // merge h with its sibling s
        if (s < h) {
            bm_header_ptr temp = h;
            h = s;
            s = temp;
        }
        h->size += 1;
        h->next = s->next;
        s = sibling(h);
    }

    // remove any page that has no used block
    bm_header_ptr cur = &bm_list_head;
    while (cur->next) {
        bm_header_ptr next = cur->next;
        if (cur->next->size == 12 && !cur->next->used) {
            cur->next = next->next;
            munmap((void *) next, 1 << 12);
        } else {
            cur = cur->next;
        }
    }
}

void * brealloc (void * p, size_t s)
{
        bm_header_ptr hdr = ((bm_header_ptr) p) - 1 ;
        size_t block_size = 1 << hdr->size ;
        void * new_ptr = p ;

        // if p is NULL, allocate a new block of size s
        if (p == NULL) {
                new_ptr = bmalloc(s) ;
        }
        // if s is zero, free the block and return NULL
        else if (s == 0) {
                bfree(p) ;
                new_ptr = NULL ;
        }
        // if the new size is smaller than the current block size, split the block
        else if (s < block_size - sizeof(bm_header)) {
                size_t new_size = fitting(s) ;
                if (new_size != hdr->size) {
                        bfree((void *) hdr + sizeof(bm_header) + (1 << new_size)) ;
                }
        }
        // if the new size is larger than the current block size, allocate a new block and copy the data
        else if (s > block_size - sizeof(bm_header)) {
                new_ptr = bmalloc(s) ;
                if (new_ptr != NULL) {
                        size_t copy_size = s < block_size - sizeof(bm_header) ? s : block_size - sizeof(bm_header) ;
                        memcpy(new_ptr, p, copy_size) ;
                        bfree(p) ;
                }
        }
        return new_ptr ;
}

void bmconfig (bm_option opt)
{
        bm_mode = opt;
}

void
bmprint ()
{
        bm_header_ptr itr ;
    int i, used_blocks = 0, total_blocks = 0, used_memory = 0, total_memory = 0, available_memory = 0, fragmentation = 0 ;

    printf("==================== bm_list ====================\n") ;
    for (itr = bm_list_head.next, i = 0 ; itr != 0x0 ; itr = itr->next, i++) {
        int block_size = 1 << itr->size;
        int payload_size = block_size - sizeof(bm_header);
        printf("%3d:%p:%1d %8d:%8d\n", i, ((void *) itr) + sizeof(bm_header), (int)itr->used, block_size, payload_size) ;

        total_blocks++;
        total_memory += block_size;
        if(itr->used) {
            used_blocks++;
            used_memory += payload_size;
        } else {
            available_memory += payload_size;
            fragmentation += payload_size % 8;
        }
    }
    printf("=================================================\n") ;
    printf("Total amount of memory: %d\n", total_memory);
    printf("Total amount of memory given to users: %d\n", used_memory);
    printf("Total amount of available memory: %d\n", available_memory);
    printf("Total amount of internal fragmentation: %d\n", fragmentation);
    printf("Total number of blocks: %d\n", total_blocks);
    printf("Total number of used blocks: %d\n", used_blocks);
    printf("Total number of available blocks: %d\n", total_blocks - used_blocks);
}