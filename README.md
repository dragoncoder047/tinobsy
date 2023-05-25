# tinobsy

A tiny object system and garbage collector written in C. No included scripting language, but I'm working on one and will share it once Tinobsy is complete.

## features

* Infinitely possible types.
* Lightweight. Each object is less than 100 bytes.
* Garbage-collected. Includes both a reference-counting collector (for speed) and a full mark-and-sweep (to prevent memory leaks).
* Includes thread management support. (OS-agnostic, thread spawn/kill support not implemented here.)
* Includes `setjmp`-based non-local control flow capabilities.
* Single-file C header.

## usage

To start, you will need to create a "virtual machine" object, and free it after you are done using it. This is done with these functions:

```c
tvm* my_vm = tnewvm();
// do stuff
tfreevm(vm);
```

Once you have done that you can declare types and allocate objects:

```c
ttype my_type = {"MyType", NOTHING, NOTHING};
tobject* my_object = talloc(vm, &my_type);
```

The object is still "attached" internally to the VM you created, so you don't need to free it manually. It will be cleaned up automatically along with the VM when the VM is freed. The type struct is not -- you have to manage it yourself. (The easiest way is to statically allocate it; types don't change that much at all.)

### objects

`my_object->meta` points to another `tobject*`, which can be used for metadata (superclass, properties, etc).

The payload of the object is a large `union`, which is divided into two `car` and `cdr` cells. (The terminology is taken from Lisp.)

* The `car` cell can be a `tobject*`, `void*`, `char*`, `int32_t`, or `float`.
* Similarly, the `cdr` can be a `tobject*`, `void*`, `tfptr`, `uint32_t`, or `float`.
    * A `tfptr` is a function pointer, that takes 4 arguments `(tthread* thread, tobject* self, tobject* args, tobject* env)` and returns a `tobject*`.
* Additionally, `int64_t` and `double` fields span both `car` and `cdr` fields.

### types

Tinobsy places virtually no restrictions on what can be stored in an object. To be able to handle different types, the Tinobsy VM must be made aware of what is stored in each cell; this is done through `ttype`s. As stated above, the type struct's memory is not managed by the VM -- you have to manage it yourself.

They are simply a struct of three values: the type's name (`const char* const`), and two entries indicating how the `car` and `cdr` cells are used. The possible values for these are:

* `OBJECT`, which means the cell points to another `tobject*`, and the garbage collector should count this as a reference for the other object, and mark the other object recursively.
* `OWNED_PTR`, which means the cell points to a block of memory that the object "owns" (usually a `char*`, for e.g. a string or symbol), and it will be `free()`'d along with the object when it is garbage-collected.
* `NOTHING`, which is the catch-all for any other use of the cell. It is used for numbers stored inline in the `tobject`, for externally-managed `void*` pointers that aren't `tobject*`s, or simply if the cell is not used.

### garbage collection

Tinobsy has a hybrid reference-counting and mark-sweep garbage collector. The reference-counting part runs concurrently when objects are allocated and assigned to each other, and the mark-sweep collector is invoked manually.

#### reference counting

When a fresh object is returned from `talloc()`, its internal reference count is set to 1, corresponding to the C pointer it is assigned to.

`void tincref(tobj* x)` and `void tdecref(tobj* x)` take care of changing the reference count, and additionally, if an object's reference count ever reaches 0, the object is immediately freed (and reference counts updated recursively).

To simplify the need to update reference counts on every assignment, `tinobsy.h` provides a macro `SET(x, y)`, which takes the place of `x = y` to additionally maintain proper reference counts.

#### mark-and-sweep

Circular references are the Achilles' heel of reference-counting collectors, so Tinobsy also includes a mark-and-sweep garbage collector to be able to collect cyclic garbage.

The garbage collector is invoked by the function `size_t tmarksweep(tvm* vm)`. It marks everything reachable, sweeps everything that isn't, and returns the number of swept objects.

To protect intermediate structures from being garbage-collected, both the VM and `tthread`s include a `gc_stack` member for this purpose. This can be made to point to the temporary objects, so they will end up being marked.

### error handling

Tinobsy threads have an included `jmp_buf*` member to allow for error handling. Note that this is a *pointer* to a `jump_buf`, not an actual `jmp_buf`, so you must allocate your own `jmp_buf` and assign the pointer and pass the buffer to `setjmp()`.

The function `tthrow(tthread* thread, tobject* error, int signal)` throws the error back to the `setjmp()`'ed point, using the `jmp_buf*` pointer stored in the `thread`. The `error` parameter is stored in the `thread->error` member to be inspected. The `signal` parameter is the code to be passed to `longjmp()`, which can be inspected (see below). There is also a macro `THROW(thread, error)` that expands to a call of `tthrow()` with the last parameter set to 1 (the most common value).

The easiest way to handle the `setjmp()`, `jmp_buf`, and return code detection easily in Tinobsy is with the macro `TRYCATCH(thread, code_that_might_throw, code_to_handle_error)` macro. The last two parameters are **blocks of code** (pretty unusual for a macro). They are used like this:

```c
tthread* t = some_thread();
TRYCATCH(t, {
    do_something_that_might_throw(t);
    // or, explicitly raise an error:
    traise(t, get_error_object(), 22);
}, {
    // This code runs if traise() was called
    cleanup(t->error);
    // You can see what signal code was passed to traise()
    // by inspecting the "sig" variable
    if (sig == 22) fprintf(stderr, "Catch-22!");
});
```
