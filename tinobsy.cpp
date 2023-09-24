#include "tinobsy.hpp"

namespace tinobsy {

object_schema::object_schema(const char* const name, init_function init, cmp_function cmp, mark_function mark, finalize_function finalize)
: name(name),
  init(init),
  cmp(cmp),
  mark(mark),
  finalize(finalize) {}

object_schema::~object_schema() {}

object::object(const object_schema* schema, object* next, va_list args)
: next(next),
  schema((const object_schema*)schema) {
    if (schema->init) schema->init(this, args);
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

object* vm::allocate(const object_schema* schema, ...) {
    va_list args;
    va_start(args, schema);
    ASSERT(schema != NULL, "tried to initialize with a null schema");
    DBG("vm::allocate() a %s", schema->name);
    object* newobj = new object(schema, this->first, args);
    // Intern it?
    if (schema->cmp) {
        DBG("Trying to intern a %s", schema->name);
        for (object* oldobj = this->first; oldobj != NULL; oldobj = oldobj->next) {
            if (oldobj->schema == schema && schema->cmp(oldobj, newobj) == 0) {
                DBG("Interned! %s", schema->name);
                delete newobj;
                va_end(args);
                return oldobj;
            }
        }
        DBG("New %s not interned", schema->name);
    }
    this->first = newobj;
    this->num_objects++;
    va_end(args);
    return newobj;
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

size_t vm::gc() {
    size_t start = this->num_objects;
    DBG("vm::gc() begin, %zu objects {", start);
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

vm::~vm() {
    DBG("vm::~vm() {");
    while (this->first != NULL) {
        object* die = this->first;
        this->first = this->first->next;
        delete die;
    }
    DBG("}");
}

void schema_functions::init_cons(object* self, va_list args) {
    DBG();
    self->cells = new cell[2];
    self->cells[0].as_obj = va_arg(args, object*);
    self->cells[1].as_obj = va_arg(args, object*);
}

void schema_functions::mark_cons(object* self) {
    DBG("{");
    self->cells[0].as_obj->mark();
    self->cells[1].as_obj->mark();
    DBG("}");
}

void schema_functions::finalize_cons(object* self) {
    DBG();
    delete[] self->cells;
}

void schema_functions::init_str(object* self, va_list args) {
    DBG();
    self->as_chars = strdup(va_arg(args, char*));
}

void schema_functions::finalize_str(object* self) {
    DBG();
    free((void*)self->as_chars);
}

int schema_functions::obj_memcmp(object* a, object* b) {
    DBG("comparing objects at %p and %p: %lu, %lu", a, b, a->as_big_int, b->as_big_int);
    return a->as_big_int - b->as_big_int;
}

int schema_functions::cmp_str(object* a, object* b) {
    DBG("comparing %s to %s", a->as_chars, b->as_chars);
    return strcmp(a->as_chars, b->as_chars);
}

}