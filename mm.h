#ifndef __MM__
#define __MM__

#include <stdint.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include "gluethread/glthread.h"

#define MM_MAX_STRUCT_NAME 32

struct vm_page_family_;

typedef enum
{
    MM_FALSE,
    MM_TRUE
} vm_bool_t;

typedef struct block_meta_data_
{
    vm_bool_t is_free;
    uint32_t block_size;
    glthread_t priority_thread_glue;
    struct block_meta_data_ *prev_block;
    struct block_meta_data_ *next_block;
    uint32_t offset;
} block_meta_data_t;

typedef struct vm_page_
{
    struct vm_page_ *next;
    struct vm_page_ *prev;
    struct vm_page_family_ *pg_family;
    block_meta_data_t block_meta_data;
    char page_memory[0];
} vm_page_t;

typedef struct vm_page_family_
{
    char struct_name[MM_MAX_STRUCT_NAME];
    uint32_t struct_size;
    vm_page_t *first_page;
    glthread_t free_block_priority_list_head;
} vm_page_family_t;

typedef struct vm_page_for_family_
{
    struct vm_page_for_family_ *next;
    vm_page_family_t vm_page_family[0];
} vm_page_for_family_t;

GLTHREAD_TO_STRUCT(glthread_to_block_meta_data, block_meta_data_t, priority_thread_glue, glthread_ptr);

#define MAX_FAMILIES_PER_VM_PAGE \
    (SYSTEM_PAGE_SIZE - sizeof(vm_page_for_family_t *)) / sizeof(vm_page_family_t)

#define ITERATE_PAGE_FAMILIES_BEGIN(vm_page_for_family_ptr, curr)                   \
    {                                                                               \
        uint32_t count = 0;                                                         \
        for (curr = (vm_page_family_t *)&vm_page_for_family_ptr->vm_page_family[0]; \
             curr->struct_size && count <= MAX_FAMILIES_PER_VM_PAGE;                \
             curr++, count++)                                                       \
        {

#define ITERATE_PAGE_FAMILIES_END(vm_page_for_family_ptr, curr) \
    }                                                           \
    }

vm_page_family_t *
lookup_page_family_by_name(char *struct_name);

#define offset_of(container_structure, field_name) (char *)(&((container_structure *)0)->field_name)
#define MM_GET_PAGE_FROM_META_BLOCK(block_meta_data_ptr) (void *)((char *)block_meta_data_ptr - block_meta_data_ptr->offset)
#define NEXT_META_BLOCK(block_meta_data_ptr) (block_meta_data_ptr->next_block)
#define NEXT_META_BLOCK_BY_SIZE(block_meta_data_ptr) (block_meta_data_t *)((char *)(block_meta_data_ptr + 1) + block_meta_data_ptr->block_size)
#define PREV_META_BLOCK(block_meta_data_ptr) (block_meta_data_ptr->prev_block)

#define mm_bind_blocks_for_allocation(allocated_meta_block, free_meta_block)      \
    {                                                                             \
        NEXT_META_BLOCK(free_meta_block) = NEXT_META_BLOCK(allocated_meta_block); \
        PREV_META_BLOCK(free_meta_block) = allocated_meta_block;                  \
        NEXT_META_BLOCK(allocated_meta_block) = free_meta_block;                  \
        if (NEXT_META_BLOCK(free_meta_block))                                     \
            PREV_META_BLOCK(NEXT_META_BLOCK(free_meta_block)) = free_meta_block;  \
    }

#endif

vm_bool_t
mm_is_vm_page_empty(vm_page_t *vm_page);

#define MARK_VM_PAGE_EMPTY(vm_page_t_ptr)             \
    vm_page_t_ptr->block_meta_data.is_free = MM_TRUE; \
    vm_page_t_ptr->block_meta_data.next_block = NULL; \
    vm_page_t_ptr->block_meta_data.prev_block = NULL;

#define ITERATE_VM_PAGE_BEGIN(vm_page_family_ptr, curr)                                \
    {                                                                                  \
        for (curr = (vm_page_family_ptr->first_page); curr != NULL; curr = curr->next) \
        {

#define ITERATE_VM_PAGE_END(vm_page_family_ptr, curr) \
    }                                                 \
    }

#define ITERATE_VM_PAGE_ALL_BLOCKS_BEGIN(vm_page_ptr, curr)                                      \
    {                                                                                            \
        for (curr = (&vm_page_ptr->block_meta_data); curr != NULL; curr = NEXT_META_BLOCK(curr)) \
        {

#define ITERATE_VM_PAGE_ALL_BLOCKS_END(vm_page_ptr, curr) \
    }                                                     \
    }

vm_page_t *
allocate_vm_page(vm_page_family_t *vm_page_family);
void mm_vm_page_delete_and_free(vm_page_t *vm_page);

static inline block_meta_data_t *
mm_get_largest_free_block_page_family(vm_page_family_t *page_family)
{
    glthread_t *first_free_block_glue = page_family->free_block_priority_list_head.right;
    if (first_free_block_glue)
        return glthread_to_block_meta_data(first_free_block_glue);
    return NULL;
}