#ifndef TINOBSY_H
#define TINOBSY_H

#ifdef __cplusplus
extern "C" {
#else
#ifndef bool
typedef int bool;
#define true 1
#define false 0
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

#ifdef TINOBSY_DEBUG
#define DBG(s, ...) printf("[%s:%i-%s] " s "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define ASSERT(cond, ...) do { \
    if (!(cond)) { \
        DBG("Assertion failed: %s", #cond); \
        DBG(__VA_ARGS__); \
        exit(72); \
    } else { \
        DBG("Assertion succeeded: %s", #cond); \
    } \
} while(0)
#else
#define DBG(...)
#define ASSERT(...)
#endif

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
typedef struct stvm tvm;

typedef tobject* (*tfptr)(tvm* vm, tthread* thread, tobject* self, tobject* args, tobject* env);

struct stobject {
    ttype* type;
    size_t refcount;
    tflags flags;
    struct stobject* next;
    struct stobject* meta;
    union {
        struct {
            union {
                struct stobject* car;
                void* car_ptr;
                char* car_str;
                int32_t car_int;
                float car_float;
            };
            union {
                struct stobject* cdr;
                void* cdr_ptr;
                tfptr func;
                uint32_t cdr_uint;
                float cdr_float;
            };
        };
        int64_t as_integer;
        double as_double;
    };
};

struct stthread {
    unsigned int vpid;
    struct stthread* next_thread;
    tobject* gc_stack;
    tobject* env;
    jmp_buf* trycatch;
    jmp_buf top_try;
    void* thread_handle;
};

struct stvm {
    tobject* first;
    size_t num_objects;
    tobject* nil;
    tthread* threads;
    tobject* env;
    tobject* gc_stack;
    jmp_buf* trycatch;
    jmp_buf top_try;
};

tobject* talloc(tvm* vm, const ttype* type) {
    ASSERT(type != NULL, "tried to initialize a null type");
    DBG("allocating a %s", type->name);
    tobject* newobject;
    for (newobject = vm->first; newobject != NULL; newobject = newobject->next) {
        if (newobject->type == NULL) {
            DBG("reusing garbage");
            tobject* oldnext = newobject->next;
            memset(newobject, 0, sizeof(struct stobject));
            newobject->next = oldnext;
            goto gotgarbage;
        }
    }
    DBG("need new memory");
    newobject = (tobject*)calloc(1, sizeof(struct stobject));
    newobject->next = vm->first;
    vm->first = newobject;
    vm->num_objects++;
    gotgarbage:
    newobject->refcount = 1;
    newobject->type = type;
    return newobject;
}

// Forward reference
void tdecref(tobject* x);
void tincref(tobject* x);
void tsetflag(tobject* x, tflag f);
void tclrflag(tobject* x, tflag f);
bool ttstflag(tobject* x, tflag f);

void tfinalize(tobject* x) {
    if (x == NULL) return;
    ttype* xt = x->type;
    if (xt == NULL) return; // Already finalized
    DBG("finalizing a %s {", xt->name);
    if (xt->car == OWNED_PTR) free(x->car_ptr);
    else if (xt->car == OBJECT) tdecref(x->car);
    if (xt->cdr == OWNED_PTR) free(x->cdr_ptr);
    else if (xt->cdr == OBJECT) tdecref(x->cdr);
    DBG("}");
    x->type = NULL;
    tclrflag(x, GC_MARKED);
}

inline void tdecref(tobject* x) {
    if (x != NULL && x->refcount > 0 && x->type != NULL) {
        x->refcount--;
        DBG("decref'ed a %s, now have %zu refs", x->type->name, x->refcount);
        if (x->refcount <= 0) tfinalize(x);
    }
}

inline void tincref(tobject* x) {
    if (x != NULL) {
        ASSERT(x->type != NULL, "null type object incref'ed");
        x->refcount++;
        DBG("incref'ed a %s, now have %zu refs", x->type->name, x->refcount);
    }
}

inline bool ttstflag(tobject* x, tflag f) {
    return x != NULL && x->flags & (1<<f) != 0;
}

inline void tsetflag(tobject* x, tflag f) {
    if (x != NULL) x->flags |= 1 << f;
}

inline void tclrflag(tobject* x, tflag f) {
    if (x != NULL) x->flags &= ~(1 << f);
}

void tmarkobject(tobject* x) {
    mark:
    if (x == NULL) {
        DBG("marking a NULL");
        return;
    }
    if (ttstflag(x, GC_MARKED)) return;
    tsetflag(x, GC_MARKED);
    ttype* xt = x->type;
    if (xt != NULL) {
        DBG("marking a %s", xt->name);
        if (xt->car == OBJECT) tmarkobject(x->car);
        if (xt->cdr == OBJECT) {
            x = x->cdr;
            goto mark;
            // tail recursion
        }
    }
}

size_t tmarksweep(tvm* vm) {
    size_t start = vm->num_objects;
    DBG("garbage collect start, %zu objects {", start);
    for (tthread* t = vm->threads; t != NULL; t = t->next_thread) {
        tmarkobject(t->gc_stack);
        tmarkobject(t->env);
    }
    DBG("marking globals");
    tmarkobject(vm->gc_stack);
    tmarkobject(vm->env);
    tmarkobject(vm->nil);
    tobject** x = &vm->first;
    DBG("garbage collect sweeping");
    while (*x != NULL) {
        if (!ttstflag(*x, GC_MARKED)) {
            tobject* u = *x;
            *x = u->next;
            tfinalize(u);
            free(u);
            vm->num_objects--;
        } else {
            tclrflag(*x, GC_MARKED);
            x = &(*x)->next;
        }
    }
    size_t freed = start - vm->num_objects;
    DBG("garbage collect end, %zu objects, %zu freed }", vm->num_objects, freed);
    return freed;
}

ttype tnil_type = {"nil", NOTHING, NOTHING};

tvm* tnewvm() {
    tvm* vm = (tvm*)calloc(1, sizeof(struct stvm));
    vm->nil = talloc(vm, &tnil_type);
    vm->trycatch = &vm->top_try;
    return vm;
}

void tfreevm(tvm* vm) {
    DBG("free vm");
    #ifdef TINOBSY_DEBUG
    size_t th = 0;
    #endif
    while (vm->threads != NULL) {
        tthread* t = vm->threads;
        vm->threads = t->next_thread;
        free(t);
        #ifdef TINOBSY_DEBUG
        th++;
        #endif
    }
    DBG("freed %zu threads", th);
    vm->nil = vm->env = vm->gc_stack = NULL;
    tmarksweep(vm);
    ASSERT(vm->num_objects == 0, "gc bugged");
    free(vm);
}

unsigned int tnextvpid(tvm* vm) {
    unsigned int p = 0;
    while (true) {
        for (tthread* t = vm->threads; t != NULL; t = t->next_thread) {
            if (t->vpid == p) goto next;
        }
        return p;
        next:
        p++;
    }
}

tthread* tpushthread(tvm* vm) {
    tthread* new_ = (tthread*)calloc(1, sizeof(struct stthread));
    new_->vpid = tnextvpid(vm);
    new_->next_thread = vm->threads;
    vm->threads = new_;
    new_->trycatch = &new_->top_try;
    return new_;
}

void tfreethread(tvm* vm, tthread* th) {
    tthread** x = &vm->threads;
    while (*x != NULL) {
        if (*x == th) {
            *x = th->next_thread;
        } else {
            x = &(*x)->next_thread;
        }
    }
    tdecref(th->gc_stack);
    tdecref(th->env);
    free(th);
}

#define SET(x, y) do{tincref(y);tdecref(x);(x)=(y);}while(0)

#ifdef __cplusplus
}
#endif
#endif
