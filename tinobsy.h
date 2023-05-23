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
        fprintf(stderr, "Assert failed, causing a deliberate segfault now.\n"); \
        *((int*)0) = 1; \
    } else { \
        DBG("Assertion succeeded: %s", #cond); \
    } \
} while(0)
#else
#define DBG(...)
#define ASSERT(...)
#endif

/// @brief What is stored in the cell of the object.
typedef enum {
    /// @brief There is nothing in the cell or the value is stored inline.
    NOTHING,
    /// @brief The cell points to another object.
    OBJECT,
    /// @brief The cell points to a block of memory that this object "owns".
    OWNED_PTR
} tptrkind;

/// @brief Information about the contents of the object struct.
typedef const struct {
    /// @brief The "name" of the object's type.
    const char* const name;
    /// @brief The contents of the "car" cell.
    const tptrkind car;
    /// @brief The contents of the "cdr" cell.
    const tptrkind cdr;
} ttype;

/// @brief A bit-field to store flags information.
typedef uint16_t tflags;

/// @brief The enum of flags currently in use by Tinobsy.
typedef enum {
    GC_MARKED
} tflag;

/// @brief The base object type for everything in Tinobsy.
typedef struct stobject tobject;
/// @brief The struct used to store thread-specific data for Tinobsy coroutines.
typedef struct stthread tthread;
/// @brief The main Tinobsy VM type used to manage all of the objects and threads.
typedef struct stvm tvm;

/// @brief The minimum function pointer needed to call Tinobsy programs.
typedef tobject* (*tfptr)(tthread* thread, tobject* self, tobject* args, tobject* env);

/// @brief The struct that makes up all Tinobsy objects.
struct stobject {
    /// @brief A pointer to this object's type information. NULL if this object is a "tombstone" that has been finalized already.
    ttype* type;
    /// @brief How many other things point to this object (C pointers or other objects)
    size_t refcount;
    /// @brief FLags about this object.
    tflags flags;
    /// @brief INTERNAL POINTER - DO NOT MODIFY - The most recent object allocated in the linked list of all objects.
    struct stobject* next;
    /// @brief A pointer to metadata about this object. Can be NULL.
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

/// @brief The struct that is used to manage virtual threads in Tinobsy.
struct stthread {
    /// @brief The "virtual process ID" used to identify this thread.
    unsigned int vpid;
    /// @brief The next thread in the linked list of all threads.
    struct stthread* next_thread;
    /// @brief A pointer to the VM that created this thread.
    tvm* owner;
    /// @brief The stack of objects that must not be collected by the garbage collector.
    tobject* gc_stack;
    /// @brief The current local environment (variables, etc).
    tobject* env;
    /// @brief NULL normally, but used to pass up an error when one is thrown.
    tobject* error;
    /// @brief The pointer to the current jmp_buf used to handle errors.
    jmp_buf* trycatch;
    /// @brief An opaque pointer to an OS-specific structure used to manage this thread.
    void* thread_handle;
};

/// @brief The struct used to manage all objects and threads.
struct stvm {
    /// @brief The most recent object allocated.
    tobject* first;
    /// @brief The current number of objects that have been allocated.
    size_t num_objects;
    /// @brief An object that can be used to represent the NIL value and differentiate it from a NULL pointer.
    tobject* nil;
    /// @brief The linked list of all active threads.
    tthread* threads;
};

/// @brief Allocate a new object on this VM.
/// @param vm The owner VM.
/// @param type The pointer to the type information struct. Must not be NULL.
/// @return The newly allocated object.
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

// Forward references

void tdecref(tobject* x);
void tincref(tobject* x);
void tsetflag(tobject* x, tflag f);
void tclrflag(tobject* x, tflag f);
bool ttstflag(tobject* x, tflag f);
void tfreethread(tthread* th);

/// @brief Finalize the object, that is, free any owned memory and decrement references to any pointed-to objects.
/// @param x The object to be finalized.
void tfinalize(tobject* x) {
    if (x == NULL) return;
    ttype* xt = x->type;
    if (xt == NULL) return; // Already finalized
    DBG("finalizing a %s {", xt->name);
    tdecref(x->meta);
    if (xt->car == OWNED_PTR) free(x->car_ptr);
    else if (xt->car == OBJECT) tdecref(x->car);
    if (xt->cdr == OWNED_PTR) free(x->cdr_ptr);
    else if (xt->cdr == OBJECT) tdecref(x->cdr);
    DBG("}");
    x->type = NULL;
    x->car = x->cdr = NULL;
    tclrflag(x, GC_MARKED);
}

/// @brief Decrement the reference count of the object, and if it is now 0, finalize the object.
/// @param x The object that has lost a reference.
inline void tdecref(tobject* x) {
    if (x != NULL && x->refcount > 0 && x->type != NULL) {
        x->refcount--;
        DBG("decref'ed a %s, now have %zu refs", x->type->name, x->refcount);
        if (x->refcount <= 0) tfinalize(x);
    }
}

/// @brief Increment the reference count of the object.
/// @param x The object that has gained a reference.
inline void tincref(tobject* x) {
    if (x != NULL) {
        ASSERT(x->type != NULL, "null type object incref'ed");
        x->refcount++;
        DBG("incref'ed a %s, now have %zu refs", x->type->name, x->refcount);
    }
}

/// @brief Test if a flag is set on the object.
/// @param x The object to test.
/// @param f The flag to look at.
/// @return Whether the flag is set.
inline bool ttstflag(tobject* x, tflag f) {
    return x != NULL && (x->flags & (1<<f)) != 0;
}

/// @brief Set a flag on an object.
/// @param x The object to set the flag on.
/// @param f The flag to set.
inline void tsetflag(tobject* x, tflag f) {
    if (x != NULL) x->flags |= 1 << f;
}

/// @brief Clear a flag on an object.
/// @param x The object to clear the flag on.
/// @param f The flag to clear.
inline void tclrflag(tobject* x, tflag f) {
    if (x != NULL) x->flags &= ~(1 << f);
}

/// @brief Recursively marks the object, following pointers to other objects.
/// @param x The object to be marked.
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
        tmarkobject(x->meta);
        if (xt->car == OBJECT) tmarkobject(x->car);
        if (xt->cdr == OBJECT) {
            x = x->cdr;
            goto mark;
            // tail recursion
        }
    }
}

/// @brief Garbage-collects all objects that don't have any active references, freeing their memory.
/// @param vm The VM to run the garbage-collector on.
/// @return The number of objects swept.
size_t tmarksweep(tvm* vm) {
    size_t start = vm->num_objects;
    DBG("garbage collect start, %zu objects {", start);
    for (tthread* t = vm->threads; t != NULL; t = t->next_thread) {
        tmarkobject(t->gc_stack);
        tmarkobject(t->env);
        tmarkobject(t->error);
    }
    DBG("marking globals");
    tmarkobject(vm->nil);
    tobject** x = &vm->first;
    DBG("garbage collect sweeping");
    size_t dangling = 0;
    while (*x != NULL) {
        if (!ttstflag(*x, GC_MARKED)) {
            tobject* u = *x;
            // Kill all pointers to this object
            if (u->refcount > 0) {
                size_t realrefs = 0;
                tobject* p = vm->first;
                while (p != NULL) {
                    if (p->type != NULL) {
                        if (p->type->car == OBJECT && p->car == u) {
                            ASSERT(!ttstflag(p, GC_MARKED), "Unmarked object pointed to by marked object");
                            p->car = NULL;
                            realrefs++;
                        }
                        if (p->type->cdr == OBJECT && p->cdr == u) {
                            ASSERT(!ttstflag(p, GC_MARKED), "Unmarked object pointed to by marked object");
                            p->cdr = NULL;
                            realrefs++;
                        }
                    }
                    p = p->next;
                }
                if (realrefs != u->refcount) {
                    dangling += u->refcount - realrefs;
                }
                else {
                    DBG("Cyclic garbage detected");
                }
            }
            *x = u->next;
            tfinalize(u);
            free(u);
            vm->num_objects--;
        } else {
            tclrflag(*x, GC_MARKED);
            x = &(*x)->next;
        }
    }
    #ifndef TINOBSY_IGNORE_DANGLING_POINTERS
    fprintf(stderr, "\nWARNING!! %zu dangling pointers detected!!\n", dangling);
    #endif
    size_t freed = start - vm->num_objects;
    DBG("garbage collect end, %zu objects, %zu freed, %zu dangling pointers }", vm->num_objects, freed, dangling);
    return freed;
}

/// @brief The typeinfo for the vm->nil member.
ttype tnil_type = {"nil", NOTHING, NOTHING};

/// @brief Allocates a new VM and sets it up.
/// @return The newly allocated VM.
tvm* tnewvm() {
    tvm* vm = (tvm*)calloc(1, sizeof(struct stvm));
    vm->nil = talloc(vm, &tnil_type);
    return vm;
}

/// @brief Destroys the VM, frees all associated objects and threads, and releases the memory.
/// @param vm The VM to be freed.
void tfreevm(tvm* vm) {
    DBG("free vm");
    #ifdef TINOBSY_DEBUG
    size_t th = 0;
    #endif
    while (vm->threads != NULL) {
        tfreethread(vm->threads);
        #ifdef TINOBSY_DEBUG
        th++;
        #endif
    }
    DBG("freed %zu threads", th);
    tdecref(vm->nil);
    vm->nil = NULL;
    tmarksweep(vm);
    ASSERT(vm->num_objects == 0, "gc bugged");
    free(vm);
}

/// @brief Gets the next unused VPID for threads on this VM.
/// @param vm The VM whose threads to search.
/// @return The next open VPID.
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

/// @brief Pushes a new thread onto the VM's thread stack, and returns it.
/// @param vm The VM to push the thread onto.
/// @return The new thread.
tthread* tpushthread(tvm* vm) {
    tthread* new_ = (tthread*)calloc(1, sizeof(struct stthread));
    new_->vpid = tnextvpid(vm);
    DBG("Allocating a new thread, next vpid is %u", new_->vpid);
    new_->next_thread = vm->threads;
    vm->threads = new_;
    new_->owner = vm;
    return new_;
}

/// @brief Deletes the thread and all associated memory, and splices it out of the owner VM's list of threads.
/// @param th The thread to delete.
void tfreethread(tthread* th) {
    tvm* vm = th->owner;
    DBG("Freeing thread %u {", th->vpid);
    tthread** x = &vm->threads;
    while (*x != NULL) {
        if (*x == th) {
            *x = th->next_thread;
            break;
        } else {
            x = &(*x)->next_thread;
        }
    }
    tdecref(th->gc_stack);
    tdecref(th->env);
    tdecref(th->error);
    free(th);
    DBG("}");
}

/// @def Set X to Y, while updating reference counts.
#define SET(x, y) do { \
    tincref(y); \
    tdecref(x); \
    (x)=(y); \
} while (0)

/// @def On therad t, set up the jmp_buf and run tc, if it throws, run cc.
#define TRYCATCH(t, tc, cc) do { \
    jmp_buf dynamic; \
    jmp_buf* prev = (t)->trycatch; \
    (t)->trycatch = &dynamic; \
    bool thrown = false; \
    tobject* old_st = NULL; SET(old_st, (t)->gc_stack); \
    int sig = 0; \
    if (!(sig = setjmp(dynamic))) { tc; } \
    else { thrown = true; SET((t)->gc_stack, old_st); } \
    (t)->trycatch = prev; \
    if (thrown) { \
        ASSERT((t)->error != NULL); \
        cc; \
    } \
} while (0)

#ifdef __cplusplus
[[noreturn]]
#endif
/// @brief Raise an error on the thread by longjmp()'ing back to the last saved try-catch point. This function does not return.
/// @param th The thread to use.
/// @param error The error object to be thrown.
/// @param sig The value to be returned by setjmp().
void traise(tthread* th, tobject* error, int sig) {
    DBG("Throwing an error on thread %u", th->vpid);
    ASSERT(th->trycatch != NULL, "No try-catch set");
    SET(th->error, error);
    SET(th->gc_stack, NULL);
    tdecref(error);
    longjmp(*(th->trycatch), sig);
}

/// @def Short for traise(th, err, 1).
#define RAISE(th, err) traise((th), (err), 1)

#ifdef __cplusplus
}
#endif
#endif
