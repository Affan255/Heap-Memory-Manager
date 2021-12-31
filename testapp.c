#include "uapi_mm.h"
#include <stdio.h>

typedef struct emp_{
    char name[32];
    uint32_t emp_id;
}emp_t;

typedef struct student_{
    char name[32];
    uint32_t roll_no;
    uint32_t marks_phys;
    uint32_t marks_chem;
    uint32_t marks_maths;
    struct student_ *next;
}student_t;


int main(int argc, char **argv) {
    mm_init();
    MM_REG_STRUCT(emp_t);
    MM_REG_STRUCT(student_t);
    mm_print_registered_page_families();

    emp_t *emp_1 = XCALLOC(1, emp_t);
    emp_t *emp_2 = XCALLOC(1, emp_t);
    emp_t *emp_3 = XCALLOC(1, emp_t);

    student_t *stud_1 = XCALLOC(1, student_t);
    student_t *stud_2 = XCALLOC(1, student_t);

    
    // for (int i=1;i<=205;i++) {
    //     XCALLOC(1, emp_t);
    //     XCALLOC(1, student_t);
    // }
    
    mm_print_memory_usage(0);
    // mm_print_block_usage();

    XFREE(emp_1);
    mm_print_memory_usage(0);
    XFREE(emp_2);
    mm_print_memory_usage(0);

    return 0;
}

