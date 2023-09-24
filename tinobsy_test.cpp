#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <setjmp.h>
#define TINOBSY_DEBUG
#define TINOBSY_IGNORE_DANGLING_POINTERS
#include "tinobsy.hpp"

using namespace tinobsy;

class MyVM : public vm {
    public:
    object* stack;
    void mark_globals() {
        this->stack->mark();
    }
};

MyVM* VM;
const int times = 10;

void divider();
inline void divider() {
    printf("\n-------------------------------------------------------------\n\n");
}

object_schema nothing_type("nothing", NULL, NULL, NULL, NULL);
object_schema atom_type("atom", schema_functions::init_str, schema_functions::cmp_str, NULL, schema_functions::finalize_str);

void test_sweep() {
    DBG("test mark-sweep collector: objects are swept when not put into a thread");
    size_t oldobj = VM->num_objects;
    VM->allocate(&nothing_type);
    VM->allocate(&nothing_type);
    VM->allocate(&nothing_type);
    VM->allocate(&nothing_type);
    VM->allocate(&nothing_type);
    VM->allocate(&nothing_type);
    VM->allocate(&nothing_type);
    DBG("Begin sweep of everything");
    VM->gc();
    ASSERT(VM->num_objects == oldobj, "did not sweep right objects");
}

object_schema cons_type("cons", schema_functions::init_cons, NULL, schema_functions::mark_cons, schema_functions::finalize_cons);

void push(vm* v, object* thing, object*& stack) {
    object* cell = v->allocate(&cons_type, thing, stack);
    stack = cell;
}

void test_mark_no_sweep() {
    DBG("test mark-sweep collector: objects aren't swept when marked on globals");
    for (int i = 0; i < times; i++) {
        object* foo = VM->allocate(&nothing_type);
        push(VM, foo, VM->stack);
    }
    size_t oldobj = VM->num_objects;
    VM->gc();
    ASSERT(VM->num_objects == oldobj, "swept some by mistake");
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
        object* foo = VM->allocate(&atom_type, buffer);
    }
    VM->gc();
    DBG("Check Valgrind output to make sure stuff was freed");
}

void test_reference_cycle() {
    DBG("Test unreachable reference cycle gets collected");
    size_t oldobj = VM->num_objects;
    object* a = VM->allocate(&cons_type, NULL, NULL);
    a->cells[0].as_obj = a;
    a->cells[1].as_obj = a;
    VM->gc();
    ASSERT(VM->num_objects == oldobj, "a was not collected");
}

void initint(object* a, va_list args) {
    a->as_big_int = va_arg(args, int64_t);
}

const object_schema Integer("int", initint, schema_functions::obj_memcmp, NULL, NULL);
void test_interning() {
    DBG("Test primitives are interned");
    int64_t foo = 47;
    object* a = VM->allocate(&Integer, foo);
    ASSERT(a->as_big_int == 47, "did not copy right");
    for (int i = 0; i < times; i++) {
        object* b = VM->allocate(&Integer, foo);
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
