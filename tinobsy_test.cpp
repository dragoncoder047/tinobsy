#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <setjmp.h>
#define TINOBSY_DEBUG
#include "tinobsy.hpp"

using namespace tinobsy;

class MyVM : public vm {
    public:
    object* stack;
    void mark_globals() {
        this->markobject(this->stack);
    }
};

MyVM* VM;
const int times = 10;

void divider();
inline void divider() {
    printf("\n-------------------------------------------------------------\n\n");
}

object_type nothing_type("nil", NULL, NULL, NULL);
object_type atom_type("atom", NULL, NULL, NULL);

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
    ASSERT(VM->freespace == oldobj, "did not sweep right objects %zu %zu", VM->freespace, oldobj);
}

object_type cons_type("cons", markcons, NULL, NULL);

void push(vm* v, object* thing, object*& stack) {
    object* cell = v->alloc(&cons_type);
    cell->car = thing;
    cell->cdr = stack;
    stack = cell;
}

void test_mark_no_sweep() {
    DBG("test mark-sweep collector: objects aren't swept when marked on globals");
    for (int i = 0; i < times; i++) {
        object* foo = VM->alloc(&nothing_type);
        push(VM, foo, VM->stack);
    }
    size_t oldobj = VM->freespace;
    VM->gc();
    ASSERT(VM->freespace == oldobj, "swept some by mistake");
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
        TODO(allocate string (void)buffer;);
    }
    VM->gc();
    DBG("Check Valgrind output to make sure stuff was freed");
}

void test_reference_cycle() {
    DBG("Test unreachable reference cycle gets collected");
    size_t oldobj = VM->freespace;
    object* a = VM->alloc(&cons_type);
    a->car = a;
    a->cdr = a;
    VM->gc();
    ASSERT(VM->freespace == oldobj, "a was not collected");
}

const object_type Integer("int", NULL, NULL, NULL);

object* makeint(vm* vm, int64_t z) {
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
        ASSERT(a == b, "not interned");
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
    DBG("sizeof(object) = %zu, sizeof(vm) = %zu", sizeof(object), sizeof(vm));
    VM = new MyVM();
    for (int i = 0; i < num_tests; i++) {
        divider();
        tests[i]();
    }
    divider();
    DBG("end of tests");
    delete VM;
    return 0;
}
