#include "mm.h"
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>

static size_t SYSTEM_PAGE_SIZE = 0;

void mm_init()
{
    SYSTEM_PAGE_SIZE = getpagesize();
}

static vm_page_for_family_t *first_vm_page_for_family = NULL;

static void *
mm_get_new_vm_page_from_kernel(int units)
{
    char *vm_page = mmap(0, units * SYSTEM_PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
    if (vm_page == MAP_FAILED)
    {
        printf("Error: VM page allocation failed\n");
        return NULL;
    }
    memset(vm_page, 0, units * SYSTEM_PAGE_SIZE);
    return (void *)vm_page;
}

void mm_instantiate_new_page_family(char *struct_name, uint32_t struct_size)
{
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_for_family_t *new_vm_page_for_family = NULL;
    if (struct_size > SYSTEM_PAGE_SIZE)
    {
        printf("Error: %s() Structure %s exceeds the system page size\n", __FUNCTION__, struct_name);
        return;
    }
    if (!first_vm_page_for_family)
    {
        first_vm_page_for_family = (vm_page_for_family_t *)mm_get_new_vm_page_from_kernel(SYSTEM_PAGE_SIZE);
        first_vm_page_for_family->next = NULL;
        vm_page_family_curr = (vm_page_family_t *)&first_vm_page_for_family->vm_page_family[0];
        strncpy(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME);
        vm_page_family_curr->struct_size = struct_size;
        vm_page_family_curr->first_page = NULL;
        init_glthread(&vm_page_family_curr->free_block_priority_list_head);
        return;
    }
    uint32_t count1 = 0;
    ITERATE_PAGE_FAMILIES_BEGIN(first_vm_page_for_family, vm_page_family_curr)
    {
        if (strncmp(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME) != 0)
        {
            count1++;
            continue;
        }
        assert(0);
    }
    ITERATE_PAGE_FAMILIES_END(first_vm_page_for_family, vm_page_family_curr)

    if (count1 == MAX_FAMILIES_PER_VM_PAGE)
    {
        new_vm_page_for_family = (vm_page_for_family_t *)mm_get_new_vm_page_from_kernel(SYSTEM_PAGE_SIZE);
        new_vm_page_for_family->next = first_vm_page_for_family;
        first_vm_page_for_family = new_vm_page_for_family;
        vm_page_family_curr = (vm_page_family_t *)&first_vm_page_for_family->vm_page_family[0];
    }
    strncpy(vm_page_family_curr->struct_name, struct_name, MM_MAX_STRUCT_NAME);
    vm_page_family_curr->struct_size = struct_size;
    vm_page_family_curr->first_page = NULL;
    init_glthread(&vm_page_family_curr->free_block_priority_list_head);
}

void mm_print_registered_page_families()
{
    printf("System page size: %d\n", (int)SYSTEM_PAGE_SIZE);
    printf("Maximum no of families per page: %d\n", MAX_FAMILIES_PER_VM_PAGE);
    vm_page_for_family_t *vm_page_for_family_curr = first_vm_page_for_family;
    if (!vm_page_for_family_curr)
    {
        printf("No vm pages allocated\n");
        return;
    }
    vm_page_family_t *vm_page_family_curr = NULL;
    int vm_page_count = 1;
    while (vm_page_for_family_curr != NULL)
    {
        printf("VM page %d\n", vm_page_count);
        ITERATE_PAGE_FAMILIES_BEGIN(vm_page_for_family_curr, vm_page_family_curr)
        {
            printf("Page family: %s, Struct size: %d\n", vm_page_family_curr->struct_name, vm_page_family_curr->struct_size);
        }
        ITERATE_PAGE_FAMILIES_END(vm_page_for_family_curr, vm_page_family_curr)
        vm_page_count++;
        vm_page_for_family_curr = vm_page_for_family_curr->next;
    }
}

vm_page_family_t *
lookup_page_family_by_name(char *struct_name)
{
    vm_page_for_family_t *vm_page_for_family_curr = first_vm_page_for_family;

    vm_page_family_t *vm_page_family_curr = NULL;
    while (vm_page_for_family_curr != NULL)
    {

        ITERATE_PAGE_FAMILIES_BEGIN(vm_page_for_family_curr, vm_page_family_curr)
        {
            if (strncmp(vm_page_family_curr->struct_name, struct_name, sizeof(struct_name)) == 0)
            {
                return vm_page_family_curr;
            }
        }
        ITERATE_PAGE_FAMILIES_END(vm_page_for_family_curr, vm_page_family_curr)

        vm_page_for_family_curr = vm_page_for_family_curr->next;
    }
    return NULL;
}

void meta_block_util(block_meta_data_t *first_meta_block)
{
    block_meta_data_t *curr = NULL;
    uint32_t free_count = 0, alloc_count = 0, max_free_size = 0, max_alloc_size = 0;
    void *max_free_block;
    void *max_alloc_block;

    for (curr = first_meta_block; curr != NULL; curr = curr->next_block)
    {
        if (curr->is_free == MM_TRUE)
        {
            if (curr->next_block && curr->next_block->is_free)
                assert(0);
            free_count++;
            if (max_free_size < curr->block_size)
            {
                max_free_size = curr->block_size;
                max_free_block = (void *)curr;
            }
        }
        else
        {
            alloc_count++;
            if (max_alloc_size < curr->block_size)
            {
                max_alloc_size = curr->block_size;
                max_alloc_block = (void *)curr;
            }
        }
    }
}

static void
mm_union_free_blocks(block_meta_data_t *first, block_meta_data_t *second)
{
    assert(first->is_free == MM_TRUE && second->is_free == MM_TRUE);
    first->block_size += sizeof(block_meta_data_t) + second->block_size;
    first->next_block = second->next_block;
    if (second->next_block)
        second->next_block->prev_block = first;
}

vm_bool_t
mm_is_vm_page_empty(vm_page_t *vm_page)
{
    block_meta_data_t meta_block = vm_page->block_meta_data;
    if (meta_block.is_free == MM_TRUE && meta_block.next_block == NULL && meta_block.prev_block == NULL)
        return MM_TRUE;
    return MM_FALSE;
}

static inline uint32_t
mm_max_page_allocatable_memory(int units)
{
    return (uint32_t)((uint32_t)SYSTEM_PAGE_SIZE * units - (uint32_t)offset_of(vm_page_t, page_memory));
}

static int
free_blocks_comparison_fn(void *_block_meta_data_1, void *_block_meta_data_2)
{
    block_meta_data_t *block_meta_data_1 = (block_meta_data_t *)_block_meta_data_1;
    block_meta_data_t *block_meta_data_2 = (block_meta_data_t *)_block_meta_data_2;
    if (block_meta_data_1->block_size < block_meta_data_2->block_size)
        return 1;
    else if (block_meta_data_1->block_size > block_meta_data_2->block_size)
        return -1;
    else
        return 0;
}

static void
mm_add_free_block_meta_data_to_free_block_list(vm_page_family_t *vm_page, block_meta_data_t *free_block)
{
    assert(free_block->is_free == MM_TRUE);
    glthread_priority_insert(&vm_page->free_block_priority_list_head, &free_block->priority_thread_glue, free_blocks_comparison_fn, offset_of(block_meta_data_t, priority_thread_glue));
}

vm_page_t *
allocate_vm_page(vm_page_family_t *vm_page_family)
{
    vm_page_t *vm_page = mm_get_new_vm_page_from_kernel(1);
    MARK_VM_PAGE_EMPTY(vm_page);
    vm_page->block_meta_data.block_size = mm_max_page_allocatable_memory(1);
    vm_page->block_meta_data.offset = offset_of(vm_page_t, block_meta_data);
    init_glthread(&vm_page->block_meta_data.priority_thread_glue);
    vm_page->next = NULL;
    vm_page->prev = NULL;
    vm_page->pg_family = vm_page_family;

    if (!vm_page_family->first_page)
    {
        vm_page_family->first_page = vm_page;
        return vm_page;
    }
    vm_page->next = vm_page_family->first_page;
    vm_page_family->first_page->prev = vm_page;
    vm_page_family->first_page = vm_page;
    return vm_page;
}

static void
mm_return_vm_page_to_kernel(void *vm_page, int units)
{
    if (munmap(vm_page, SYSTEM_PAGE_SIZE * units))
    {
        printf("Error: Could not unmap VM page to kernel\n");
    }
}

void mm_vm_page_delete_and_free(vm_page_t *vm_page)
{
    vm_page_family_t *vm_page_family = vm_page->pg_family;
    if (vm_page_family->first_page == vm_page)
    {
        vm_page_family->first_page = vm_page->next;
        if (vm_page->next)
            vm_page->next->prev = vm_page;
        vm_page->next = NULL;
        vm_page->prev = NULL;
        mm_return_vm_page_to_kernel((void *)vm_page, 1);
        return;
    }
    if (vm_page->next)
        vm_page->next->prev = vm_page;
    vm_page->prev->next = vm_page->next;
    mm_return_vm_page_to_kernel((void *)vm_page, 1);
    return;
}

static vm_page_t *
mm_family_new_page_add(vm_page_family_t *vm_page_family)
{

    vm_page_t *vm_page = allocate_vm_page(vm_page_family);

    if (!vm_page)
        return NULL;

    /* The new page is like one free block, add it to the
     * free block list*/
    mm_add_free_block_meta_data_to_free_block_list(
        vm_page_family, &vm_page->block_meta_data);

    return vm_page;
}

static vm_bool_t
mm_split_free_data_block_for_allocation(vm_page_family_t *page_family, block_meta_data_t *free_block, uint32_t size)
{
    assert(free_block->is_free == MM_TRUE);
    block_meta_data_t *next_meta_block = NULL;
    if (free_block->block_size < size)
    {
        return MM_FALSE;
    }
    uint32_t remaining_size = free_block->block_size - size;
    free_block->is_free = MM_FALSE;
    free_block->block_size = size;
    remove_glthread(&free_block->priority_thread_glue);

    /* Case 1: No split */
    if (!remaining_size)
    {
        return MM_TRUE;
    }

    /* Case 2: Partial split */
    /* Soft Internal Fragmentation */
    else if (sizeof(block_meta_data_t) < remaining_size && remaining_size < sizeof(block_meta_data_t) + page_family->struct_size)
    {
        next_meta_block = NEXT_META_BLOCK_BY_SIZE(free_block);
        next_meta_block->is_free = MM_TRUE;
        next_meta_block->block_size = remaining_size - sizeof(block_meta_data_t);
        next_meta_block->offset = free_block->offset + sizeof(block_meta_data_t) + free_block->block_size;

        init_glthread(&next_meta_block->priority_thread_glue);

        mm_bind_blocks_for_allocation(free_block, next_meta_block);

        mm_add_free_block_meta_data_to_free_block_list(page_family, next_meta_block);
    }

    /* Hard Internal Fragmentation */
    else if (remaining_size < sizeof(block_meta_data_t))
    {
        /* No need to do anything */
    }

    /* Case 3: Full Split */
    else
    {
        next_meta_block = NEXT_META_BLOCK_BY_SIZE(free_block);
        next_meta_block->is_free = MM_TRUE;
        next_meta_block->block_size = remaining_size - sizeof(block_meta_data_t);
        next_meta_block->offset = free_block->offset + sizeof(block_meta_data_t) + free_block->block_size;

        init_glthread(&next_meta_block->priority_thread_glue);
        mm_bind_blocks_for_allocation(free_block, next_meta_block);
        mm_add_free_block_meta_data_to_free_block_list(page_family, next_meta_block);
    }
    return MM_TRUE;
}

static block_meta_data_t *
mm_allocate_free_data_block(vm_page_family_t *page_family, uint32_t req_size)
{
    vm_bool_t status = MM_FALSE;

    block_meta_data_t *first_free_block = mm_get_largest_free_block_page_family(page_family);
    if (!first_free_block || first_free_block->block_size < req_size)
    {
        vm_page_t *vm_page = mm_family_new_page_add(page_family);
        status = mm_split_free_data_block_for_allocation(page_family, &vm_page->block_meta_data, req_size);
        if (status == MM_TRUE)
            return &vm_page->block_meta_data;
        return NULL;
    }
    if (first_free_block)
    {
        status = mm_split_free_data_block_for_allocation(page_family, first_free_block, req_size);
        if (status == MM_TRUE)
            return first_free_block;
        return NULL;
    }
}

void *
xcalloc(char *struct_name, int units)
{
    vm_page_family_t *page_family = lookup_page_family_by_name(struct_name);
    if (!page_family)
    {
        printf("Error: Stucture %s is not registered with memory manager\n", struct_name);
        return NULL;
    }
    if (units * page_family->struct_size > mm_max_page_allocatable_memory(1))
    {
        printf("Error: Memory requested exceeds page size\n");
        return NULL;
    }
    block_meta_data_t *free_block_meta_data = NULL;
    free_block_meta_data = mm_allocate_free_data_block(page_family, units * page_family->struct_size);
    if (free_block_meta_data)
    {
        memset((char *)(free_block_meta_data + 1), 0, free_block_meta_data->block_size);
        return (void *)(free_block_meta_data + 1);
    }
    return NULL;
}

static void
dump_vm_data_page(vm_page_t *vm_page, int page_count)
{
    printf("%d  Page %d\n", vm_page, page_count);
    printf("next -> %d ", vm_page->next);
    printf("prev -> %d ", vm_page->prev);
}

static void
dump_meta_block_data(block_meta_data_t *block_meta, int count)
{
    printf("%d    ", block_meta);
    printf("Block %d    ", count);
    if (block_meta->is_free)
        printf("F R E E D    ");
    else
        printf("ALLOCATED    ");
    printf("block-size = %d    ", block_meta->block_size);
    printf("next = %d    ", block_meta->next_block);
    printf("prev = %d    ", block_meta->prev_block);
}

static void
dump_memory_usage_for_page_family(vm_page_family_t *pg_family)
{
    vm_page_t *vm_page_curr = NULL;
    int count = 0, page_count = 0;
    ITERATE_VM_PAGE_BEGIN(pg_family, vm_page_curr)
    {

        dump_vm_data_page(vm_page_curr, page_count);
        printf("\n");

        block_meta_data_t *block_meta_curr = NULL;
        ITERATE_VM_PAGE_ALL_BLOCKS_BEGIN(vm_page_curr, block_meta_curr)
        {
            dump_meta_block_data(block_meta_curr, count);
            printf("\n");
            count++;
        }
        ITERATE_VM_PAGE_ALL_BLOCKS_END(vm_page_curr, block_meta_curr)
        page_count++;
        printf("\n");
    }
    ITERATE_VM_PAGE_END(pg_family, vm_page_curr)
}

void mm_print_memory_usage(char *struct_name)
{
    if (!struct_name)
    {
        vm_page_family_t *pg_family_curr = NULL;
        ITERATE_PAGE_FAMILIES_BEGIN(first_vm_page_for_family, pg_family_curr)
        {
            printf("Page family -> %s", pg_family_curr->struct_name);
            printf("\n");
            dump_memory_usage_for_page_family(pg_family_curr);
        }
        ITERATE_PAGE_FAMILIES_END(first_vm_page_for_family, pg_family_curr)
    }

    else
    {
        vm_page_family_t *pg_family = lookup_page_family_by_name(struct_name);
        dump_memory_usage_for_page_family(pg_family);
    }
}

static int
mm_get_hard_internal_frag_size(block_meta_data_t *first, block_meta_data_t *second)
{
    block_meta_data_t *next_block = NEXT_META_BLOCK_BY_SIZE(first);
    return (int)((unsigned long)second - (unsigned long)next_block);
}

static block_meta_data_t *
mm_free_blocks(block_meta_data_t *to_be_free_block)
{
    block_meta_data_t *return_block = NULL;
    assert(to_be_free_block->is_free == MM_FALSE);
    vm_page_t *hosting_page = MM_GET_PAGE_FROM_META_BLOCK(to_be_free_block);
    vm_page_family_t *vm_page_family = hosting_page->pg_family;
    return_block = to_be_free_block;
    to_be_free_block->is_free = MM_TRUE;
    block_meta_data_t *next_block = NEXT_META_BLOCK(to_be_free_block);

    /* Handle hard IF memory */
    if (next_block)
    {
        to_be_free_block->block_size += mm_get_hard_internal_frag_size(to_be_free_block, next_block);
    }
    else
    {
        char *end_address_of_vm_page = (char *)((char *)hosting_page + SYSTEM_PAGE_SIZE);
        char *end_addr_of_free_data_block = (char *)((to_be_free_block + 1) + to_be_free_block->block_size);
        to_be_free_block->block_size += (int)((unsigned long)end_address_of_vm_page - (unsigned long)end_addr_of_free_data_block);
    }

    /* Perform merging */
    if (next_block && next_block->is_free == MM_TRUE)
    {
        mm_union_free_blocks(to_be_free_block, next_block);
        return_block = to_be_free_block;
    }
    block_meta_data_t *prev_block = PREV_META_BLOCK(to_be_free_block);
    if (prev_block && prev_block->is_free == MM_TRUE)
    {
        mm_union_free_blocks(prev_block, to_be_free_block);
        return_block = prev_block;
    }

    if (mm_is_vm_page_empty(hosting_page))
    {
        mm_vm_page_delete_and_free(hosting_page);
        return NULL;
    }
    mm_add_free_block_meta_data_to_free_block_list(hosting_page->pg_family, return_block);
    return return_block;
}

void xfree(void *app_data)
{
    block_meta_data_t *block_meta_data = (block_meta_data_t *)((char *)app_data - sizeof(block_meta_data_t));
    assert(block_meta_data->is_free == MM_FALSE);
    mm_free_blocks(block_meta_data);
}