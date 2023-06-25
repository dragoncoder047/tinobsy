# tinobsy

A tiny object system and garbage collector written in C. No included scripting language, but I'm working on one and will share it once Tinobsy is complete.

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
tinobsy::object_schema my_type("MyType", NOTHING, NOTHING);
auto my_object = vm->allocate(&my_type);
```

The object is still "attached" internally to the VM you created, so you don't need to free it manually (and you can't anyway, because the destructor is private). It will be cleaned up automatically along with the VM when the VM is freed. The object schema is not -- you have to manage it yourself. (The easiest way is to statically allocate it; types don't change that much at all.)

### objects

`my_object->meta` points to another `tinobst::object*`, which can be used for metadata (superclass, properties, etc).

The payload of the object is a large `union`, which is divided into two `car` and `cdr` cells. (The terminology is taken from Lisp.)

* The `car` cell can be a `object*`, `void*`, `char*`, `int32_t`, or `float`.
* Similarly, the `cdr` can be a `tobject*`, `void*`, `function_pointer`, `uint32_t`, or `float`.
    * A `function_pointer` is a bare C function pointer (not a `std::function`), that takes 4 arguments `(tinobsy::thread* thread, tinobsy::object* self, tinobsy::object* args, tinobsy::object* env)` and returns a `tinobsy::object*`.
* Additionally, `int64_t` and `double` fields span both `car` and `cdr` fields.

### types

Tinobsy places virtually no restrictions on what can be stored in an object. To be able to handle different types, the Tinobsy VM must be made aware of what is stored in each cell; this is done through `tinobsy::object_schema` objects. As stated above, the type schema's memory is not managed by the VM -- you have to manage it yourself.

They are simply a struct of three values: the type's name (`const char* const`), and two entries indicating how the `car` and `cdr` cells are used. The possible values for these are:

* `OBJECT`, which means the cell points to another `tinobsy::object*`, and the garbage collector should count this as a reference for the other object, and mark the other object recursively.
* `OWNED_PTR`, which means the cell points to a block of memory that the object "owns" (usually a `char*`, for e.g. a string or symbol), and it will be `free()`'d (*not* `delete`d) along with the object when it is garbage-collected.
* `NOTHING`, which is the catch-all for any other use of the cell. It is used for numbers stored inline in the `tinobsy::object`, for externally-managed `void*` pointers that aren't `tinobsy::object*`s, or simply if the cell is not used.

### garbage collection

Tinobsy has a hybrid reference-counting and mark-sweep garbage collector. The reference-counting part runs concurrently when objects are allocated and assigned to each other, and the mark-sweep collector is invoked manually.

#### reference counting

When a fresh object is returned from `tinobsy::vm::allocate()`, its internal reference count is set to 1, corresponding to the C++ pointer it is assigned to.

`tinobsy::object::incref()` and `tinobsy::object::decref()` take care of changing the reference count, and additionally, if an object's reference count ever reaches 0, the object is immediately freed (and reference counts updated recursively).

To simplify the need to update reference counts on every assignment, `tinobsy.hpp` provides a macro `SET(x, y)`, which takes the place of `x = y` to additionally maintain proper reference counts, as well as `UNSET(x)` which is the same as `SET(x, NULL)` except the compiler won't complain about calling a method on NULL.

#### mark-and-sweep

Circular references are the Achilles' heel of reference-counting collectors, so Tinobsy also includes a mark-and-sweep garbage collector to be able to collect cyclic garbage.

The garbage collector is invoked by the function `tinobsy::vm::gc()`. It marks everything reachable, sweeps everything that isn't, and returns the number of swept objects.

To protect intermediate structures from being garbage-collected, `tinobsy::thread`s include a `gc_stack` member for this purpose. This can be made to point to the temporary objects, so they will end up being marked.

### error handling

Tinobsy threads have an included `jmp_buf*` member to allow for error handling. Note that this is a *pointer* to a `jump_buf`, not an actual `jmp_buf`, so you must allocate your own `jmp_buf`, assign the pointer, and pass the buffer to `setjmp()`.

The function `tinobsy::thread::raise(tinobsy::object* error, int signal)` throws the error back to the `setjmp()`'ed point, using the `jmp_buf*` pointer stored in the `thread`. The `error` parameter is stored in the `thread->error` member to be inspected. The `signal` parameter is the code to be passed to `longjmp()`, which can be inspected (see below). There is also a macro `THROW(thread, error)` that expands to a call of `raise()` with the last parameter set to 1 (the most common value). **As with a bare `longjmp()`, the `jmp_buf` must have been initialized before use or bad things will happen!!**

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
    if (sig == 22) fprintf(stderr, "Catch-22!");
});
```
