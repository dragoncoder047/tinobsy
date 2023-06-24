#include "tinobsy.hpp"

namespace tinobsy {

object* allocate(vm* vm, const object_schema* type) {
    ASSERT(type != NULL, "tried to initialize a null type");
    DBG("allocating a %s", type->name);
    object* newobject;
    for (newobject = vm->first; newobject != NULL; newobject = newobject->next) {
        if (newobject->type == NULL) {
            DBG("reusing garbage");
            object* oldnext = newobject->next;
            memset(newobject, 0, sizeof(struct s_obj));
            newobject->next = oldnext;
            goto gotgarbage;
        }
    }
    DBG("need new memory");
    newobject = (object*)calloc(1, sizeof(struct s_obj));
    newobject->next = vm->first;
    vm->first = newobject;
    vm->num_objects++;
    gotgarbage:
    newobject->refcount = 1;
    newobject->type = type;
    return newobject;
}

void finalize(object* x) {
    if (x == NULL) return;
    object_schema* xt = x->type;
    if (xt == NULL) return; // Already finalized
    DBG("finalizing a %s {", xt->name);
    decref(x->meta);
    if (xt->car == OWNED_PTR) free(x->car_ptr);
    else if (xt->car == OBJECT) decref(x->car);
    if (xt->cdr == OWNED_PTR) free(x->cdr_ptr);
    else if (xt->cdr == OBJECT) decref(x->cdr);
    DBG("}");
    x->type = NULL;
    x->car = x->cdr = NULL;
    clr_flag(x, GC_MARKED);
}

inline void decref(object* x) {
    if (x != NULL && x->refcount > 0 && x->type != NULL) {
        x->refcount--;
        DBG("decref'ed a %s, now have %zu refs", x->type->name, x->refcount);
        if (x->refcount <= 0) finalize(x);
    }
}

inline void incref(object* x) {
    if (x != NULL) {
        ASSERT(x->type != NULL, "null type object incref'ed");
        x->refcount++;
        DBG("incref'ed a %s, now have %zu refs", x->type->name, x->refcount);
    }
}

inline bool tst_flag(object* x, flag f) {
    return x != NULL && (x->flags & (1<<f)) != 0;
}

inline void set_flag(object* x, flag f) {
    if (x != NULL) x->flags |= 1 << f;
}

inline void clr_flag(object* x, flag f) {
    if (x != NULL) x->flags &= ~(1 << f);
}

void mark_object(object* x) {
    mark:
    if (x == NULL) {
        DBG("marking a NULL");
        return;
    }
    if (tst_flag(x, GC_MARKED)) return;
    set_flag(x, GC_MARKED);
    object_schema* xt = x->type;
    if (xt != NULL) {
        DBG("marking a %s", xt->name);
        mark_object(x->meta);
        if (xt->car == OBJECT) mark_object(x->car);
        if (xt->cdr == OBJECT) {
            x = x->cdr;
            goto mark;
            // tail recursion
        }
    }
}

size_t gc(vm* vm) {
    size_t start = vm->num_objects;
    DBG("garbage collect start, %zu objects {", start);
    for (thread* t = vm->threads; t != NULL; t = t->next_thread) {
        mark_object(t->gc_stack);
        mark_object(t->env);
        mark_object(t->error);
    }
    DBG("marking globals");
    mark_object(vm->nil);
    object** x = &vm->first;
    DBG("garbage collect sweeping");
    size_t dangling = 0;
    while (*x != NULL) {
        if (!tst_flag(*x, GC_MARKED)) {
            object* u = *x;
            // Kill all pointers to this object
            if (u->refcount > 0) {
                size_t realrefs = 0;
                object* p = vm->first;
                while (p != NULL) {
                    if (p->type != NULL) {
                        if (p->type->car == OBJECT && p->car == u) {
                            ASSERT(!tst_flag(p, GC_MARKED), "Unmarked object pointed to by marked object");
                            p->car = NULL;
                            realrefs++;
                        }
                        if (p->type->cdr == OBJECT && p->cdr == u) {
                            ASSERT(!tst_flag(p, GC_MARKED), "Unmarked object pointed to by marked object");
                            p->cdr = NULL;
                            realrefs++;
                        }
                    }
                    p = p->next;
                }
                if (realrefs != u->refcount) {
                    dangling += u->refcount - realrefs;
                }
                else {
                    DBG("Cyclic garbage detected");
                }
            }
            *x = u->next;
            finalize(u);
            free(u);
            vm->num_objects--;
        } else {
            clr_flag(*x, GC_MARKED);
            x = &(*x)->next;
        }
    }
    #ifndef TINOBSY_IGNORE_DANGLING_POINTERS
    fprintf(stderr, "\nWARNING!! %zu dangling pointers detected!!\n", dangling);
    #endif
    size_t freed = start - vm->num_objects;
    DBG("garbage collect end, %zu objects, %zu freed, %zu dangling pointers }", vm->num_objects, freed, dangling);
    return freed;
}

vm* new_vm() {
    vm* new_vm = (vm*)calloc(1, sizeof(struct s_vm));
    new_vm->nil = allocate(new_vm, &nil_schema);
    return new_vm;
}

void free_vm(vm* vm) {
    DBG("free vm");
    #ifdef TINOBSY_DEBUG
    size_t th = 0;
    #endif
    while (vm->threads != NULL) {
        free_thread(vm->threads);
        #ifdef TINOBSY_DEBUG
        th++;
        #endif
    }
    DBG("freed %zu threads", th);
    decref(vm->nil);
    vm->nil = NULL;
    gc(vm);
    ASSERT(vm->num_objects == 0, "gc bugged");
    free(vm);
}

unsigned int next_vpid(vm* vm) {
    unsigned int p = 0;
    while (true) {
        for (thread* t = vm->threads; t != NULL; t = t->next_thread) {
            if (t->vpid == p) goto next;
        }
        return p;
        next:
        p++;
    }
}

thread* push_thread(vm* vm) {
    thread* new_ = (thread*)calloc(1, sizeof(struct s_thr));
    new_->vpid = next_vpid(vm);
    DBG("Allocating a new thread, next vpid is %u", new_->vpid);
    new_->next_thread = vm->threads;
    vm->threads = new_;
    new_->owner = vm;
    return new_;
}

void free_thread(thread* th) {
    vm* vm = th->owner;
    DBG("Freeing thread %u {", th->vpid);
    thread** x = &vm->threads;
    while (*x != NULL) {
        if (*x == th) {
            *x = th->next_thread;
            break;
        } else {
            x = &(*x)->next_thread;
        }
    }
    decref(th->gc_stack);
    decref(th->env);
    decref(th->error);
    free(th);
    DBG("}");
}

void raise(thread* th, object* error, int sig) {
    DBG("Throwing an error on thread %u", th->vpid);
    ASSERT(th->trycatch != NULL, "No try-catch set");
    SET(th->error, error);
    SET(th->gc_stack, NULL);
    decref(error);
    longjmp(*(th->trycatch), sig);
}

}