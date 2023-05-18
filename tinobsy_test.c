#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define TINOBSY_DEBUG
#include "tinobsy.h"

tvm* VM;
const int times = 10;

void divider();
inline void divider() {
    printf("\n-------------------------------------------------------------\n\n");
}

ttype nothing_type = {"nothing", NOTHING, NOTHING};
ttype atom_type = {"atom", OWNED_PTR, NOTHING};

void test_threads_stack() {
    DBG("test threads stack");
    tthread* a = tpushthread(VM);
    tthread* b = tpushthread(VM);
    tthread* c = tpushthread(VM);
    tfreethread(VM, b);
    tfreethread(VM, c);
    tfreethread(VM, a);
    ASSERT(VM->threads == NULL, "did not remove all threads");
}

void test_gc_1() {
    DBG("test mark-sweep collector: objects are swept when not put into a thread");
    size_t oldobj = VM->num_objects;
    talloc(VM, &nothing_type);
    talloc(VM, &nothing_type);
    talloc(VM, &nothing_type);
    talloc(VM, &nothing_type);
    talloc(VM, &nothing_type);
    talloc(VM, &nothing_type);
    talloc(VM, &nothing_type);
    DBG("Begin sweep of everything");
    tmarksweep(VM);
    ASSERT(VM->num_objects == oldobj, "did not sweep right objects");
}

ttype cons_type = {"cons", OBJECT, OBJECT};
tobject* cons(tvm* vm, tobject* x, tobject* y) {
    tobject* cell = talloc(vm, &cons_type);
    SET(cell->car, x);
    SET(cell->cdr, y);
    return cell;
}

#define PUSH(vm, x, y) do{tobject* cell__=cons((vm),(x),(y));SET(y,cell__);tdecref(cell__);}while(0)

void test_gc_2() {
    DBG("test mark-sweep collector: objects aren't swept when owned by a thread and threads are freed properly");
    tthread* t = tpushthread(VM);
    for (int i = 0; i < times; i++) {
        tobject* foo = talloc(VM, &nothing_type);
        PUSH(VM, foo, t->gc_stack);
        tdecref(foo); // done with it
    }
    size_t oldobj = VM->num_objects;
    tmarksweep(VM);
    ASSERT(VM->num_objects == oldobj, "swept some by mistake");
    tfreethread(VM, t);
}

void test_gc_3() {
    DBG("test refcount collector: can reuse garbage");
    size_t origobj = VM->num_objects;
    for (int i = 0; i < times; i++) {
        tobject* foo = talloc(VM, &nothing_type);
        tdecref(foo);
    }
    ASSERT(VM->num_objects - origobj < times, "didn't reuse objects");
}

void test_gc_4() {
    DBG("test refcount collector: refs are counted properly");
    size_t oldrefs = VM->nil->refcount;
    for (int i = 0; i < times; i++) {
        PUSH(VM, VM->nil, VM->gc_stack);
    }
    ASSERT(VM->nil->refcount - oldrefs == times, "nil not referenced %i times", times);
    VM->gc_stack = NULL;
    tmarksweep(VM);
}

char randchar() {
    return "qwertyuiopasdfghjklzxcvbnm"[rand() % 26];
}

void test_freeing_things() {
    DBG("Test owned pointers, are freed");
    char buffer[64];
    for (int i = 0; i < times; i++) {
        snprintf(buffer, sizeof(buffer), "%c%c%c%c%c%c", randchar(),randchar(),randchar(),randchar(),randchar(),randchar());
        tobject* foo = talloc(VM, &atom_type);
        foo->car_str = strdup(buffer);
        DBG("Random atom is %s", buffer);
    }
    tmarksweep(VM);
    DBG("Check Valgrind output to make sure stuff was freed");
}

typedef void (*test)(void);
test tests[] = {
    test_threads_stack,
    test_gc_1,
    test_gc_2,
    test_gc_3,
    test_gc_4,
    test_freeing_things
};
const int num_tests = sizeof(tests) / sizeof(tests[0]);

int main() {
    srand(time(NULL));
    DBG("Begin Tinobsy test suite");
    VM = tnewvm();
    for (int i = 0; i < num_tests; i++) {
        divider();
        tests[i]();
    }
    divider();
    DBG("end of tests");
    divider();
    tfreevm(VM);
    return 0;
}
