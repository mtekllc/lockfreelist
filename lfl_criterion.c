#include <criterion/criterion.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

#include "lock_free_list.h"

lfl_def(test)
        int id;
lfl_end

typedef lfl_type(test) test_t;

Test(lfl_lockfree, add_and_find)
{
        lfl_vars(test, active);
        lfl_init(test, active);

        lfl_add_tail(test, active, n1);
        n1->id = 100;
        lfl_add_tail(test, active, n2);
        n2->id = 200;
        lfl_add_tail(test, active, n3);
        n3->id = 300;

        lfl_find(test, active, found, id, 200);

        cr_assert_not_null(found, "expected to find node with id 200");
        cr_expect_eq(found->id, 200, "expected id 200, got %d", found->id);
}

Test(lfl_lockfree, logical_removal)
{
        lfl_vars(test, active);
        lfl_init(test, active);

        lfl_add_tail(test, active, a);
        a->id = 1;
        lfl_add_tail(test, active, b);
        b->id = 2;
        lfl_add_tail(test, active, c);
        c->id = 3;

        lfl_remove(test, active, b);

        int count = 0;

        lfl_foreach(test, active, item){
                count++;
                cr_assert_neq(item->id, 2, "removed node with id 2 should not appear");
        }

        cr_expect_eq(count, 2, "expected 2 nodes after removal, got %d", count);
}

Test(lfl_lockfree, cleanup)
{
        lfl_vars(test, active);
        lfl_init(test, active);

        for (int i = 0; i < 5; i++) {
                lfl_add_tail(test, active, n);
                n->id = i;
        }

        lfl_clear(test, active);
        struct test_linked_list *ptr = atomic_load(&(active_head));
        cr_expect_null(ptr, "expected list head to be NULL after clear");
}

atomic_uint_fast64_t cleanup_count = 0;

static void cleanup_node(test_t *node)
{
        cr_log_info("cleaning node id = %d\n", node->id);
        atomic_fetch_add(&cleanup_count,1);
}

Test(lfl_lockfree, sweep_with_cleanup)
{
        lfl_vars(test, sweepable);
        lfl_init(test, sweepable);

        lfl_add_tail(test, sweepable, x);
        x->id = 1;
        lfl_add_tail(test, sweepable, y);
        y->id = 2;
        lfl_add_tail(test, sweepable, z);
        z->id = 3;

        lfl_remove(test, sweepable, y);
        atomic_store(&y->refcount, 0);

        lfl_sweep(test, sweepable, refcount, &cleanup_node);

        cr_expect_eq(cleanup_count, 1, "expected 1 cleaned node, got %d", cleanup_count);
        lfl_clear(test, sweepable);
}

Test(lfl_lockfree, count_pending_nodes)
{
        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;

        lfl_remove(test, stucklist, n2);
        atomic_store(&n2->refcount, 1); // simulate active usage

        int pending = -1;
        lfl_count_pending_cleanup(test, stucklist, refcount, pending);
        cr_expect_eq(pending, 1, "expected 1 pending cleanup node, got %d", pending);

        atomic_store(&n2->refcount, 0);
        // do not touch n2 after this point — it may be freed
        lfl_sweep(test, stucklist, refcount, NULL);
        lfl_clear(test, stucklist);
}

atomic_uint_fast64_t deep_clean = 0;

static void noop_cleanup(test_t *element)
{
        cr_log_info("deep clean node id = %d\n", element->id);
        atomic_store(&deep_clean, 1);
}

Test(lfl_lockfree, count_pending_nodes_deep_clean)
{
        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;

        lfl_remove(test, stucklist, n2);
        atomic_store(&n2->refcount, 1); // simulate active usage

        int pending = -1;
        lfl_count_pending_cleanup(test, stucklist, refcount, pending);
        cr_expect_eq(pending, 1, "expected 1 pending cleanup node, got %d", pending);

        atomic_store(&n2->refcount, 0);
        // do not touch n2 after this point — it may be freed
        lfl_sweep(test, stucklist, refcount, noop_cleanup);
        cr_assert(deep_clean, "noop_cleanup was not called as expected");
        lfl_clear(test, stucklist);
}

Test(lfl_lockfree, delete_node_immediate_free)
{
        test_t *cursor = NULL;
        int count2 = 0;
        int count1 = 0;

        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;

        lfl_delete(test, stucklist, n2);

        lfl_find(test, stucklist, found, id, 2);
        cr_assert_null(found, "expected not to find node with id 2");

        lfl_foreach(test, stucklist, lle) {
                count1++;
        }

        cr_expect_eq(count1, 2, "expected 2 nodes after delete, got %d", count1);

        /* now manually iterate over the list and count stuff */
        cursor = lfl_get_head(stucklist);
        while (cursor) {
                cr_expect_neq(cursor->id, 2, "deleted node with id=2 still in list");
                cursor = lfl_get_next(cursor);
                count2++;
        }
        cr_expect_eq(count2, 2, "expected 2 nodes after delete, got %d", count1);

        lfl_clear(test, stucklist);
}

Test(lfl_lockfree, delete_head_node)
{
        test_t *cursor = NULL;
        int count = 0;
        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;

        lfl_delete(test, stucklist, n1);

        cursor = lfl_get_head(stucklist);
        cr_expect_neq(cursor->id, 1, "deleted head node with id=1 still in list");

        while (cursor) {
                count++;
                cursor = lfl_get_next(cursor);
        }

        cr_expect_eq(count, 2, "expected 2 nodes after head delete, got %d", count);

        lfl_clear(test, stucklist);
}

Test(lfl_lockfree, delete_tail_node)
{
        test_t *cursor = NULL;
        int count = 0;
        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;

        lfl_delete(test, stucklist, n3);

        cursor = lfl_get_head(stucklist);
        while (cursor) {
                cr_expect_neq(cursor->id, 3, "deleted tail node with id=3 still in list");
                cursor = lfl_get_next(cursor);
                count++;
        }

        cr_expect_eq(count, 2, "expected 2 nodes after tail delete, got %d", count);

        lfl_clear(test, stucklist);
}

Test(lfl_lockfree, direct_and_macro_iteration_agree)
{
        test_t *cursor = NULL;
        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;

        // count via get_next loop
        int count_next = 0;
        cursor = lfl_get_head(stucklist);
        while (cursor) {
                count_next++;
                cursor = lfl_get_next(cursor);
        }

        // count via foreach macro
        int count_foreach = 0;
        lfl_foreach(test, stucklist, item) {
                count_foreach++;
        }

        cr_expect_eq(count_next, 3, "get_next loop count mismatch: expected 3, got %d", count_next);
        cr_expect_eq(count_foreach, 3, "foreach macro count mismatch: expected 3, got %d", count_foreach);

        lfl_clear(test, stucklist);
}

Test(lfl_lockfree, skip_removed_nodes_in_foreach)
{
        test_t *cursor = NULL;
        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;
        lfl_add_tail(test, stucklist, n4);
        n4->id = 4;

        lfl_remove(test, stucklist, n2);
        lfl_remove(test, stucklist, n4);

        // count all nodes manually including removed
        int total_count = 0;
        cursor = lfl_get_head(stucklist);
        while (cursor) {
                total_count++;
                cursor = lfl_get_next(cursor);
        }
        cr_expect_eq(total_count, 4, "expected 4 total nodes in list");

        // count only live (non-removed) nodes via lfl_foreach
        int live_count = 0;
        lfl_foreach(test, stucklist, item) {
                live_count++;
        }
        cr_expect_eq(live_count, 2, "expected 2 non-removed nodes, got %d", live_count);

        lfl_clear(test, stucklist);
}


Test(lfl_lockfree, foreach_remove_and_verify_skipped)
{
        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;

        // first pass: remove node with id 2
        lfl_foreach(test, stucklist, item) {
                if (item->id == 2) {
                        lfl_remove(test, stucklist, item);
                }
        }

        // second pass: ensure node 2 is skipped
        int count = 0;
        lfl_foreach(test, stucklist, item2) {
                cr_expect_neq(item2->id, 2, "node with id=2 was not skipped after removal");
                count++;
        }
        cr_expect_eq(count, 2, "expected 2 non-removed nodes after one removal, got %d", count);

        lfl_clear(test, stucklist);
}

Test(lfl_lockfree, foreach_delete_and_verify_removed)
{
        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;

        // first pass: delete node with id 2
        lfl_foreach(test, stucklist, item) {
                if (item->id == 2) {
                        lfl_delete(test, stucklist, item);
                        break; // avoid touching freed memory
                }
        }

        // second pass: ensure node 2 is not in list at all
        int count = 0;
        lfl_foreach(test, stucklist, item2) {
                cr_expect_neq(item2->id, 2, "node with id=2 was not deleted properly");
                count++;
        }
        cr_expect_eq(count, 2, "expected 2 remaining nodes after delete, got %d", count);

        lfl_clear(test, stucklist);
}


Test(lfl_lockfree, foreach_mixed_remove_delete_then_sweep)
{
        lfl_vars(test, stucklist);
        lfl_init(test, stucklist);

        lfl_add_tail(test, stucklist, n1);
        n1->id = 1;
        lfl_add_tail(test, stucklist, n2);
        n2->id = 2;
        lfl_add_tail(test, stucklist, n3);
        n3->id = 3;
        lfl_add_tail(test, stucklist, n4);
        n4->id = 4;

        // first pass: remove node 2 and delete node 3
        lfl_foreach(test, stucklist, item) {
                if (item->id == 2) {
                        lfl_remove(test, stucklist, item);
                }
                if (item->id == 3) {
                        lfl_delete(test, stucklist, item);
                        break; // item is freed
                }
        }

        // sweep logically removed (node 2)
        atomic_store(&n2->refcount, 0); // mark safe to free
        lfl_sweep(test, stucklist, refcount, NULL);

        // second pass: only node 1 and 4 should remain
        int count = 0;
        lfl_foreach(test, stucklist, item2) {
                cr_expect_neq(item2->id, 2, "removed node with id=2 still present after sweep");
                cr_expect_neq(item2->id, 3, "deleted node with id=3 still present");
                count++;
        }
        cr_expect_eq(count, 2, "expected 2 remaining nodes after mixed remove/delete/sweep, got %d", count);

        lfl_clear(test, stucklist);
}

Test(lfl_pop, pop_head_returns_first_node)
{
        lfl_vars(test, queue);
        lfl_init(test, queue);

        lfl_add_tail(test, queue, n1);
        n1->id = 100;
        lfl_add_tail(test, queue, n2);
        n2->id = 200;

        lfl_pop_head(test, queue, head);
        cr_assert_not_null(head, "Expected a node to be popped from head");
        cr_expect_eq(head->id, 100, "Expected head node to have id 100, got %d", head->id);

        lfl_delete(test, queue, head);
        lfl_clear(test, queue);
}

Test(lfl_pop, pop_tail_returns_last_node)
{
        lfl_vars(test, queue);
        lfl_init(test, queue);

        lfl_add_tail(test, queue, n1);
        n1->id = 1;
        lfl_add_tail(test, queue, n2);
        n2->id = 2;
        lfl_add_tail(test, queue, n3);
        n3->id = 3;

        lfl_pop_tail(test, queue, tail);
        cr_assert_not_null(tail, "Expected a node to be popped from tail");
        cr_expect_eq(tail->id, 3, "Expected tail node to have id 3, got %d", tail->id);

        lfl_delete(test, queue, tail);
        lfl_clear(test, queue);
}

Test(lfl_pop, pop_head_from_empty_returns_null)
{
        lfl_vars(test, queue);
        lfl_init(test, queue);

        lfl_pop_head(test, queue, head);
        cr_expect_null(head, "Expected NULL when popping head from an empty list");

        lfl_clear(test, queue);
}

Test(lfl_pop, pop_tail_from_empty_returns_null)
{
        lfl_vars(test, queue);
        lfl_init(test, queue);

        lfl_pop_tail(test, queue, tail);
        cr_expect_null(tail, "Expected NULL when popping tail from an empty list");

        lfl_clear(test, queue);
}

struct container {
        int metadata;
        lfl_vars(test, innerlist);
};

Test(lfl_vars, vars_can_be_embedded_in_struct)
{
        struct container c;
        lfl_init(test, c.innerlist);

        lfl_add_tail(test, c.innerlist, n1);
        n1->id = 42;

        lfl_foreach(test, c.innerlist, item) {
                cr_expect_eq(item->id, 42, "Expected embedded list node with id 42, got %d", item->id);
        }

        lfl_clear(test, c.innerlist);
}

Test(lfl_integrity, add_delete_add_sequence)
{
        lfl_vars(test, queue);
        lfl_init(test, queue);

        // add a node
        lfl_add_tail(test, queue, n1);
        n1->id = 1;

        // delete it
        lfl_delete(test, queue, n1);

        // add another node after delete
        lfl_add_tail(test, queue, n2);
        n2->id = 2;

        // check if new node is correctly added
        int count = 0;
        lfl_foreach(test, queue, item) {
                cr_expect_eq(item->id, 2, "Expected new node with id 2 after delete, got %d", item->id);
                count++;
        }

        cr_expect_eq(count, 1, "Expected exactly one node after add-delete-add sequence, got %d", count);

        lfl_clear(test, queue);
}

Test(lfl_dualstage, init_fill_insert_with_ptr_macro)
{
        lfl_vars(test, queue);
        lfl_init(test, queue);

        test_t *node = lfl_new(test);
        node->id = 42;

        lfl_add_tail_ptr(test, queue, node);

        int found = 0;
        lfl_foreach(test, queue, item) {
                cr_expect_eq(item->id, 42, "expected node with id 42, got %d", item->id);
                found++;
        }
        cr_expect_eq(found, 1, "expected one node in the list, found %d", found);

        lfl_delete(test, queue, node);
        lfl_clear(test, queue);
}

Test(lfl_dualstage, init_fill_insert_head_with_ptr_macro)
{
        lfl_vars(test, queue);
        lfl_init(test, queue);

        test_t *node = lfl_new(test);
        node->id = 24;

        lfl_add_head_ptr(test, queue, node);

        int found = 0;
        lfl_foreach(test, queue, item) {
                cr_expect_eq(item->id, 24, "expected node with id 24, got %d", item->id);
                found++;
        }
        cr_expect_eq(found, 1, "expected one node in the list, found %d", found);

        lfl_delete(test, queue, node);
        lfl_clear(test, queue);
}

Test(lfl_dualstage, compound_insert_head_and_tail_with_ptr)
{
        lfl_vars(test, queue);
        lfl_init(test, queue);

        test_t *first = lfl_new(test);
        test_t *last = lfl_new(test);
        first->id = 1;
        last->id = 99;

        lfl_add_head_ptr(test, queue, first);
        lfl_add_tail_ptr(test, queue, last);

        int seen_head = 0, seen_tail = 0, total = 0;
        lfl_foreach(test, queue, item) {
                if (item->id == 1) seen_head++;
                if (item->id == 99) seen_tail++;
                total++;
        }

        cr_expect_eq(seen_head, 1, "head node not found");
        cr_expect_eq(seen_tail, 1, "tail node not found");
        cr_expect_eq(total, 2, "expected two nodes, found %d", total);

        lfl_delete(test, queue, first);
        lfl_delete(test, queue, last);
        lfl_clear(test, queue);
}

Test(lfl_count_macro, counts_non_removed_nodes_correctly)
{
        lfl_vars(test, queue);
        lfl_init(test, queue);

        lfl_add_tail(test, queue, n1);
        n1->id = 1;
        lfl_add_tail(test, queue, n2);
        n2->id = 2;
        lfl_add_tail(test, queue, n3);
        n3->id = 3;

        int count = 0;
        lfl_count(test, queue, count);
        cr_expect_eq(count, 3, "expected 3 nodes, got %d", count);

        lfl_remove(test, queue, n2);

        lfl_count(test, queue, count);
        cr_expect_eq(count, 2, "expected 2 non-removed nodes after one removal, got %d", count);

        lfl_clear(test, queue);
}