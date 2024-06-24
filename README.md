# tinobsy

A tiny object system and garbage collector written in C++. No included scripting language, but I'm working on one and will share it once Tinobsy is complete.

## features

* Infinitely possible types.
* Lightweight. Each object is less than 100 bytes.
* Garbage-collected. Includes a full mark-and-sweep collector.
* Two-file C++ header/source combination.

## usage

To start, you will need to create a "virtual machine" object, and free it after you are done using it. This is done with the usual C++ new/delete:

```c++
auto my_vm = new tinobsy::vm();
// do stuff
delete vm;
```

Once you have done that you can declare type types and allocate objects:

```c++
tinobsy::object_type my_type("MyType", ..., ..., ...) // see below for what to put in place of the ...'s
auto my_object = vm->allocate(&my_type);
```

The object is still "attached" internally to the VM you created, so you don't need to free it manually (and you can't anyway, because the destructor is private). It will be cleaned up automatically along with the VM when the VM is freed. The object type is not -- you have to manage it yourself. (The easiest way is to statically allocate it as a global variable; types don't change that much at all.)

### objects

`my_object->meta` points to another `tinobsy::object*`, which can be used for metadata (superclass, properties, etc). This field can also be called as `my_object->car` or `car(my_object)`.

The payload of the object is a large `union`. See `tinobsy.hpp` for what the fields can be.

### types

Tinobsy places virtually no restrictions on what can be stored in an object. To be able to handle different types, the Tinobsy VM must be made aware of what to do with the payload of the object; this is done through `tinobsy::object_type` objects. As stated above, the type type's memory is not managed by the VM -- you have to manage it yourself.

They are simply a struct of four values: the type's name (`const char* const`), and four function pointers indicating what to do with the object during the three phases of the object's lifetime:

* The first function takes care of marking the object. The garbage collector calls this when it wants to know what other objects this one points to, so it can determine what is in use and what is garbage. It is passed a pointer to the VM and a pointer to the object to be marked. For all of the `tinobsy::object`s this one points to, call `vm->markobject(obj)` on each. The last object can be returned instead of marked, to optimize tail-recursion.
* The second function implements the second half of garbage collection: freeing any allocated memory when the object really is garbage. It is called just before the garbage collector de-initializes the object (the object won't be `delete`d right away for optimization reasons, but it's a very bad idea to keep pointers to a "dead" object).
* The third function is not really used by Tinobsy, but will probably be useful when implementing a scripting language around Tinobsy: it is passed two objects, and is intended to be used to "print" the first object onto the second object "stream".

Any of them can be NULL if nothing needs to be done there.

### garbage collection

Tinobsy has a simple mark-and-sweep collector, based partly off of Bob Nystrom's [mark-sweep](https://github.com/munificent/mark-sweep) collector and partly off of David Johnson-Davies' [uLisp](http://www.ulisp.com/show?1BD3) garbage collector.

> [!NOTE]
> Tinobsy doesn't make any guesses as to when is a good time to automatically collect -- you have to do it manually. Do this by calling `vm->gc()` at a point where you know there aren't any temporary objects that are still being referred to by C++ variables hanging around that would be corrupted if the garbage collector "pulled the rug" on those objects.

#### interning

Allocating an object with `alloc()` doesn't actually initialize the fields of the object -- you have to write your own functions to do that yourself. If the object is an "atomic" type such as a string or number that doesn't point to any other Tinobsy objects (i.e. the mark function is/can be NULL), you can intern the object. The helper function `vm->get_existing_object<type>(schema, value, cmp_func)` returns the existing object, that has the same schema as the one passed in, and also the same value (the `as_ptr` member is cast to the templated-in type and compared to the value using the function), or NULL if the object doesn't exist yet. There is a convenience function `op_eq<type>(type, type)` that can be used as the comparison function for types that store their values inline. For example:

```cpp
object_schema MyBits("mybits", NULL, free_mybits, print_mybits);
object* make_mybits(vm* vm, struct mybits value) {
    object* x = vm->get_existing_object<struct mybits>(&Example, value, op_eq<struct mybits>);
    if (!x) {
        x = vm->alloc(&MyBits);
        x->as_ptr = calloc(1, sizeof(struct mybits));
        memcpy((void*)&value, (void*)x->as_ptr, sizeof(struct mybits));
    }
    return x;
}
```

There is also a preprocessor macro `INTERN(vm, typ, sch, val)` and `INTERN_PTR(vm, typ, sch, val, cmp)` that are just sugar for checking this function and then returning early if the object exists -- the above function could be simplified to:

```cpp
object_schema MyBits("mybits", NULL, free_mybits, print_mybits);
object* make_mybits(vm* vm, struct mybits value) {
    INTERN(vm, struct mybits, &MyBits, value);
    /* if we get here it means a new object must be created */
    object* x = vm->alloc(&MyBits);
    x->as_ptr = calloc(1, sizeof(struct mybits));
    memcpy((void*)&value, (void*)x->as_ptr, sizeof(struct mybits));
    return x;
}
```

#### mark-and-sweep

The garbage collector is invoked by the function `tinobsy::vm::gc()`. It recursively calls `tinobsy::vm::markobject()` on each reachable object (using the `object_type`'s mark-function), and then saves all the objects that didn't get marked in a free-object list for reuse. Internally, Tinobsy allocates objects in chunks of 128 (which can be overridden with the compile constant `TINOBSY_CHUNK_SIZE`). Whenever the garbage collector notices that a chunk is completely empty, it deletes the chunk. The main garbage collection function returns the count of objects deleted.

#### globals

> [!CAUTION]
> A bare `tinobsy::vm` has no globals and nothing is marked by default when you call `gc()`, so *everything* will be collected unless you manually mark objects first.

The `tinobsy::vm` class has a virtual function `mark_globals()` that the garbage collector always calls before it collects. You can implement this method in a subclass to be able to mark the additional global members you added to the subclass.
