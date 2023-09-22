#include "tinobsy.hpp"

namespace tinobsy {

object_schema::object_schema(const char* const name, init_function init, cmp_function cmp, mark_function mark, finalize_function finalize)
: name(name),
  init(init),
  cmp(cmp),
  mark(mark),
  finalize(finalize) {}

object_schema::~object_schema() {}

object::object(const object_schema* schema, object* next, void* arg0, void* arg1, void* arg2)
: meta(NULL),
  car(NULL),
  cdr(NULL),
  next(next),
  schema((const object_schema*)schema),
  flags(0) {
    if (schema->init) schema->init(this, arg0, arg1, arg2);
}

object::~object() {
    if (this->schema == NULL) return;
    DBG("object::~object() for a %s begin {", this->schema->name);
    if (this == NULL) return;
    const object_schema* xt = this->schema;
    ASSERT(xt != NULL, "should not be finalized yet");
    if (this->schema->finalize) this->schema->finalize(this);
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
    DBG("}");
}

object* vm::allocate(const object_schema* schema, void* arg0, void* arg1, void* arg2) {
    ASSERT(schema != NULL, "tried to initialize with a null schema");
    DBG("vm::allocate() a %s", schema->name);
    object* newobj = new object(schema, this->first, arg0, arg1, arg2);
    // Intern it?
    if (schema->cmp) {
        DBG("Trying to intern a %s", schema->name);
        for (object* oldobj = this->first; oldobj != NULL; oldobj = oldobj->next) {
            if (oldobj->schema == schema && schema->cmp(oldobj, newobj) == 0) {
                DBG("Interned! %s", schema->name);
                delete newobj;
                return oldobj;
            }
        }
        DBG("New %s not interned", schema->name);
    }
    this->first = newobj;
    this->num_objects++;
    return newobj;
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
    if (x->tst_flag(GC_MARKED)) {
        DBG("Already marked (Reference cycle?)");
        return;
    }
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
    while (*x != NULL) {
        if (!(*x)->tst_flag(GC_MARKED)) {
            object* u = *x;
            *x = u->next;
            delete u;
            this->num_objects--;
        } else {
            (*x)->clr_flag(GC_MARKED);
            x = &(*x)->next;
        }
    }
    size_t freed = start - this->num_objects;
    DBG("vm::gc() end, %zu objects, %zu freed }", this->num_objects, freed);
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
    this->error = error;
    this->gc_stack = NULL;
    longjmp(*(this->trycatch), sig);
}

void schema_functions::mark_cons(object* self) {
    DBG("{");
    if (self->car) self->car->mark();
    if (self->cdr) self->cdr->mark();
    DBG("}");
}

void schema_functions::init_str(object* self, void* arg0, void* _, void* __) {
    DBG();
    const char* str = (const char*)arg0;
    self->car_str = strdup(str);
}

void schema_functions::finalize_str(object* self) {
    DBG();
    free((void*)self->car_str);
}

int schema_functions::obj_memcmp(object* a, object* b) {
    DBG("comparing objects at %p and %p", a, b);
    int x = memcmp(&a->as_integer, &b->as_integer, sizeof(int64_t));
    if (x) return x;
    return memcmp(&a->cdr, &b->cdr, sizeof(object*));
}

int schema_functions::cmp_str(object* a, object* b) {
    DBG("comparing %s to %s", a->car_str, b->car_str);
    return strcmp(a->car_str, b->car_str);
}

}