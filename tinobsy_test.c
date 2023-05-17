#include <stdio.h>
#define TINOBSY_DEBUG
#include "tinobsy.h"

void divider();
inline void divider() {
    printf("\n-------------------------------------------------------------\n\n");
}

ttype dummy_type = {"dummy", NOTHING, NOTHING};

void test_gc_1() {
    divider();
    DBG("test mark-sweep collector: objects are swept when not put into a thread");
    tvm* vm = tnewvm();
    talloc(vm, &dummy_type);
    talloc(vm, &dummy_type);
    talloc(vm, &dummy_type);
    talloc(vm, &dummy_type);
    talloc(vm, &dummy_type);
    talloc(vm, &dummy_type);
    talloc(vm, &dummy_type);
    DBG("Begin sweep of everything");
    tfreevm(vm);
}

ttype cons_type = {"cons", OBJECT, OBJECT};
tobject* cons(tvm* vm, tobject* x, tobject* y) {
    tobject* cell = talloc(vm, &cons_type);
    SET(cell->car, x);
    SET(cell->cdr, y);
    return cell;
}

#define PUSH(vm, x, y) ((y) = cons((vm), (x), (y)))

void test_gc_2() {
    divider();
    DBG("test mark-sweep collector: objects aren't swept when owned by a thread and threads are freed properly");
    tvm* vm = tnewvm();
    tthread* t = tpushthread(vm);
    PUSH(vm, talloc(vm, &dummy_type), t->gc_stack);
    PUSH(vm, talloc(vm, &dummy_type), t->gc_stack);
    PUSH(vm, talloc(vm, &dummy_type), t->gc_stack);
    PUSH(vm, talloc(vm, &dummy_type), t->gc_stack);
    size_t oldobj = vm->num_objects;
    tmarksweep(vm);
    ASSERT(vm->num_objects == oldobj, "swept some by mistake");
    tfreevm(vm);
}

void test_gc_3() {
    divider();
    DBG("test refcount collector: can reuse garbage");
    tvm* vm = tnewvm();
    size_t origobj = vm->num_objects;
    for (int i = 0; i < 10; i++) {
        tobject* foo = talloc(vm, &dummy_type);
        tdecref(foo);
    }
    ASSERT(vm->num_objects - origobj == 1, "didn't reuse objects");
    tfreevm(vm);
}

void test_gc_4() {
    divider();
    DBG("test refcount collector: refs are counted properly");
    tvm* vm = tnewvm();
    size_t oldrefs = vm->nil->refcount;
    const int times = 10;
    for (int i = 0; i < times; i++) {
        PUSH(vm, vm->nil, vm->gc_stack);
    }
    ASSERT(vm->nil->refcount - oldrefs == times, "nil not referenced %i times", times);
    tfreevm(vm);
}

int main() {
    test_gc_1();
    test_gc_2();
    test_gc_3();
    test_gc_4();
    DBG("end of tests");
    return 0;
}
