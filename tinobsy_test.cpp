#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <setjmp.h>
#define TINOBSY_DEBUG
#define TINOBSY_IGNORE_DANGLING_POINTERS
#include "tinobsy.hpp"

using namespace tinobsy;

vm* VM;
const int times = 10;

void divider();
inline void divider() {
    printf("\n-------------------------------------------------------------\n\n");
}

object_schema nothing_type("nothing", NULL, NULL, NULL, NULL);
object_schema atom_type("atom", schema_functions::init_str, schema_functions::cmp_str, NULL, schema_functions::finalize_str);

void test_threads_stack() {
    DBG("test threads stack");
    thread* a = VM->push_thread();
    thread* b = VM->push_thread();
    thread* c = VM->push_thread();
    delete b;
    delete c;
    delete a;
    ASSERT(VM->threads == NULL, "did not remove all threads");
}

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

object_schema cons_type("cons", NULL, NULL, schema_functions::mark_cons, NULL);
object* cons(vm* v, object* x, object* y) {
    ASSERT(x != NULL || y != NULL);
    object* cell = v->allocate(&cons_type);
    cell->car = x;
    cell->cdr = y;
    return cell;
}

void push(vm* v, object* thing, object*& stack) {
    object* cell = cons(v, thing, stack);
    stack = cell;
}

void test_mark_no_sweep() {
    DBG("test mark-sweep collector: objects aren't swept when owned by a thread and threads are freed properly");
    thread* t = VM->push_thread();
    for (int i = 0; i < times; i++) {
        object* foo = VM->allocate(&nothing_type);
        push(VM, foo, t->gc_stack);
    }
    size_t oldobj = VM->num_objects;
    VM->gc();
    ASSERT(VM->num_objects == oldobj, "swept some by mistake");
    delete t;
}

char randchar() {
    return "qwertyuiopasdfghjklzxcvbnm"[rand() % 26];
}

void test_freeing_things() {
    DBG("Test owned pointers are freed");
    char buffer[64];
    for (int i = 0; i < times; i++) {
        snprintf(buffer, sizeof(buffer), "%c%c%c%c%c%c", randchar(),randchar(),randchar(),randchar(),randchar(),randchar());
        DBG("Random atom is %s", buffer);
        object* foo = VM->allocate(&atom_type, buffer);
    }
    VM->gc();
    DBG("Check Valgrind output to make sure stuff was freed");
}

void test_reference_cycle() {
    DBG("Test unreachable reference cycle gets collected");
    size_t oldobj = VM->num_objects;
    object* a = VM->allocate(&cons_type);
    a->car = a;
    a->cdr = a;
    VM->gc();
    ASSERT(VM->num_objects == oldobj, "A was collected");
}

void test_setjmp() {
    DBG("Test try-catch setjmp with error object");
    thread* t = VM->push_thread();
    TRYCATCH(t, {
        object* x = VM->allocate(&atom_type, (void*)"foobar");
        t->raise(x);
        ASSERT(false, "unreachable");
    }, {
        ASSERT(t->error != NULL && !strcmp(t->error->car_str, "foobar"), "bad error");
    });
    delete t;
}

void test_catch_code() {
    DBG("Test try-catch setjmp wth custom error code");
    thread* t = VM->push_thread();
    TRYCATCH(t, {
        t->raise(NULL, 22);
        ASSERT(false, "unreachable");
    }, {
        ASSERT(sig == 22, "didn't throw code 22");
    });
    delete t;
}

void initint(object* a, void* i, void* _, void* __) {
    a->as_integer = *(int64_t*)i;
}

const object_schema Integer("int", initint, schema_functions::obj_memcmp, NULL, NULL);
void test_interning() {
    DBG("Test primitives are interned");
    int64_t foo = 47;
    object* a = VM->allocate(&Integer, &foo);
    ASSERT(a->as_integer == 47, "did not copy right");
    for (int i = 0; i < times; i++) {
        object* b = VM->allocate(&Integer, &foo);
        ASSERT(a == b, "not interned");
    }
}

typedef void (*test)();
test tests[] = {
    test_threads_stack,
    test_sweep,
    test_mark_no_sweep,
    test_freeing_things,
    test_reference_cycle,
    test_setjmp,
    test_catch_code,
    test_interning,
};
const int num_tests = sizeof(tests) / sizeof(tests[0]);

int main() {
    srand(time(NULL));
    DBG("Begin Tinobsy test suite");
    DBG("sizeof(object) = %zu, sizeof(thread) = %zu, sizeof(vm) = %zu", sizeof(object), sizeof(thread), sizeof(vm));
    VM = new vm();
    for (int i = 0; i < num_tests; i++) {
        divider();
        tests[i]();
    }
    divider();
    DBG("end of tests");
    delete VM;
    return 0;
}
