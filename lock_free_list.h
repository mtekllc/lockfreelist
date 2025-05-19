#include <stdlib.h>
#include <stdatomic.h>

/*
 * MIT License
 *
 * Copyright (c) 2024 Michael Miller
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @brief lock-free linked list macros based on atomic CAS principles
 *
 * this header defines a macro framework for creating and managing lock-free
 * singly-linked lists in c using atomic operations. the implementation
 * relies on compare-and-swap (cas) primitives from the c11 atomic model to
 * provide non-blocking, thread-safe operations for insertion, logical
 * removal, traversal, and memory reclamation.
 *
 * the design adheres to a proven strategy in concurrent data structure
 * theory, where fine-grained control over memory order semantics (acquire,
 * release) provides consistent visibility of shared state across threads
 * without requiring mutual exclusion. the macros are structured to make
 * safe progress in the presence of concurrent readers and writers, avoiding
 * common synchronization bottlenecks like global locks.
 *
 * traversal macros (like lfl_foreach) internally stash the 'next' pointer
 * before each iteration step, enabling safe node deletion during iteration
 * without risking access to freed memory. atomic refcounts and explicit
 * memory_order annotations enforce visibility and lifetime guarantees for
 * reclamation logic.
 *
 * this macro system is intended for use in high-throughput, multi-threaded
 * environments where performance, scalability, and safety are required in
 * place of simpler mutex-based queues or lists.
 */

/**
 * @brief define a new lock-free list struct for a given type name
 *
 * @param name base name of the list type
 */
#define lfl_def(name) \
        struct name##_linked_list { \
                _Atomic(struct name##_linked_list *) next; \
                _Atomic(struct name##_linked_list *) nextc; \
                _Atomic(struct name##_linked_list *) prev; \
                _Atomic(struct name##_linked_list *) prevc; \
                _Atomic(int) removed; \
                _Atomic(int) refcount;

/**
 * @brief close the list struct declaration
 */
#define lfl_end \
        };

/**
 * @brief declare head/tail pointers for a list instance
 *
 * @param name list type name
 * @param inst list instance name
 */
#define lfl_vars(name, inst) \
        _Atomic(struct name ## _linked_list *) inst## _head; \
        _Atomic(struct name ## _linked_list *) inst## _tail

/**
 * @brief declare head/tail pointers for a list instance as 'external'
 *
 * @param name
 * @param inst
 */
#define lfl_vars_extern(name, inst) \
        extern _Atomic(struct name ## _linked_list *) inst## _head; \
        extern _Atomic(struct name ## _linked_list *) inst## _tail

/**
 * @brief declare head/tail pointers for a list instance as 'static'
 *
 * @param name
 * @param inst
 */
#define lfl_vars_static(name, inst) \
        static _Atomic(struct name ## _linked_list *) inst## _head = NULL; \
        static _Atomic(struct name ## _linked_list *) inst## _tail = NULL

/* typing */
#define lfl_type(name) struct name ## _linked_list

/* allocating */
#define lfl_new(name) \
        ((lfl_type(name) *)calloc(1, sizeof(lfl_type(name))))

/* for discrete operations */

/* get the head */
#define lfl_get_head(inst) atomic_load_explicit(&inst##_head, memory_order_acquire)

/* get the tail */
#define lfl_get_tail(inst) atomic_load_explicit(&inst##_tail, memory_order_acquire)

/* get the next */
#define lfl_get_next(_cursor) atomic_load_explicit(&_cursor->next, memory_order_acquire)

/**
 * @brief initialize list instance pointers to NULL
 *
 * @param name list type name
 * @param inst list instance name
 */
#define lfl_init(name, inst) \
        do { \
                atomic_store(&(inst##_head), NULL); \
                atomic_store(&(inst##_tail), NULL); \
        } while (0)

/**
 * @brief iterate over list items, skipping logically removed nodes
 *
 *        note: stores next pointer for safe removal/deletion inside the loop
 *
 * @param name list type name
 * @param inst list instance name
 * @param item loop variable
 */
#define lfl_foreach(name, inst, item) \
        struct name##_linked_list *item = atomic_load_explicit(&(inst##_head), memory_order_acquire), *item##_next = NULL; \
        for (; item != NULL; item = item##_next) \
                if ((item##_next = atomic_load_explicit(&(item->next), memory_order_acquire)), \
                    !atomic_load_explicit(&(item->removed), memory_order_acquire))

/**
 * @brief lock-free tail append using CAS
 *
 * @param name list type name
 * @param inst list instance name
 * @param item loop variable / pointer output
 */
#define lfl_add_tail(name, inst, item) \
        struct name##_linked_list *item; \
        do { \
                item = calloc(1, sizeof(*item)); \
                atomic_store_explicit(&item->next, NULL, memory_order_relaxed); \
                atomic_store_explicit(&item->removed, 0, memory_order_relaxed); \
                struct name##_linked_list *expected_tail; \
                struct name##_linked_list *null_ptr = NULL; \
                do { \
                        expected_tail = atomic_load_explicit(&(inst##_tail), memory_order_acquire); \
                        if (expected_tail == NULL) { \
                                if (atomic_compare_exchange_weak_explicit( \
                                        &(inst##_head), &null_ptr, item, \
                                        memory_order_release, memory_order_relaxed)) { \
                                    atomic_store_explicit(&(inst##_tail), item, memory_order_release); \
                                    atomic_store_explicit(&item->prev, NULL, memory_order_relaxed); \
                                    break; \
                                } \
                        } else { \
                                struct name##_linked_list *next = NULL; \
                                if (atomic_compare_exchange_weak_explicit( \
                                        &expected_tail->next, &next, item, \
                                        memory_order_release, memory_order_relaxed)) { \
                                    atomic_store_explicit(&item->prev, expected_tail, memory_order_relaxed); \
                                    atomic_compare_exchange_weak_explicit( \
                                        &(inst##_tail), &expected_tail, item, \
                                        memory_order_release, memory_order_relaxed); \
                                    break; \
                                } \
                        } \
                } while (1); \
        } while (0)

/**
 * @brief lock-free head prepend using CAS
 *
 * @param name list type name
 * @param inst list instance name
 * @param item output variable to receive new node
 */
#define lfl_add_head(name, inst, item) \
        struct name##_linked_list *item; \
        do { \
                item = calloc(1, sizeof(*item)); \
                atomic_store_explicit(&item->removed, 0, memory_order_relaxed); \
                struct name##_linked_list *old_head; \
                do { \
                        old_head = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                        atomic_store_explicit(&item->next, old_head, memory_order_relaxed); \
                        atomic_store_explicit(&item->prev, NULL, memory_order_relaxed); \
                } while (!atomic_compare_exchange_weak_explicit( \
                        &(inst##_head), &old_head, item, \
                        memory_order_release, memory_order_relaxed)); \
                if (old_head) { \
                        atomic_store_explicit(&old_head->prev, item, memory_order_release); \
                } else { \
                        atomic_store_explicit(&(inst##_tail), item, memory_order_release); \
                } \
        } while (0)

/**
 * @brief insert a pre-allocated and initialized node at the tail of the list
 *
 * @param name list type name
 * @param inst list instance name
 * @param ptr  pointer to an initialized node to insert
 *
 * @note unlike lfl_add_tail, this macro does not allocate memory; caller
 *       must allocate and fully initialize the node before calling
 */
#define lfl_add_tail_ptr(name, inst, ptr) \
        do { \
                atomic_store_explicit(&ptr->next, NULL, memory_order_relaxed); \
                atomic_store_explicit(&ptr->removed, 0, memory_order_relaxed); \
                struct name##_linked_list *expected_tail; \
                struct name##_linked_list *null_ptr = NULL; \
                do { \
                        expected_tail = atomic_load_explicit(&(inst##_tail), memory_order_acquire); \
                        if (expected_tail == NULL) { \
                                if (atomic_compare_exchange_weak_explicit( \
                                        &(inst##_head), &null_ptr, ptr, \
                                        memory_order_release, memory_order_relaxed)) { \
                                    atomic_store_explicit(&(inst##_tail), ptr, memory_order_release); \
                                    atomic_store_explicit(&ptr->prev, NULL, memory_order_relaxed); \
                                    break; \
                                } \
                        } else { \
                                struct name##_linked_list *next = NULL; \
                                if (atomic_compare_exchange_weak_explicit( \
                                        &expected_tail->next, &next, ptr, \
                                        memory_order_release, memory_order_relaxed)) { \
                                    atomic_store_explicit(&ptr->prev, expected_tail, memory_order_relaxed); \
                                    atomic_compare_exchange_weak_explicit( \
                                        &(inst##_tail), &expected_tail, ptr, \
                                        memory_order_release, memory_order_relaxed); \
                                    break; \
                                } \
                        } \
                } while (1); \
        } while (0)

/**
 * @brief insert a pre-allocated and initialized node at the head of the list
 *
 * @param name list type name
 * @param inst list instance name
 * @param ptr  pointer to an initialized node to insert
 *
 * @note unlike lfl_add_head, this macro does not allocate memory; caller
 *       must allocate and fully initialize the node before calling
 */
#define lfl_add_head_ptr(name, inst, ptr) \
        do { \
                atomic_store_explicit(&ptr->removed, 0, memory_order_relaxed); \
                struct name##_linked_list *old_head; \
                do { \
                        old_head = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                        atomic_store_explicit(&ptr->next, old_head, memory_order_relaxed); \
                        atomic_store_explicit(&ptr->prev, NULL, memory_order_relaxed); \
                } while (!atomic_compare_exchange_weak_explicit( \
                        &(inst##_head), &old_head, ptr, \
                        memory_order_release, memory_order_relaxed)); \
                if (old_head) { \
                        atomic_store_explicit(&old_head->prev, ptr, memory_order_release); \
                } else { \
                        atomic_store_explicit(&(inst##_tail), ptr, memory_order_release); \
                } \
        } while (0)

/**
 * @brief logically removes a node from the list (non-blocking)
 *
 * @param name list type name
 * @param inst list instance name (unused, but kept for consistency)
 * @param target pointer to node to remove
 */
#define lfl_remove(name, inst, target) \
        do { \
                atomic_store_explicit(&(target->removed), 1, memory_order_release); \
        } while (0)

/**
 * @brief atomically remove node from list and free
 *
 * @param name  list type name
 * @param inst  list instance name
 * @param ptr   pointer to node to delete
 */

#define lfl_delete(name, inst, ptr) \
        do { \
                struct name##_linked_list *prev = atomic_load_explicit(&(ptr->prev), memory_order_acquire); \
                struct name##_linked_list *next = atomic_load_explicit(&(ptr->next), memory_order_acquire); \
                if (prev) { \
                        struct name##_linked_list *expected = ptr; \
                        atomic_compare_exchange_weak_explicit(&(prev->next), &expected, next, memory_order_acq_rel, memory_order_acquire); \
                } else { \
                        struct name##_linked_list *expected = ptr; \
                        atomic_compare_exchange_weak_explicit(&(inst##_head), &expected, next, memory_order_acq_rel, memory_order_acquire); \
                } \
                if (next) { \
                        struct name##_linked_list *expected = ptr; \
                        atomic_compare_exchange_weak_explicit(&(next->prev), &expected, prev, memory_order_acq_rel, memory_order_acquire); \
                } else { \
                        struct name##_linked_list *expected = ptr; \
                        atomic_compare_exchange_weak_explicit(&(inst##_tail), &expected, prev, memory_order_acq_rel, memory_order_acquire); \
                } \
                free(ptr); \
        } while (0)

/**
 * @brief lock-free search through the list using a condition
 *
 * @param name list type name
 * @param inst list instance name
 * @param item loop variable that will point to the match
 * @param cond expression to evaluate per item (e.g. item->id == 5)
 */
#define lfl_find(name, inst, item, field, value) \
        struct name##_linked_list *item = NULL; \
        do { \
                struct name##_linked_list *name##_cursor = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                while (name##_cursor) { \
                        if (!atomic_load_explicit(&name##_cursor->removed, memory_order_acquire)) { \
                                if ((name##_cursor->field) == (value)) { \
                                        item = name##_cursor; \
                                        break; \
                                } \
                        } \
                        name##_cursor = atomic_load_explicit(&name##_cursor->next, memory_order_acquire); \
                } \
        } while (0)

/**
 * @brief atomically sweep logically removed nodes with refcount == 0,
 *        and optionally call a cleanup function before freeing
 *
 * @param name     list type name
 * @param inst     instance name
 * @param ref      field name of atomic refcount in the node
 * @param cleanup  pointer to cleanup function or NULL
 */

#define lfl_sweep(name, inst, ref, ...) \
        do { \
                struct name##_linked_list *prev = NULL; \
                struct name##_linked_list *curr = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                void (*cleanup_fn)(struct name##_linked_list *) = NULL; \
                if (sizeof((void *[]){__VA_ARGS__}) / sizeof(void *) > 0) \
                        cleanup_fn = __VA_ARGS__; \
                while (curr) { \
                        struct name##_linked_list *next = atomic_load_explicit(&(curr->next), memory_order_acquire); \
                        int removed = atomic_load_explicit(&(curr->removed), memory_order_acquire); \
                        int refs = atomic_load_explicit(&(curr->ref), memory_order_acquire); \
                        if (removed && refs == 0) { \
                                if (prev) { \
                                        struct name##_linked_list *expected = curr; \
                                        if (atomic_compare_exchange_weak_explicit(&(prev->next), &expected, next, memory_order_acq_rel, memory_order_acquire)) { \
                                                if (cleanup_fn) cleanup_fn(curr); \
                                                free(curr); \
                                                curr = next; \
                                                continue; \
                                        } else { \
                                                prev = NULL; \
                                                curr = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                                                continue; \
                                        } \
                                } else { \
                                        struct name##_linked_list *expected = curr; \
                                        if (atomic_compare_exchange_weak_explicit(&(inst##_head), &expected, next, memory_order_acq_rel, memory_order_acquire)) { \
                                                if (cleanup_fn) cleanup_fn(curr); \
                                                free(curr); \
                                                curr = next; \
                                                continue; \
                                        } else { \
                                                prev = NULL; \
                                                curr = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                                                continue; \
                                        } \
                                } \
                        } \
                        prev = curr; \
                        curr = next; \
                } \
        } while (0)


/**
 * @brief free all nodes in the list (for shutdown)
 *
 * @param name list type name
 * @param inst list instance name
 */
#define lfl_clear(name, inst) \
        do { \
                struct name##_linked_list *cursor = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                while (cursor) { \
                        struct name##_linked_list *next = atomic_load_explicit(&cursor->next, memory_order_relaxed); \
                        free(cursor); \
                        cursor = next; \
                } \
                atomic_store(&(inst##_head), NULL); \
                atomic_store(&(inst##_tail), NULL); \
        } while (0)

/**
 * @brief count the number of non-removed nodes in the list
 *
 * @param name list type name
 * @param inst list instance name
 * @param out  integer variable to store the node count
 *
 * @note only nodes that are not logically removed are counted
 */
#define lfl_count(name, inst, out) \
        do { \
                out = 0; \
                lfl_foreach(name, inst, _item) { \
                        out++; \
                } \
        } while (0)

/**
 * @brief count nodes that are logically removed but still held by references
 *
 * @param name list type name
 * @param inst list instance name
 * @param ref  field name of the atomic refcount in the node
 * @param out  integer variable to store the count of pending cleanup nodes
 *
 * @return none; result is written to the variable passed via `out`
 */
#define lfl_count_pending_cleanup(name, inst, ref, out) \
        do { \
                int _pending = 0; \
                struct name##_linked_list *cursor = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                while (cursor) { \
                        int removed = atomic_load_explicit(&cursor->removed, memory_order_acquire); \
                        int refs = atomic_load_explicit(&cursor->ref, memory_order_acquire); \
                        if (removed && refs > 0) _pending++; \
                        cursor = atomic_load_explicit(&cursor->next, memory_order_acquire); \
                } \
                out = _pending; \
        } while (0)


/**
 * @brief atomically remove and return the first node in the list
 *
 * @param name  list type name
 * @param inst  list instance name
 * @param item  local variable to receive the head node pointer
 *
 * @return the first node is unlinked but not freed; caller may inspect it
 */

#define lfl_pop_head(name, inst, item) \
        do { \
                struct name##_linked_list *cursor = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                while (cursor) { \
                        struct name##_linked_list *next = atomic_load_explicit(&(cursor->next), memory_order_acquire); \
                        if (atomic_compare_exchange_weak_explicit(&(inst##_head), &cursor, next, memory_order_acq_rel, memory_order_acquire)) { \
                                item = cursor; \
                                if (!next) atomic_store_explicit(&(inst##_tail), (struct name##_linked_list *)NULL, memory_order_release); \
                                atomic_store_explicit(&(item->next), (struct name##_linked_list *)NULL, memory_order_release); \
                                atomic_store_explicit(&(item->prev), (struct name##_linked_list *)NULL, memory_order_release); \
                                break; \
                        } \
                } \
        } while (0)

/**
 * @brief atomically remove and return the last node in the list
 *
 * @param name  list type name
 * @param inst  list instance name
 * @param item  local variable to receive the tail node pointer
 *
 * @return the last node is unlinked but not freed; caller may inspect it
 */

#define lfl_pop_tail(name, inst, item) \
        do { \
                struct name##_linked_list *cursor_tail = atomic_load_explicit(&(inst##_tail), memory_order_acquire); \
                while (cursor_tail) { \
                        struct name##_linked_list *prev = NULL; \
                        struct name##_linked_list *curr = atomic_load_explicit(&(inst##_head), memory_order_acquire); \
                        while (curr && curr != cursor_tail) { \
                                prev = curr; \
                                curr = atomic_load_explicit(&(curr->next), memory_order_acquire); \
                        } \
                        if (!curr) break; /* item disappeared */ \
                        if (prev) { \
                                if (atomic_compare_exchange_weak_explicit(&(inst##_tail), &cursor_tail, prev, memory_order_acq_rel, memory_order_acquire)) { \
                                        atomic_store_explicit(&(prev->next), (struct name##_linked_list *)NULL, memory_order_release); \
                                        item = curr; \
                                        atomic_store_explicit(&(item->next), (struct name##_linked_list *)NULL, memory_order_release); \
                                        atomic_store_explicit(&(item->prev), (struct name##_linked_list *)NULL, memory_order_release); \
                                        break; \
                                } \
                        } else { \
                                if (atomic_compare_exchange_weak_explicit(&(inst##_head), &cursor_tail, (struct name##_linked_list *)NULL, memory_order_acq_rel, memory_order_acquire)) { \
                                        atomic_store_explicit(&(inst##_tail), (struct name##_linked_list *)NULL, memory_order_release); \
                                        item = curr; \
                                        atomic_store_explicit(&(item->next), (struct name##_linked_list *)NULL, memory_order_release); \
                                        atomic_store_explicit(&(item->prev), (struct name##_linked_list *)NULL, memory_order_release); \
                                        break; \
                                } \
                        } \
                        cursor_tail = atomic_load_explicit(&(inst##_tail), memory_order_acquire); \
                } \
        } while (0)

