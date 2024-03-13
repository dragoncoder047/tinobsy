#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <setjmp.h>
#define TINOBSY_DEBUG
#include "tinobsy.hpp"

using namespace tinobsy;


object_type cons_type("cons", markcons, NULL, NULL);
class MyVM : public vm {
    public:
    object* stack;
    void mark_globals() {
        this->markobject(this->stack);
    }
    void push(object* thing, object*& stack) {
        object* cell = this->alloc(&cons_type);
        cell->car = thing;
        cell->cdr = stack;
        stack = cell;
    }
    void push(object* thing) {
        push(thing, this->stack);
    }
};

MyVM* VM;
const int times = 10;
size_t fails = 0;
#define TEST(cond, ...) do { \
    if (!(cond)) { \
        DBG("Test failed: %s", #cond); \
        DBG(__VA_ARGS__); \
        fprintf(stderr, "Test failed %s\n", #cond); \
        fails++; \
    } else { \
        DBG("Test succeeded: %s", #cond); \
    } \
} while(0)

void divider();

inline void divider() {
    printf("\n-------------------------------------------------------------\n\n");
}

void free_string(object* self) {
    free(self->as_chars);
}

object_type nothing_type("nil", NULL, NULL, NULL);
object_type atom_type("atom", NULL, free_string, NULL);

void test_sweep() {
    DBG("test mark-sweep collector: objects are swept when not put into a thread");
    size_t oldobj = VM->freespace;
    VM->alloc(&nothing_type);
    VM->alloc(&nothing_type);
    VM->alloc(&nothing_type);
    VM->alloc(&nothing_type);
    VM->alloc(&nothing_type);
    VM->alloc(&nothing_type);
    VM->alloc(&nothing_type);
    DBG("Begin sweep of everything");
    VM->gc();
    TEST(VM->freespace == oldobj, "did not sweep right objects %zu %zu", VM->freespace, oldobj);
}

void test_mark_no_sweep() {
    DBG("test mark-sweep collector: objects aren't swept when marked on globals");
    for (int i = 0; i < times; i++) {
        object* foo = VM->alloc(&nothing_type);
        VM->push(foo);
    }
    size_t oldobj = VM->freespace;
    VM->gc();
    TEST(VM->freespace == oldobj, "swept some by mistake");
}

char randchar() {
    return "qwertyuiopasdfghjklzxcvbnm"[rand() % 26];
}

void test_freeing_things() {
    DBG("Test owned pointers are freed");
    char buffer[64] = { 0 };
    for (int i = 0; i < times; i++) {
        for (int j = 0; j < 63; j++) buffer[j] = randchar();
        DBG("Random atom is %s", buffer);
        object* foo = VM->alloc(&atom_type);
        foo->as_chars = strdup(buffer);
    }
    VM->gc();
    DBG("Check Valgrind output to make sure stuff was freed");
}

void test_reference_cycle() {
    DBG("Test unreachable reference cycle gets collected");
    size_t oldobj = VM->freespace;
    object* a = VM->alloc(&cons_type);
    car(a) = cdr(a) = a;
    VM->gc();
    TEST(VM->freespace == oldobj, "a was not collected");
}

const object_type Integer("int", NULL, NULL, NULL);

object* makeint(vm* vm, int64_t z) {
    INTERN(vm, int64_t, &Integer, z);
    object* x = vm->alloc(&Integer);
    x->as_big_int = z;
    return x;
}

void test_interning() {
    DBG("Test primitives are interned");
    int64_t foo = 47;
    object* a = makeint(VM, foo);
    ASSERT(a->as_big_int == 47, "did not copy right");
    for (int i = 0; i < times; i++) {
        object* b = makeint(VM, foo);
        TEST(a == b, "not interned");
    }
}

typedef void (*test)();
test tests[] = {
    test_sweep,
    test_mark_no_sweep,
    test_freeing_things,
    test_reference_cycle,
    test_interning,
};
const int num_tests = sizeof(tests) / sizeof(tests[0]);

int main() {
    srand(time(NULL));
    DBG("Begin Tinobsy test suite");
    DBG("sizeof(object) = %zu, sizeof(vm) = %zu, sizeof(chunk) = %zu", sizeof(object), sizeof(vm), sizeof(chunk));
    VM = new MyVM();
    for (int i = 0; i < num_tests; i++) {
        divider();
        tests[i]();
    }
    divider();
    DBG("end of tests. Total %zu fails", fails);
    fprintf(stderr, "%zu tests failed\n", fails);
    delete VM;
    return 0;
}
