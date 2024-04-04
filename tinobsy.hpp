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

#define car(x) ((x)->car)
#define cdr(x) ((x)->cdr)
#define caar(x) car(car(x))
#define cadr(x) car(cdr(x))
#define cdar(x) cdr(car(x))
#define cddr(x) cdr(cdr(x))
#define caaar(x) car(caar(x))
#define caadr(x) car(cadr(x))
#define cadar(x) car(cdar(x))
#define caddr(x) car(cddr(x))
#define cdaar(x) cdr(caar(x))
#define cdadr(x) cdr(cadr(x))
#define cddar(x) cdr(cdar(x))
#define cdddr(x) cdr(cddr(x))
#define caaaar(x) car(caaar(x))
#define caaadr(x) car(caadr(x))
#define caadar(x) car(cadar(x))
#define caaddr(x) car(caddr(x))
#define cadaar(x) car(cdaar(x))
#define cadadr(x) car(cdadr(x))
#define caddar(x) car(cddar(x))
#define cadddr(x) car(cdddr(x))
#define cdaaar(x) cdr(caaar(x))
#define cdaadr(x) cdr(caadr(x))
#define cdadar(x) cdr(cadar(x))
#define cdaddr(x) cdr(caddr(x))
#define cddaar(x) cdr(cdaar(x))
#define cddadr(x) cdr(cdadr(x))
#define cdddar(x) cdr(cddar(x))
#define cddddr(x) cdr(cdddr(x))
#define caaaaar(x) car(caaaar(x))
#define caaaadr(x) car(caaadr(x))
#define caaadar(x) car(caadar(x))
#define caaaddr(x) car(caaddr(x))
#define caadaar(x) car(cadaar(x))
#define caadadr(x) car(cadadr(x))
#define caaddar(x) car(caddar(x))
#define caadddr(x) car(cadddr(x))
#define cadaaar(x) car(cdaaar(x))
#define cadaadr(x) car(cdaadr(x))
#define cadadar(x) car(cdadar(x))
#define cadaddr(x) car(cdaddr(x))
#define caddaar(x) car(cddaar(x))
#define caddadr(x) car(cddadr(x))
#define cadddar(x) car(cdddar(x))
#define caddddr(x) car(cddddr(x))
#define cdaaaar(x) cdr(caaaar(x))
#define cdaaadr(x) cdr(caaadr(x))
#define cdaadar(x) cdr(caadar(x))
#define cdaaddr(x) cdr(caaddr(x))
#define cdadaar(x) cdr(cadaar(x))
#define cdadadr(x) cdr(cadadr(x))
#define cdaddar(x) cdr(caddar(x))
#define cdadddr(x) cdr(cadddr(x))
#define cddaaar(x) cdr(cdaaar(x))
#define cddaadr(x) cdr(cdaadr(x))
#define cddadar(x) cdr(cdadar(x))
#define cddaddr(x) cdr(cdaddr(x))
#define cdddaar(x) cdr(cddaar(x))
#define cdddadr(x) cdr(cddadr(x))
#define cddddar(x) cdr(cdddar(x))
#define cdddddr(x) cdr(cddddr(x))
#define caaaaaar(x) car(caaaaar(x))
#define caaaaadr(x) car(caaaadr(x))
#define caaaadar(x) car(caaadar(x))
#define caaaaddr(x) car(caaaddr(x))
#define caaadaar(x) car(caadaar(x))
#define caaadadr(x) car(caadadr(x))
#define caaaddar(x) car(caaddar(x))
#define caaadddr(x) car(caadddr(x))
#define caadaaar(x) car(cadaaar(x))
#define caadaadr(x) car(cadaadr(x))
#define caadadar(x) car(cadadar(x))
#define caadaddr(x) car(cadaddr(x))
#define caaddaar(x) car(caddaar(x))
#define caaddadr(x) car(caddadr(x))
#define caadddar(x) car(cadddar(x))
#define caaddddr(x) car(caddddr(x))
#define cadaaaar(x) car(cdaaaar(x))
#define cadaaadr(x) car(cdaaadr(x))
#define cadaadar(x) car(cdaadar(x))
#define cadaaddr(x) car(cdaaddr(x))
#define cadadaar(x) car(cdadaar(x))
#define cadadadr(x) car(cdadadr(x))
#define cadaddar(x) car(cdaddar(x))
#define cadadddr(x) car(cdadddr(x))
#define caddaaar(x) car(cddaaar(x))
#define caddaadr(x) car(cddaadr(x))
#define caddadar(x) car(cddadar(x))
#define caddaddr(x) car(cddaddr(x))
#define cadddaar(x) car(cdddaar(x))
#define cadddadr(x) car(cdddadr(x))
#define caddddar(x) car(cddddar(x))
#define cadddddr(x) car(cdddddr(x))
#define cdaaaaar(x) cdr(caaaaar(x))
#define cdaaaadr(x) cdr(caaaadr(x))
#define cdaaadar(x) cdr(caaadar(x))
#define cdaaaddr(x) cdr(caaaddr(x))
#define cdaadaar(x) cdr(caadaar(x))
#define cdaadadr(x) cdr(caadadr(x))
#define cdaaddar(x) cdr(caaddar(x))
#define cdaadddr(x) cdr(caadddr(x))
#define cdadaaar(x) cdr(cadaaar(x))
#define cdadaadr(x) cdr(cadaadr(x))
#define cdadadar(x) cdr(cadadar(x))
#define cdadaddr(x) cdr(cadaddr(x))
#define cdaddaar(x) cdr(caddaar(x))
#define cdaddadr(x) cdr(caddadr(x))
#define cdadddar(x) cdr(cadddar(x))
#define cdaddddr(x) cdr(caddddr(x))
#define cddaaaar(x) cdr(cdaaaar(x))
#define cddaaadr(x) cdr(cdaaadr(x))
#define cddaadar(x) cdr(cdaadar(x))
#define cddaaddr(x) cdr(cdaaddr(x))
#define cddadaar(x) cdr(cdadaar(x))
#define cddadadr(x) cdr(cdadadr(x))
#define cddaddar(x) cdr(cdaddar(x))
#define cddadddr(x) cdr(cdadddr(x))
#define cdddaaar(x) cdr(cddaaar(x))
#define cdddaadr(x) cdr(cddaadr(x))
#define cdddadar(x) cdr(cddadar(x))
#define cdddaddr(x) cdr(cddaddr(x))
#define cddddaar(x) cdr(cdddaar(x))
#define cddddadr(x) cdr(cdddadr(x))
#define cdddddar(x) cdr(cddddar(x))
#define cddddddr(x) cdr(cdddddr(x))

}

#include "tinobsy.cpp"
