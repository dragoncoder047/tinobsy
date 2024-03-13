#include "tinobsy.hpp"

namespace tinobsy {

object_type::object_type(const char* const name, object* (*mark)(vm*, object*), void (*free)(object*), void (*print)(object* obj, object* stream))
: name(name),
  mark(mark),
  free(free),
  print(print) {}

object* vm::alloc(const object_type* type) {
    DBG("vm::alloc() a %s", type ? type->name : "wild pointer");
    if (this->freespace == 0) {
        DBG("out of freespace, trying to expand chunk list...");
        chunk* newchunk = new chunk(this->chunks);
        if (!newchunk) {
            perror("calloc()");
            abort();
        }
        this->n_chunks++;
        this->chunks = newchunk;
        // Zip free list
        for (ssize_t i = TINOBSY_CHUNK_SIZE - 1; i >= 0; i--) {
            object* here = &newchunk->d[i];
            cdr(here) = this->freelist;
            this->freelist = here;
            this->freespace++;
        }
        DBG("successfully added 1 chunk (%u objects)", TINOBSY_CHUNK_SIZE);
    }
    // Pull object from freelist
    object* newobj = this->freelist;
    this->freelist = newobj->as_obj;
    memset(newobj, 0, sizeof(object));
    newobj->type = (object_type*)type;
    this->freespace--;
    return newobj;
}

void vm::markobject(object* o) {
    MARK:
    if (o == NULL) {
        DBG("Marking NULL");
        return;
    }
    object_type* t = o->type;
    if (o->tst_flag(GC_MARKED)) {
        DBG("already marked");
        return;
    }
    o->set_flag(GC_MARKED);
    if (t == NULL) {
        DBG("Object exists, NULL type");
        o = car(o);
        goto MARK;
    }
    DBG("Marking a %s", t->name);
    if (!t->mark) {
        DBG("No MARK pointer, returning...");
        return;
    }
    o = t->mark(this, o);
    goto MARK;
}

size_t vm::gc() {
    size_t start = this->freespace;
    DBG("vm::gc() begin, %zu objects free / %zu objects total (%zu chunks) {", start, TINOBSY_CHUNK_SIZE * this->n_chunks, this->n_chunks);
    this->mark_globals();
    DBG("garbage collect sweeping objects");
    size_t ci = 0;
    this->freelist = NULL;
    this->freespace = 0;
    size_t freed_chunks = 0;
    for (chunk** c = &this->chunks; c && *c; c = &(*c)->next) {
        bool is_empty = true;
        object* freelist_here = this->freelist;
        for (size_t i = 0; i < TINOBSY_CHUNK_SIZE; i++) {
            object* o = &(*c)->d[i];
            if (!o->tst_flag(GC_MARKED)) this->release(o);
            else {
                o->clr_flag(GC_MARKED);
                is_empty = false;
            }
        }
        if (is_empty) {
            DBG("Whole chunk is empty");
            chunk* going = *c;
            *c = (*c)->next;
            delete going;
            this->freelist = freelist_here; // Reset freelist to top of chunk
            this->n_chunks--; // Decrement chunks
            this->freespace -= TINOBSY_CHUNK_SIZE; // Un-count new objects
            freed_chunks++;
        }
        DBG("}");
        ci++;
    }
    size_t freed = this->freespace + TINOBSY_CHUNK_SIZE * freed_chunks - start;
    DBG("vm::gc() after sweeping objects, %zu objects freed, %zu chunks freed", freed, freed_chunks);
    return freed;
}

void vm::release(object* o) {
    if (!o) return;
    if (o->type && o->type->free) o->type->free(o);
    memset(o, 0, sizeof(object));
    cdr(o) = this->freelist;
    this->freelist = o;
    this->freespace++;
}

vm::~vm() {
    DBG("vm::~vm() {");
    while (this->chunks) {
        chunk* c = this->chunks;
        for (size_t i = 0; i < TINOBSY_CHUNK_SIZE; i++) {
            this->release(&c->d[i]);
        }
        this->chunks = c->next;
        delete c;
    }
    DBG("}");
}

template <class field_type>
object* vm::get_existing_object(object_type* sch, field_type val, bool (*cmp)(field_type, field_type)) {
    chunk* c = this->chunks;
    while (c) {
        for (size_t i = 0; i < TINOBSY_CHUNK_SIZE; i++) {
            object* obj = &c->d[i];
            DBG("Interning, searching a %s", obj->type ? obj->type->name : "(uninitialized)");
            if (obj->type != sch) continue;
            if (cmp(val, (field_type)(obj->as_ptr))) return obj;
        }
        c = c->next;
    }
    return NULL;
}

template <class type>
bool op_eq(type a, type b) {
    return a == b;
}

object* markcons(vm* vm, object* self) {
    vm->markobject(car(self));
    return cdr(self);
}

}
