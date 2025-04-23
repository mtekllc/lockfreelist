# lockfree-list-macros

A macro-based C framework for building and managing lock-free singly-linked lists using C11 atomics and Compare-And-Swap (CAS) operations.

This implementation provides non-blocking, thread-safe primitives for insertion, logical removal, traversal, reference counting, memory reclamation, and safe iteration in concurrent environments. It is designed for performance-critical systems where fine-grained control is preferred over global locking.

## Features

- **CAS-based head/tail insertions** using `lfl_add_head()` and `lfl_add_tail()`
- **Logical removal** via `lfl_remove()` with deferred cleanup based on atomic reference counts
- **Immediate removal** via `lfl_delete()` with pointer-safe traversal
- **Safe iteration** with `lfl_foreach()` that stashes `next` to support in-loop deletion
- **Reference-based deferred sweeping** with `lfl_sweep()`
- **Queue state analysis** using `lfl_count_pending_cleanup()`
- **List initialization and shutdown** via `lfl_init()` and `lfl_clear()`

## Macro Descriptions

### `lfl_def(name)` / `lfl_end`
Defines a new lock-free list node type. Insert your user fields between the two macros.

### `lfl_vars(name, inst)`
Declares `head` and `tail` pointers for a named list instance.

### `lfl_init(name, inst)`
Initializes the list instance by setting head and tail to `NULL`.

### `lfl_add_head(name, inst, item)` / `lfl_add_tail(name, inst, item)`
Appends a node to the head or tail of the list. The macro allocates and initializes the node.

### `lfl_remove(name, inst, target)`
Marks a node as logically removed by setting its `removed` flag. Intended for use with `lfl_sweep()`.

### `lfl_delete(name, inst, ptr)`
Immediately unlinks and frees a node from the list. Safe for in-loop deletion using `lfl_foreach()`.

### `lfl_foreach(name, inst, item)`
Iterates over all non-removed nodes. Internally caches the `next` pointer before each iteration, allowing safe `lfl_delete()` calls from within the loop.

### `lfl_find(name, inst, item, field, value)`
Searches for a node with a field matching the given value. Skips removed nodes.

### `lfl_sweep(name, inst, ref, [cleanup])`
Traverses the list and permanently frees nodes that are both logically removed and have `refcount == 0`. Optionally calls a cleanup function before freeing.

### `lfl_clear(name, inst)`
Frees all nodes in the list unconditionally, typically used for shutdown.

### `lfl_count_pending_cleanup(name, inst, ref, out)`
Counts nodes that are logically removed but still have a non-zero reference count.

### `lfl_pop_head(name, inst, item)`
Atomically removes the head of the list and assigns the result to `item`.
The node is logically removed but **not freed**. Caller is responsible for
processing and eventually calling `lfl_delete()` or allowing it to be swept.

### `lfl_pop_tail(name, inst, item)`
Traverses and removes the last node in the list, assigning it to `item`.
The node is unlinked and logically removed but **not freed**.

Both macros return `NULL` (via `item`) if the list is empty.

Example usage:

```c
lfl_pop_head(mytype, myqueue, node);
if (node) {
    // use node, delete when done
    lfl_delete(mytype, myqueue, node);
}
```

---


## Example Use Case

A test program is provided that:
- Spawns a producer thread injecting new work at high throughput
- Spawns a monitor thread reporting live queue size
- Spawns a cleaner thread that deletes or sweeps old work

Press `Ctrl+C` to stop the producer and drain the queue safely.

## License

MIT License — see [LICENSE](./LICENSE) for full terms.

---

© 2024 Michael Miller

