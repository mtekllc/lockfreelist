# lockfree-list-macros

[![Build Status](https://github.com/mtekllc/lockfreelist/actions/workflows/ci.yml/badge.svg)](https://github.com/mtekllc/lockfreelist/actions)

A macro-based C framework for building and managing lock-free doubly-linked lists using C11 atomics and Compare-And-Swap (CAS) operations.

This implementation provides non-blocking, thread-safe primitives for insertion, logical removal, traversal, memory reclamation, counting, and node removal — designed for high-throughput concurrent systems where fine-grained memory control is preferred over coarse-grained locks.

---

## Features

- **CAS-based head/tail insertions** using `lfl_add_head()` and `lfl_add_tail()`
- **Dual-stage insertion** with `lfl_add_head_ptr()` and `lfl_add_tail_ptr()` (caller-allocated nodes)
- **Logical removal** via `lfl_remove()` without immediate memory reclamation
- **Immediate deletion** via `lfl_delete()` when safe
- **Safe traversal** with `lfl_foreach()` supporting in-loop deletion
- **Node searching** with `lfl_find()`
- **Deferred sweeping** using `lfl_sweep()` based on reference counts
- **Queue state analysis** with `lfl_count()` and `lfl_count_pending_cleanup()`
- **List initialization and shutdown** with `lfl_init()` and `lfl_clear()`
- **Atomic node popping** from head or tail with `lfl_pop_head()` and `lfl_pop_tail()`

---

## Macro Descriptions

### `lfl_def(name)` / `lfl_end`
Defines a new lock-free list node type. User fields go between `lfl_def` and `lfl_end`.

Example:
```c
lfl_def(mytype)
    int id;
lfl_end
```

---

### `lfl_vars(name, inst)`
Declares atomic head and tail pointers for a list instance.

Example:
```c
lfl_vars(mytype, myqueue);
```

---

### `lfl_init(name, inst)`
Initializes a list's head and tail to `NULL`.

---

### `lfl_type(name)`
Expands to `struct name##_linked_list`, the type of list nodes.

---

### `lfl_get_head(inst)` / `lfl_get_tail(inst)`
Atomically loads the head or tail of a list.

---

### `lfl_get_next(cursor)`
Atomically loads the next pointer of a node.

---

### `lfl_foreach(name, inst, item)`
Safely iterates over **non-removed** nodes in the list.

- Internally caches the `next` pointer before visiting the current node
- Allows `lfl_remove()` and `lfl_delete()` to be called safely inside the loop

Example:
```c
lfl_foreach(mytype, myqueue, node) {
    printf("Node id = %d\n", node->id);
}
```

---

### `lfl_add_head(name, inst, item)` / `lfl_add_tail(name, inst, item)`
Single-stage add macros:
- **Allocates** a new node internally
- **Immediately publishes** to the list

---

### Dual-Stage Insertion: `_ptr` variants

- `lfl_add_head_ptr(name, inst, ptr)`
- `lfl_add_tail_ptr(name, inst, ptr)`

These variants **do not allocate memory internally**.
The caller **allocates and fully initializes** the node before publishing it to the list.

This avoids readers ever seeing partially populated structures.

Example:
```c
test_t *node = lfl_new(test);
node->id = 123;
lfl_add_tail_ptr(test, myqueue, node);
```

Helper macro `lfl_new(name)` simplifies allocation.

---

### `lfl_remove(name, inst, target)`
Marks a node as logically removed (but keeps it in the list until swept or deleted).

---

### `lfl_delete(name, inst, ptr)`
Unlinks and frees a node immediately.
Safe for calling inside a `lfl_foreach()` loop.

---

### `lfl_find(name, inst, item, field, value)`
Finds the first node matching a specified field value, skipping logically removed nodes.

---

### `lfl_sweep(name, inst, ref, [cleanup])`
Traverses the list and frees logically removed nodes whose `refcount` is zero.

Optional: calls a cleanup function before freeing nodes.

---

### `lfl_clear(name, inst)`
Unconditionally frees all nodes and resets the list to empty.

---

### `lfl_count(name, inst, out)`
Counts the number of nodes that are **not logically removed**.

Example:
```c
int live_nodes = 0;
lfl_count(mytype, myqueue, live_nodes);
printf("Live nodes: %d\n", live_nodes);
```

---

### `lfl_count_pending_cleanup(name, inst, ref, out)`
Counts nodes that are logically removed but still held by outstanding references (refcount > 0).

---

### `lfl_pop_head(name, inst, item)` / `lfl_pop_tail(name, inst, item)`
Atomically removes a node from the head or tail of the list.

- Node is logically removed but **not freed**
- Caller should eventually free it or allow sweep

Example:
```c
lfl_pop_head(mytype, myqueue, node);
if (node) {
    // process node
    lfl_delete(mytype, myqueue, node);
}
```

### Moving and sorting nodes

`lfl_move_before(name, inst, A, B)` moves `B` so it appears directly before `A`.
`lfl_move_after(name, inst, A, B)` moves `B` directly after `A`.

`lfl_sort_asc(name, inst, field)` sorts the list in ascending order based on `field`. `lfl_sort_desc(name, inst, field)` performs the opposite sort.

```c
lfl_def(test, {
        int id;
});
lfl_end(test);

lfl_vars(test, q);
lfl_init(test, q);

lfl_add_tail(test, q, n1); n1->id = 3;
lfl_add_tail(test, q, n2); n2->id = 1;
lfl_add_tail(test, q, n3); n3->id = 2;

lfl_move_before(test, q, n1, n3); /* list is n3, n1, n2 */
lfl_move_after(test, q, n1, n2);  /* list is n3, n2, n1 */

lfl_sort_asc(test, q, id);  /* list becomes 1, 2, 3 */
lfl_sort_desc(test, q, id); /* list becomes 3, 2, 1 */

```

---

## Example Use Case

A sample test program can:
- Spawn a producer thread adding new work
- Spawn a monitor thread scanning and counting live nodes
- Spawn a cleaner thread removing or sweeping nodes

Press `Ctrl+C` to stop the producer and cleanly drain the list.

---

## License

MIT License — see [LICENSE](./LICENSE) for full terms.

---

© 2025 Michael Miller
