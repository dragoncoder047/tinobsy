#pragma once

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <ccomplex>

#ifndef TINOBSY_CHUNK_SIZE
#define TINOBSY_CHUNK_SIZE 128
#endif

#ifdef TINOBSY_DEBUG
#define TINOBSY_DEBUG_N 1
#else
#define TINOBSY_DEBUG_N 0
#endif

#ifdef __PRETTY_FUNCTION__
#define __WHERE__ __PRETTY_FUNCTION__
#else
#define __WHERE__ __func__
#endif

#define DBG(s, ...) do { if (TINOBSY_DEBUG_N) printf("[%s %s:%i] " s "\n", __WHERE__, __FILE__, __LINE__, ##__VA_ARGS__); } while (0)
#define ASSERT(cond, ...) do { \
    if (TINOBSY_DEBUG_N) { \
        if (!(cond)) { \
            DBG("Assertion FAILED: %s", #cond); \
            DBG(__VA_ARGS__); \
            fprintf(stderr, "Assertion %s (in %s, line %i of %s) FAILED\n", #cond, __WHERE__, __LINE__, __FILE__); \
            abort(); \
        } else { \
            DBG("Assertion succeeded: %s", #cond); \
        } \
    } \
} while(0)
#define TODO(nm) do { DBG("%s: %s", #nm, strerror(ENOSYS)); errno = ENOSYS; perror(#nm); exit(74); } while (0)

namespace tinobsy {

// Forward declare
class object;
class vm;
class chunk;

// Information about the contents of the object struct.
class object_type {
    public:
    // The "name" of the object's type.
    const char* const name;
    // The function to mark the object Returns a pointer to allow tail-recursion optimization.
    object* (*mark)(vm*, object*);
    void (*free)(object*);
    void (*print)(object* obj, object* stream);
    object_type(const char* const name, object* (*mark)(vm*, object*), void (*free)(object*), void (*print)(object* obj, object* stream));
    ~object_type() = default;
};

// A bit-field to store flags information.
typedef uint16_t flag_field;

// The enum of flags currently in use by Tinobsy.
typedef enum {
    GC_MARKED = 15, // One less than number of bits
} flag;

// The struct that makes up all Tinobsy objects.
class object {
    public:
    // A pointer to this object's type information. Cannot be NULL.
    object_type* type = NULL;
    // Flags about this object.
    flag_field flags = 0;
    // A pointer to metadata about this object. Can be NULL.
    union { object* meta = NULL; object* car; };
    // The object's payload -- can be anything.
    union {
        void* as_ptr = NULL;
        char* as_chars;
        int64_t as_big_int;
        double as_double;
        object* as_obj; object* cdr;
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

    object()  = default;
    ~object() = default;
    private:
    void clean_up();

    friend class vm;
};

// Chunks of memory, to reduce the frequency of free() and calloc() calls.
class chunk {
    chunk* next = NULL;
    object d[TINOBSY_CHUNK_SIZE];
    chunk() = default;
    chunk(chunk* next) : next(next) {};
    bool is_all_marked();
    friend class vm;
};

// The struct used to manage all objects and threads.
class vm {
    public:
    // The list of all chunks allocated
    chunk* chunks = NULL;
    // The current number of chunks that have been allocated.
    size_t n_chunks = 0;
    // The list of free objects
    object* freelist = NULL;
    // The number of free objects available
    size_t freespace = 0;

    vm() = default;
    ~vm();

    // Allocate a new object on this VM.
    object* alloc(const object_type*);

    // Garbage-collects all objects that don't have any active references.
    // Then deletes all the chunks that don't contain any in-use objects.
    size_t gc();
    virtual void mark_globals() {};

    void markobject(object*);

    template <class field_type>
    object* get_existing_object(object_type*, field_type, bool (*)(field_type, field_type));

    private:
    void release(object*);
};

template <class type>
bool op_eq(type, type);

// Default functions
object* markcons(vm*, object*);

#define INTERN(vm, typ, sch, val) INTERN_PTR((vm), typ, (sch), (val), ::tinobsy::op_eq<typ>)

#define INTERN_PTR(vm, typ, sch, val, cmp) do { \
    object* maybe = (vm)->get_existing_object<typ>((::tinobsy::object_type*)(sch), (val), (cmp)); \
    if (maybe) return maybe; \
} while (0)

#include "cxxxr.h"

}

#include "tinobsy.cpp"
