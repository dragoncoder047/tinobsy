#pragma once

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <csetjmp>


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

namespace tinobsy {

// Forward declare
class object;
class thread;
class vm;

typedef void (*init_function)(object*, void*);
typedef void (*mark_function)(object*);
typedef void (*finalize_function)(object*);

// Information about the contents of the object struct.
class object_schema {
    public:
    // The "name" of the object's schema.
    char* name;
    // The function to set up the object.
    init_function init;
    // The function to mark the object.
    mark_function mark;
    // The function to finalize the object.
    finalize_function finalize;
    object_schema(const char* const name, init_function init, mark_function mark, finalize_function finalize);
    ~object_schema();
};

// A bit-field to store flags information.
typedef uint16_t flag_field;

// The enum of flags currently in use by Tinobsy.
typedef enum {
    GC_MARKED
} flag;

// The minimum function pointer needed to call Tinobsy programs.
typedef object* (*function_pointer)(thread* thread, object* self, object* args, object* env);

// The struct that makes up all Tinobsy objects.
class object {
    public:
    // A pointer to this object's schema information. NULL if this object is a "tombstone" that has been finalized already.
    object_schema* schema;
    // How many other things point to this object (C pointers or other objects)
    size_t refcount;
    // Flags about this object.
    flag_field flags;
    // INTERNAL POINTER - DO NOT MODIFY - The most recent object allocated in the linked list of all objects.
    object* next;
    // A pointer to metadata about this object. Can be NULL.
    object* meta;
    union {
        struct {
            union {
                object* car;
                void* car_ptr;
                char* car_str;
                int32_t car_int;
                float car_float;
            };
            union {
                object* cdr;
                void* cdr_ptr;
                function_pointer func;
                uint32_t cdr_uint;
                float cdr_float;
            };
        };
        int64_t as_integer;
        double as_double;
    };

    // Decrement the reference count of the object, and if it is now 0, finalize the object.
    inline void decref();

    // Increment the reference count of the object.
    inline void incref();

    // Test if a flag is set on the object.
    inline bool tst_flag(flag f);

    // Set a flag on an object.
    inline void set_flag(flag f);

    // Clear a flag on an object.
    inline void clr_flag(flag f);

    // Recursively marks the object, following pointers to other objects.
    void mark();

    private:
    object(const object_schema* schema, object* next, void* arg);
    ~object();
    // Finalize the object, that is, free any owned memory and decrement references to any pointed-to objects.
    void finalize();

    void init(const object_schema* schema, object* next, void* arg);

    friend class vm;
};

// The struct that is used to manage virtual threads in Tinobsy.
class thread {
    public:
    // The "virtual process ID" used to identify this thread.
    unsigned int vpid = 0;
    // The next thread in the linked list of all threads.
    thread* next_thread = NULL;
    // A pointer to the VM that created this thread.
    vm* owner;
    // The stack of objects that must not be collected by the garbage collector.
    object* gc_stack = NULL;
    // The current local environment (variables, etc).
    object* env = NULL;
    // NULL normally, but used to pass up an error when one is thrown.
    object* error = NULL;
    // The pointer to the current jmp_buf used to handle errors.
    jmp_buf* trycatch = NULL;
    // An opaque pointer to an OS-specific structure used to manage this thread.
    void* thread_handle = NULL;
    ~thread();

    // Raise an error on the thread by longjmp()'ing back to the last saved try-catch point. This function does not return.
    [[noreturn]] void raise(object* error, int sig);

    private:
    thread(vm* vm, unsigned int vpid, void* handle);
    friend class vm;
};

// The struct used to manage all objects and threads.
class vm {
    public:
    // The most recent object allocated.
    object* first;
    // The current number of objects that have been allocated.
    size_t num_objects;
    // An object that can be used to represent the NIL value and differentiate it from a NULL pointer.
    object* nil;
    // The linked list of all active threads.
    thread* threads;

    vm();
    ~vm();

    // Allocate a new object on this VM.
    object* allocate(const object_schema* schema, void* arg = NULL);

    // Garbage-collects all objects that don't have any active references, freeing their memory.
    size_t gc();
    virtual void mark_globals();

    // Gets the next unused VPID for threads on this VM.
    unsigned int next_vpid();

    // Pushes a new thread onto the VM's thread stack, and returns it.
    thread* push_thread();
};

// Default functions
namespace schema_functions {
    void mark_cons(object*);
    void finalize_cons(object*);

    void init_str(object*, void*);
    void finalize_str(object*);
}

// The typeinfo for the vm->nil member.
object_schema nil_schema("nil", NULL, NULL, NULL);

#define SET(x, y) do { \
    (y)->incref(); \
    (x)->decref(); \
    (x)=(y); \
} while (0)

#define UNSET(x) do { \
    (x)->decref(); \
    (x) = NULL; \
} while (0)

#define TRYCATCH(t, tc, cc) do { \
    jmp_buf dynamic; \
    jmp_buf* prev = (t)->trycatch; \
    (t)->trycatch = &dynamic; \
    bool thrown = false; \
    DBG("enter TRYCATCH macro"); \
    object* old_st = NULL; SET(old_st, (t)->gc_stack); \
    int sig = 0; \
    if (!(sig = setjmp(dynamic))) { \
        DBG("begin try block"); \
        tc; \
        DBG("no errors were thrown"); \
    } \
    else { \
        DBG("an error was thrown, code is %i", sig); \
        thrown = true; \
        SET((t)->gc_stack, old_st); \
    } \
    (t)->trycatch = prev; \
    if (thrown) { cc; } \
} while (0)

#define RAISE(th, err) (th)->raise((err), 1)

}

#include "tinobsy.cpp"
