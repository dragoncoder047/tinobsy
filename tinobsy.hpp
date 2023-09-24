#pragma once

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdarg>

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
class vm;

typedef void (*init_function)(object* self, va_list args);
typedef int (*cmp_function)(object* a, object* b);
typedef void (*mark_function)(object* self);
typedef void (*finalize_function)(object* self);

// Information about the contents of the object struct.
class object_schema {
    public:
    // The "name" of the object's schema.
    const char* const name;
    // The function to set up the object.
    init_function init;
    // The function to compare two objects of the same type when interning.
    cmp_function cmp;
    // The function to mark the object.
    mark_function mark;
    // The function to finalize the object.
    finalize_function finalize;
    object_schema(const char* const name, init_function init, cmp_function cmp, mark_function mark, finalize_function finalize);
    ~object_schema();
};

// A bit-field to store flags information.
typedef uint16_t flag_field;

// The enum of flags currently in use by Tinobsy.
typedef enum {
    GC_MARKED = 15; // One less than number of bits
} flag;

typedef union {
    object* as_obj;
    int32_t as_int;
    uint32_t as_uint;
    float as_float;
    void* as_ptr;
} cell;

// The struct that makes up all Tinobsy objects.
class object {
    const object* next;
    public:
    // A pointer to this object's schema information. Cannot be NULL.
    const object_schema* schema;
    // Flags about this object.
    flag_field flags = 0;
    // A pointer to metadata about this object. Can be NULL.
    object* meta = NULL;
    // The object's payload -- can be anything.
    union {
        void* as_ptr = NULL;
        char* as_chars;
        cell* cells;
        int64_t as_big_int;
        double as_double;
        #ifdef _Complex
        float _Complex as_complex;
        #endif
    };

    // Test if a flag is set on the object.
    inline bool tst_flag(flag f) {
        return this != NULL && (this->flags & (1<<f)) != 0;
    };

    // Set a flag on an object.
    inline void set_flag(flag f) {
        if (this != NULL) this->flags |= 1 << f;
    };

    // Clear a flag on an object.
    inline void clr_flag(flag f) {
        if (this != NULL) this->flags &= ~(1 << f);
    };

    // Recursively marks the object, following pointers to other objects.
    void mark();

    private:
    object(const object_schema* schema, object* next, va_list args);
    ~object();

    friend class vm;
};

// The struct used to manage all objects and threads.
class vm {
    public:
    // The most recent object allocated.
    object* first = NULL;
    // The current number of objects that have been allocated.
    size_t num_objects = 0;

    vm() = default;
    ~vm();

    // Allocate a new object on this VM.
    object* allocate(const object_schema* schema, ...);

    // Garbage-collects all objects that don't have any active references, freeing their memory.
    size_t gc();
    virtual void mark_globals() {};
};

// Default functions
namespace schema_functions {
    void init_cons(object*, va_list);
    void mark_cons(object*);
    void finalize_cons(object*);

    void init_str(object*, va_list);
    int cmp_str(object*, object*);
    void finalize_str(object*);

    int obj_memcmp(object*, object*);
}

}

#include "tinobsy.cpp"
