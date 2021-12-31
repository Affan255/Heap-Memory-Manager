#ifndef __UAPI_MM__
#define __UAPI_MM__

#include <stdint.h>
#include <memory.h>

void mm_init();

void *
xcalloc(char *struct_name, int units);

void xfree(void *app_data);

void mm_instantiate_new_page_family(char *struct_name, uint32_t struct_size);

#define MM_REG_STRUCT(struct_name) ( \
    mm_instantiate_new_page_family(#struct_name, sizeof(struct_name)))

#define XCALLOC(units, struct_name) ( \
    xcalloc(#struct_name, units))

#define XFREE(app_data) ( \
    xfree(app_data))

void mm_print_registered_page_families();

void mm_print_memory_usage(char *struct_name);

#define MEMORY_USAGE(struct_name) ( \
    mm_print_memory_usage(#struct_name))

#endif