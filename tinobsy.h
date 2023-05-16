#ifndef TINOBSY_H
#define TINOBSY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

typedef const struct {
    const char* const name;
    const int car_objects;
    const int cdr_objects;
} ttype;

typedef uint16_t tflags;

typedef struct stobject {
    ttype* const type;
    size_t refcount;
    tflags flags;
    stobject* const next;
    union {
        int64_t as_integer;
        double as_double;
        struct {
            union {
                struct stobject* car;
                void* car_ptr;
            };
            union {
                struct stobject* cdr;
                void* cdr_ptr;
            };
        };
    };
} tobject;

typedef struct stthread {
    unsigned int pid;
    struct stthread* const next_thread;
    tobject* gc_stack;
    jmp_buf* trycatch;
    jmp_buf top_try;
} tthread;

typedef struct {
    tobject* first;
    size_t num_objects;
    size_t next_gc;
    tobject* nil;
    othread* threads;
} tvm;

tobject* talloc(tvm* vm, const ttype type) {
    // TODO: reuse garbage
    tobject* newobject = malloc(sizeof(struct stobject));
    newobject->next = vm->first;
    vm->first = newobject;
    vm->num_objects++;
    newobject->refcount = 1;
    newobject->type = type;
    return newobject;
}

#ifdef __cplusplus
}
#endif
#endif
