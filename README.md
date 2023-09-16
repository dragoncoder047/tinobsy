# tinobsy

A tiny object system and garbage collector written in C++. No included scripting language, but I'm working on one and will share it once Tinobsy is complete.

## features

* Infinitely possible types.
* Lightweight. Each object is less than 100 bytes.
* Garbage-collected. Includes both a reference-counting collector (for speed) and a full mark-and-sweep (to prevent memory leaks).
* Includes thread management support. (OS-agnostic, thread spawn/kill support not implemented here.)
* Includes `setjmp`-based non-local control flow capabilities.
* Two-file C++ header/source combination.

## usage

To start, you will need to create a "virtual machine" object, and free it after you are done using it. This is done with the usual C++ new/delete:

```c++
auto my_vm = new tinobsy::vm();
// do stuff
delete vm;
```

Once you have done that you can declare type schemas and allocate objects:

```c++
tinobsy::object_schema my_type("MyType", ..., ..., ...); // see below for what to put in place of the ...'s
auto my_object = vm->allocate(&my_type);
```

The object is still "attached" internally to the VM you created, so you don't need to free it manually (and you can't anyway, because the destructor is private). It will be cleaned up automatically along with the VM when the VM is freed. The object schema is not -- you have to manage it yourself. (The easiest way is to statically allocate it; types don't change that much at all.)

### objects

`my_object->meta` points to another `tinobsy::object*`, which can be used for metadata (superclass, properties, etc).

The payload of the object is a large `union`, which is divided into two `car` and `cdr` cells. (The terminology is taken from Lisp.)

* The `car` cell can be a `object*`, `void*`, `char*`, `int32_t`, or `float`.
* Similarly, the `cdr` can be a `tobject*`, `void*`, `function_pointer`, `int32_t`, `uint32_t`, or `float`.
    * A `function_pointer` is a bare C function pointer (not a `std::function`), that takes two arguments `(tinobsy::thread* thread, void* context)` and returns a `void*`. The two pointers are `void*` because they are not actually used by Tinobsy and can be cast to anything you want.
* Additionally, `int64_t` and `double` fields span both `car` and `cdr` fields.

### types

Tinobsy places virtually no restrictions on what can be stored in an object. To be able to handle different types, the Tinobsy VM must be made aware of what to do with the payload of the object; this is done through `tinobsy::object_schema` objects. As stated above, the type schema's memory is not managed by the VM -- you have to manage it yourself.

They are simply a struct of four values: the type's name (`const char* const`), and four function pointers indicating what to do with the object during the three phases of the object's lifetime:

* The first function sets up the object when it is created. This can do anything, such as creating and assigning sub-objects, or allocating memory for a string. It is passed 3 `void*` pointers along with the object (they don't all need to be used, and all default to NULL).
* The second function is used for comparing objects for equality. It is passed two objects, and returns an `int` the same way `memcmp()` does.
* The third function takes care of marking the object. The garbage collector calls this when it wants to know what other objects this one points to, so it can determine what is in use and what is garbage. For all of the objects this object points to, call `subobject->mark()` on each.
* The fourth function does the reverse of the first: freeing any allocated memory, and decrementing the reference counts of pointed-to objects. It is called when the object is about to be deleted by the garbage collector.

### garbage collection

Tinobsy has a hybrid reference-counting and mark-sweep garbage collector. The reference-counting part runs concurrently when objects are allocated and assigned to each other, and the mark-sweep collector is invoked manually.

#### interning

If an object schema has the second (comparison) function defined, then objects of that type are automatically interned. If an object is allocated that would compare equal with an existing object of the same type, the new object is immediately deleted and the older object is returned instead. This is intended for primitive types such as strings and numbers.

#### reference counting

When a fresh object is returned from `tinobsy::vm::allocate()`, its internal reference count is set to 1, corresponding to the C++ pointer it is assigned to.

`tinobsy::object::incref()` and `tinobsy::object::decref()` take care of changing the reference count, and additionally, if an object's reference count ever reaches 0, the object is immediately deleted (and reference counts updated recursively).

To simplify the need to update reference counts on every assignment, `tinobsy.hpp` provides a macro `SET(x, y)`, which takes the place of `x = y` to additionally maintain proper reference counts, as well as `UNSET(x)` which is the same as `SET(x, NULL)` except the compiler won't complain about calling a method on NULL.

#### mark-and-sweep

Circular references are the Achilles' heel of reference-counting collectors, so Tinobsy also includes a mark-and-sweep garbage collector to be able to collect cyclic garbage.

The garbage collector is invoked by the function `tinobsy::vm::gc()`. It recursively calls `tinobsy::object::mark()` on each reachable object (using the `object_schema`'s mark-function), and then deletes all the objects that didn't get marked. It then returns the count of objects deleted.

To protect intermediate structures from being garbage-collected, `tinobsy::thread`s include a `gc_stack` member for this purpose. This can be made to point to the temporary objects, so they will end up being marked.

### error handling

Tinobsy threads have an included `jmp_buf*` member to allow for error handling. Note that this is a *pointer* to a `jump_buf`, not an actual `jmp_buf`, so you must allocate your own `jmp_buf`, assign the pointer, and pass the buffer to `setjmp()`.

The function `tinobsy::thread::raise(tinobsy::object* error, int signal = 1)` throws the error back to the `setjmp()`'ed point, using the `jmp_buf*` pointer stored in the `thread`. The `error` parameter is stored in the `thread->error` member to be inspected. The `signal` parameter is the code to be passed to `longjmp()`, which can be inspected (see below). **As with a bare `longjmp()`, the `jmp_buf` must have been initialized before use or bad things will happen!!**

The easiest way to handle the `setjmp()`, `jmp_buf`, and return code detection easily in Tinobsy is with the macro `TRYCATCH(thread, code_that_might_throw, code_to_handle_error)` macro. The last two parameters are **blocks of code** (pretty unusual for a macro). They are used like this:

```c++
auto t = get_some_thread();
TRYCATCH(t, {
    do_something_that_might_throw(t);
    // or, explicitly raise an error:
    t->raise(get_error_object(), 22);
}, {
    // This code runs if t->raise() was called
    cleanup(t->error);
    // You can see what signal code was passed to raise()
    // by inspecting the "sig" variable
    if (sig == 22) fprintf(stderr, "%s\n", "Catch-22!");
});
```
