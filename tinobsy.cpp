#include "tinobsy.hpp"

namespace tinobsy {

object_schema::object_schema(const char* const name, init_function init, mark_function mark, finalize_function finalize)
: name(name),
  init(init),
  mark(mark),
  finalize(finalize) {}

object_schema::~object_schema() {}

object::object(const object_schema* schema, object* next, void* arg0, void* arg1, void* arg2)
: meta(NULL),
  car(NULL),
  cdr(NULL) {
    this->init(schema, next, arg0, arg1, arg2);
}

void object::init(const object_schema* schema, object* next, void* arg0, void* arg1, void* arg2) {
    this->next = next;
    this->schema = (object_schema*)schema;
    this->flags = 0;
    this->refcount = 1;
    if (schema->init) schema->init(this, arg0, arg1, arg2);
}

object::~object() {
    if (this->schema == NULL) return;
    DBG("object::~object() for a %s begin {", this->schema->name);
    this->finalize();
    DBG("}");
}

thread::thread(vm* vm, unsigned int vpid, void* handle)
: owner(vm),
  vpid(vpid),
  thread_handle(handle) {}

thread::~thread() {
    vm* vm = this->owner;
    DBG("thread::~thread() no. %u {", this->vpid);
    thread** x = &vm->threads;
    while (*x != NULL) {
        if (*x == this) {
            *x = this->next_thread;
            break;
        } else {
            x = &(*x)->next_thread;
        }
    }
    this->env->decref();
    this->gc_stack->decref();
    this->error->decref();
    DBG("}");
}

object* vm::allocate(const object_schema* schema, void* arg0, void* arg1, void* arg2) {
    ASSERT(schema != NULL, "tried to initialize a null schema");
    DBG("vm::allocate() a %s", schema->name);
    object* newobject = NULL;
    for (newobject = this->first; newobject != NULL; newobject = newobject->next) {
        if (newobject->schema == NULL) {
            DBG("reusing garbage");
            object* oldnext = newobject->next;
            memset(newobject, 0, sizeof(object));
            newobject->init(schema, oldnext, arg0, arg1, arg2);
            return newobject;
        }
    }
    DBG("need new memory");
    newobject = new object(schema, this->first, arg0, arg1, arg2);
    newobject->next = this->first;
    this->first = newobject;
    this->num_objects++;
    return newobject;
}

void object::finalize() {
    if (this == NULL) return;
    const object_schema* xt = this->schema;
    if (xt == NULL) return; // Already finalized
    DBG("object::finalize() for a %s {", xt->name);
    this->meta->decref();
    if (this->schema->finalize) this->schema->finalize(this);
    DBG("}");
    this->schema = NULL;
    this->car = this->cdr = NULL;
    this->clr_flag(GC_MARKED);
}

inline void object::decref() {
    if (this != NULL && this->refcount > 0 && this->schema != NULL) {
        this->refcount--;
        DBG("object::decref() on a %s, now have %zu refs", this->schema->name, this->refcount);
        if (this->refcount <= 0) this->finalize();
    }
}

inline void object::incref() {
    if (this != NULL) {
        ASSERT(this->schema != NULL, "<already finalized>::incref() happened");
        this->refcount++;
        DBG("object::incref() on a %s, now have %zu refs", this->schema->name, this->refcount);
    }
}

inline bool object::tst_flag(flag f) {
    return this != NULL && (this->flags & (1<<f)) != 0;
}

inline void object::set_flag(flag f) {
    if (this != NULL) this->flags |= 1 << f;
}

inline void object::clr_flag(flag f) {
    if (this != NULL) this->flags &= ~(1 << f);
}

void object::mark() {
    object* x = this;
    mark:
    if (x == NULL) {
        DBG("NULL::mark()");
        return;
    }
    if (x->tst_flag(GC_MARKED)) return;
    x->set_flag(GC_MARKED);
    const object_schema* xt = x->schema;
    if (xt != NULL) {
        DBG("object::mark() a %s", xt->name);
        if (xt->mark) xt->mark(x);
        x = x->meta;
        goto mark;
        // tail recursion
    }
}

void vm::mark_globals() {
    DBG("marking globals");
    this->nil->mark();
}

size_t vm::gc() {
    size_t start = this->num_objects;
    DBG("vm::gc() begin, %zu objects {", start);
    for (thread* t = this->threads; t != NULL; t = t->next_thread) {
        t->gc_stack->mark();
        t->env->mark();
        t->error->mark();
    }
    this->mark_globals();
    object** x = &this->first;
    DBG("garbage collect sweeping");
    size_t dangling = 0;
    while (*x != NULL) {
        if (!(*x)->tst_flag(GC_MARKED)) {
            object* u = *x;
            // Kill all pointers to this object
            if (u->refcount > 0) {
                size_t realrefs = 0;
                object* p = this->first;
                while (p != NULL) {
                    if (p->schema != NULL) {
                        if (p->car == u) {
                            ASSERT(!p->tst_flag(GC_MARKED), "Unmarked object pointed to by marked object");
                            p->car = NULL;
                            realrefs++;
                        }
                        if (p->cdr == u) {
                            ASSERT(!p->tst_flag(GC_MARKED), "Unmarked object pointed to by marked object");
                            p->cdr = NULL;
                            realrefs++;
                        }
                    }
                    p = p->next;
                }
                if (realrefs != u->refcount) {
                    ssize_t diff = u->refcount - realrefs;
                    if (diff > 0) dangling += diff;
                    else DBG("%zu rogue number pointers", -diff);
                }
                else {
                    DBG("Cyclic garbage detected");
                }
            }
            *x = u->next;
            delete u;
            this->num_objects--;
        } else {
            (*x)->clr_flag(GC_MARKED);
            x = &(*x)->next;
        }
    }
    #ifndef TINOBSY_IGNORE_DANGLING_POINTERS
    fprintf(stderr, "\nWARNING!! %zu dangling pointers detected!!\n", dangling);
    #endif
    size_t freed = start - this->num_objects;
    DBG("vm::gc() end, %zu objects, %zu freed, %zu dangling pointers }", this->num_objects, freed, dangling);
    return freed;
}

vm::vm()
: first(NULL),
  num_objects(0),
  threads(NULL) {
    this->nil = this->allocate(&nil_schema);
}

vm::~vm() {
    DBG("vm::~vm()");
    #ifdef TINOBSY_DEBUG
    size_t th = 0;
    #endif
    while (this->threads != NULL) {
        delete this->threads;
        #ifdef TINOBSY_DEBUG
        th++;
        #endif
    }
    DBG("freed %zu threads", th);
    this->nil->decref();
    this->nil = NULL;
    this->gc();
    ASSERT(this->num_objects == 0, "gc bugged");
}

unsigned int vm::next_vpid() {
    unsigned int p = 0;
    while (true) {
        for (thread* t = this->threads; t != NULL; t = t->next_thread) {
            if (t->vpid == p) goto next;
        }
        return p;
        next:
        p++;
    }
}

thread* vm::push_thread() {
    thread* new_ = new thread(this, this->next_vpid(), NULL);
    DBG("vm::push_thread(), next vpid is %u", new_->vpid);
    new_->next_thread = this->threads;
    this->threads = new_;
    return new_;
}

[[noreturn]] void thread::raise(object* error, int sig) {
    DBG("thread::raise() on thread %u", this->vpid);
    ASSERT(this->trycatch != NULL, "No try-catch set");
    SET(this->error, error);
    UNSET(this->gc_stack);
    error->decref();
    longjmp(*(this->trycatch), sig);
}

void schema_functions::mark_cons(object* x) {
    DBG("{");
    if (x->car) x->car->mark();
    if (x->cdr) x->cdr->mark();
    DBG("}");
}

void schema_functions::finalize_cons(object* x) {
    DBG("{");
    if (x->car) x->car->decref();
    if (x->cdr) x->cdr->decref();
    DBG("}");
}

void schema_functions::init_str(object* x, void* arg0, void* _, void* __) {
    DBG();
    char* str = (char*)arg0;
    x->car_str = strdup(str);
}

void schema_functions::finalize_str(object* x) {
    DBG();
    free((void*)x->car_str);
}

}