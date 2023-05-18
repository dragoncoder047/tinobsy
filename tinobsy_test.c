#include <stdio.h>
#define TINOBSY_DEBUG
#include "tinobsy.h"

tvm* VM;
const int times = 10;

void divider();
inline void divider() {
    printf("\n-------------------------------------------------------------\n\n");
}

ttype dummy_type = {"dummy", NOTHING, NOTHING};

void test_threads_stack() {
    divider();
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
    divider();
    DBG("test mark-sweep collector: objects are swept when not put into a thread");
    size_t oldobj = VM->num_objects;
    talloc(VM, &dummy_type);
    talloc(VM, &dummy_type);
    talloc(VM, &dummy_type);
    talloc(VM, &dummy_type);
    talloc(VM, &dummy_type);
    talloc(VM, &dummy_type);
    talloc(VM, &dummy_type);
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
    divider();
    DBG("test mark-sweep collector: objects aren't swept when owned by a thread and threads are freed properly");
    tthread* t = tpushthread(VM);
    for (int i = 0; i < times; i++) {
        tobject* foo = talloc(VM, &dummy_type);
        PUSH(VM, foo, t->gc_stack);
        tdecref(foo); // done with it
    }
    size_t oldobj = VM->num_objects;
    tmarksweep(VM);
    ASSERT(VM->num_objects == oldobj, "swept some by mistake");
    tfreethread(VM, t);
}

void test_gc_3() {
    divider();
    DBG("test refcount collector: can reuse garbage");
    size_t origobj = VM->num_objects;
    for (int i = 0; i < times; i++) {
        tobject* foo = talloc(VM, &dummy_type);
        tdecref(foo);
    }
    ASSERT(VM->num_objects - origobj < times, "didn't reuse objects");
}

void test_gc_4() {
    divider();
    DBG("test refcount collector: refs are counted properly");
    size_t oldrefs = VM->nil->refcount;
    for (int i = 0; i < times; i++) {
        PUSH(VM, VM->nil, VM->gc_stack);
    }
    ASSERT(VM->nil->refcount - oldrefs == times, "nil not referenced %i times", times);
}

typedef void (*test)(void);
test tests[] = {
    test_threads_stack,
    test_gc_1,
    test_gc_2,
    test_gc_3,
    test_gc_4
};
const int num_tests = sizeof(tests) / sizeof(tests[0]);

int main() {
    VM = tnewvm();
    for (int i = 0; i < num_tests; i++) tests[i]();
    divider();
    DBG("end of tests");
    divider();
    tfreevm(VM);
    return 0;
}
