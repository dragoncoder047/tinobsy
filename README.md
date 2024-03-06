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

* The first function takes care of marking the object. The garbage collector calls this when it wants to know what other objects this one points to, so it can determine what is in use and what is garbage. It is passed a pointer to the VM and a pointer to the object to be marked. For all of the `tinobsy::object`s this one points to, call `vm->object(obj)` on each. The last object can be returned instead of marked, to optimize tail-recursion.
* The second function does the reverse of the first: freeing any allocated memory. It is called when the object is about to be unloaded by the garbage collector.
* The third function is not really used by Tinobsy, but will probably be useful when implementing a scripting language around Tinobsy: it is passed two objects, and is intended to be used to "print" the first object onto the second object "stream".

Any of them can be NULL if nothing needs to be done there.

### garbage collection

Tinobsy has a simple mark-and-sweep collector, based partly off of Bob Nystrom's [mark-sweep](https://github.com/munificent/mark-sweep) collector and partly off of David Johnson-Davies' [uLisp](http://www.ulisp.com/show?1BD3) garbage collector.

<!-- #### interning

If an object type has the second (comparison) function defined, then objects of that type are automatically interned. If an object is allocated that would compare equal with an existing object of the same type, the new object is immediately deleted and the older object is returned instead. This is intended for primitive types such as strings and numbers. -->

#### mark-and-sweep

The garbage collector is invoked by the function `tinobsy::vm::gc()`. It recursively calls `tinobsy::vm::markobject()` on each reachable object (using the `object_type`'s mark-function), and then saves all the objects that didn't get marked in a free-object list for reuse. Internally, Tinobsy allocates objects in chunks of 128 (which can be overridden with the compile constant `TINOBSY_CHUNK_SIZE`). Whenever the garbage collector notices that a chunk is completely empty, it deletes the chunk. The main garbage collection function returns the count of objects deleted.

#### globals

WARNING: A bare `tinobsy::vm` has no globals and nothing is marked by default when you call `gc()`, so *everything* will be collected unless you manually mark objects first.

The `tinobsy::vm` class has a virtual function `mark_globals()` that the garbage collector always calls before it collects. You can implement this method in a subclass to be able to mark the additional global members you added to the subclass.
