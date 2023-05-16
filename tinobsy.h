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
    
typedef enum {
    NOTHING,
    OBJECT,
    OWNED_PTR
} tptrkind;

typedef const struct {
    const char* const name;
    const tptrkind car;
    const tptrkind cdr;
} ttype;

typedef uint16_t tflags;
    
typedef enum {
    GC_MARKED
} tflag;
    
typedef struct stobject tobject;
typedef struct stthread tthread;

typedef tobject* (*tfptr)(tthread*, tobject*, tobject*, tobject*);

struct stobject {
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
                char* car_str;
            };
            union {
                struct stobject* cdr;
                void* cdr_ptr;
                tfptr func;
            };
        };
    };
};

struct stthread {
    unsigned int vpid;
    struct stthread* const next_thread;
    tobject* gc_stack;
    jmp_buf* trycatch;
    jmp_buf top_try;
    void* thread_handle;
};

typedef struct {
    tobject* first;
    size_t num_objects;
    size_t next_gc;
    tobject* nil;
    othread* threads;
} tvm;

tobject* talloc(tvm* vm, const ttype type) {
    tobject* newobject;
    for (newobject = vm->first; newobject != NULL; newobject = newobject->next) {
        if (newobject->type == NULL) goto gotgarbage;
    }
    newobject = malloc(sizeof(struct stobject));
    newobject->next = vm->first;
    vm->first = newobject;
    vm->num_objects++;
    gotgarbage:
    newobject->refcount = 1;
    newobject->type = type;
    return newobject;
}

// Forward reference
inline void tdecref(tobject* x);

void tfinalize(tobject* x) {
    ttype* xt = x->type;
    if (xt == NULL) return; // Already finalized
    if (xt->car == OWNED_PTR) free(x->car_ptr);
    else if (xt->car == OBJECT) tdecref(x->car);
    if (xt->cdr == OWNED_PTR) free(x->cdr_ptr);
    else if (xt->cdr == OBJECT) tdecref(x->cdr);
    x->type = NULL;
}

inline void tdecref(tobject* x) {
    if (x != NULL && x->refcount > 0) {
        x->refcount--;
        if (x->refcount <= 0) tfinalize(x);
    }
}

inline void tincref(tobject* x) {
    if (x != NULL) x->refcount++;
}

#define SET(x, y) do{tdecref(x);tincref(y);(x)=(y);}while(0)

#ifdef __cplusplus
}
#endif
#endif
