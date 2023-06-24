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

object_schema nothing_type = {"nothing", NOTHING, NOTHING};
object_schema atom_type = {"atom", OWNED_PTR, NOTHING};

void test_threads_stack() {
    DBG("test threads stack");
    thread* a = push_thread(VM);
    thread* b = push_thread(VM);
    thread* c = push_thread(VM);
    free_thread(b);
    free_thread(c);
    free_thread(a);
    ASSERT(VM->threads == NULL, "did not remove all threads");
}

void test_sweep() {
    DBG("test mark-sweep collector: objects are swept when not put into a thread");
    size_t oldobj = VM->num_objects;
    allocate(VM, &nothing_type);
    allocate(VM, &nothing_type);
    allocate(VM, &nothing_type);
    allocate(VM, &nothing_type);
    allocate(VM, &nothing_type);
    allocate(VM, &nothing_type);
    allocate(VM, &nothing_type);
    DBG("Begin sweep of everything");
    gc(VM);
    ASSERT(VM->num_objects == oldobj, "did not sweep right objects");
}

object_schema cons_type = {"cons", OBJECT, OBJECT};
object* cons(vm* vm, object* x, object* y) {
    ASSERT(x != NULL || y != NULL);
    object* cell = allocate(vm, &cons_type);
    SET(cell->car, x);
    SET(cell->cdr, y);
    return cell;
}

#define PUSH(vm, x, y) do{object* cell__=cons((vm), (x),(y));SET(y,cell__);decref(cell__);}while(0)

void test_mark_no_sweep() {
    DBG("test mark-sweep collector: objects aren't swept when owned by a thread and threads are freed properly");
    thread* t = push_thread(VM);
    for (int i = 0; i < times; i++) {
        object* foo = allocate(VM, &nothing_type);
        PUSH(VM, foo, t->gc_stack);
        decref(foo); // done with it
    }
    size_t oldobj = VM->num_objects;
    gc(VM);
    ASSERT(VM->num_objects == oldobj, "swept some by mistake");
    free_thread(t);
}

void test_reuse_garbage() {
    DBG("test refcount collector: can reuse garbage");
    size_t origobj = VM->num_objects;
    for (int i = 0; i < times; i++) {
        object* foo = allocate(VM, &nothing_type);
        decref(foo);
    }
    ASSERT(VM->num_objects - origobj < times, "didn't reuse objects");
}

void test_refcounting() {
    DBG("test refcount collector: refs are counted properly");
    size_t oldrefs = VM->nil->refcount;
    thread* x = push_thread(VM);
    for (int i = 0; i < times; i++) {
        PUSH(VM, VM->nil, x->gc_stack);
    }
    ASSERT(VM->nil->refcount - oldrefs == times, "nil not referenced %i times", times);
    SET(x->gc_stack, NULL);
    free_thread(x);
    gc(VM);
}

char randchar() {
    return "qwertyuiopasdfghjklzxcvbnm"[rand() % 26];
}

void test_freeing_things() {
    DBG("Test owned pointers, are freed");
    char buffer[64];
    for (int i = 0; i < times; i++) {
        snprintf(buffer, sizeof(buffer), "%c%c%c%c%c%c", randchar(),randchar(),randchar(),randchar(),randchar(),randchar());
        object* foo = allocate(VM, &atom_type);
        foo->car_str = strdup(buffer);
        DBG("Random atom is %s", buffer);
    }
    gc(VM);
    DBG("Check Valgrind output to make sure stuff was freed");
}

void test_reference_cycle() {
    DBG("Test reference cycle, collected");
    size_t oldobj = VM->num_objects;
    object* a = allocate(VM, &cons_type);
    SET(a->car, a);
    SET(a->cdr, a);
    decref(a);
    ASSERT(a->type != NULL, "A shouldn't be finalized");
    ASSERT(a->refcount > 0, "A is not in reference cycle");
    gc(VM);
    ASSERT(VM->num_objects == oldobj, "A was collected");
}

void test_setjmp() {
    DBG("Test try-catch setjmp with error object");
    thread* t = push_thread(VM);
    TRYCATCH(t, {
        object* x = allocate(VM, &atom_type);
        x->car_str = strdup("foobar");
        RAISE(t, x);
        ASSERT(false, "unreachable");
    }, {
        ASSERT(t->error != NULL && !strcmp(t->error->car_str, "foobar"), "bad error");
    });
    free_thread(t);
}

void test_catch_code() {
    DBG("Test try-catch setjmp wth custom error code");
    thread* t = push_thread(VM);
    TRYCATCH(t, {
        raise(t, NULL, 22);
        ASSERT(false, "unreachable");
    }, {
        ASSERT(sig == 22, "didn't throw code 22");
    });
    free_thread(t);
}

typedef void (*test)(void);
test tests[] = {
    test_threads_stack,
    test_sweep,
    test_mark_no_sweep,
    test_reuse_garbage,
    test_refcounting,
    test_freeing_things,
    test_reference_cycle,
    test_setjmp,
    test_catch_code,
};
const int num_tests = sizeof(tests) / sizeof(tests[0]);

int main() {
    srand(time(NULL));
    DBG("Begin Tinobsy test suite");
    DBG("sizeof(object) = %zu, sizeof(thread) = %zu, sizeof(vm) = %zu", sizeof(object), sizeof(thread), sizeof(vm));
    VM = new_vm();
    for (int i = 0; i < num_tests; i++) {
        divider();
        tests[i]();
    }
    divider();
    DBG("end of tests");
    divider();
    free_vm(VM);
    return 0;
}
