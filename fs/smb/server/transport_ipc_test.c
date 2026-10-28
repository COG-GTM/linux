// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit tests for ksmbd IPC handles. Included from transport_ipc.c.
 */

#include <kunit/test.h>

static void ipc_handle_not_reused_after_free(struct kunit *test)
{
	int a, b;

	a = ksmbd_ipc_id_alloc();
	KUNIT_ASSERT_GE(test, a, 0);
	ipc_msg_handle_free(a);		/* request timed out, handle released */
	b = ksmbd_ipc_id_alloc();
	KUNIT_ASSERT_GE(test, b, 0);
	KUNIT_EXPECT_NE(test, a, b);
	ipc_msg_handle_free(b);
}

static void ipc_late_response_not_delivered_to_new_request(struct kunit *test)
{
	struct ksmbd_tree_connect_response resp = {};
	struct ipc_msg_table_entry entry;
	int old, cur;

	old = ksmbd_ipc_id_alloc();	/* Bob's tree connect */
	KUNIT_ASSERT_GE(test, old, 0);
	ipc_msg_handle_free(old);	/* ...timed out */

	cur = ksmbd_ipc_id_alloc();	/* Mallory's tree connect */
	KUNIT_ASSERT_GE(test, cur, 0);
	entry.type = KSMBD_EVENT_TREE_CONNECT_REQUEST;
	entry.response = NULL;
	entry.handle = cur;
	init_waitqueue_head(&entry.wait);
	down_write(&ipc_msg_table_lock);
	hash_add(ipc_msg_table, &entry.ipc_table_hlist, entry.handle);
	up_write(&ipc_msg_table_lock);

	/* Bob's late reply: status OK, writable */
	resp.handle = old;
	resp.status = KSMBD_TREE_CONN_STATUS_OK;
	resp.connection_flags = KSMBD_TREE_CONN_FLAG_WRITABLE;
	handle_response(KSMBD_EVENT_TREE_CONNECT_RESPONSE, &resp, sizeof(resp));
	KUNIT_EXPECT_NULL(test, entry.response);

	/* Mallory's own reply is still delivered */
	resp.handle = cur;
	resp.status = KSMBD_TREE_CONN_STATUS_NO_SHARE;
	resp.connection_flags = 0;
	handle_response(KSMBD_EVENT_TREE_CONNECT_RESPONSE, &resp, sizeof(resp));
	KUNIT_ASSERT_NOT_NULL(test, entry.response);
	KUNIT_EXPECT_EQ(test, ((struct ksmbd_tree_connect_response *)entry.response)->status,
			(unsigned int)KSMBD_TREE_CONN_STATUS_NO_SHARE);

	down_write(&ipc_msg_table_lock);
	hash_del(&entry.ipc_table_hlist);
	up_write(&ipc_msg_table_lock);
	kvfree(entry.response);
	ipc_msg_handle_free(cur);
}

static void ipc_handle_skips_live_rpc_ids_and_wraps(struct kunit *test)
{
	int rpc, a, b, c;

	rpc = ksmbd_ipc_id_alloc();	/* long-lived RPC pipe id */
	KUNIT_ASSERT_GE(test, rpc, 0);
	mutex_lock(&ipc_ida_lock);
	ipc_ida_next = rpc;
	mutex_unlock(&ipc_ida_lock);
	a = ksmbd_ipc_id_alloc();
	KUNIT_EXPECT_GT(test, a, rpc);
	ipc_msg_handle_free(a);

	mutex_lock(&ipc_ida_lock);
	ipc_ida_next = INT_MAX;
	mutex_unlock(&ipc_ida_lock);
	b = ksmbd_ipc_id_alloc();
	KUNIT_EXPECT_EQ(test, b, INT_MAX);
	c = ksmbd_ipc_id_alloc();	/* wrapped */
	KUNIT_EXPECT_GE(test, c, 0);
	KUNIT_EXPECT_LT(test, c, INT_MAX);
	KUNIT_EXPECT_NE(test, c, rpc);
	ipc_msg_handle_free(b);
	ipc_msg_handle_free(c);
	ksmbd_rpc_id_free(rpc);
}

static struct kunit_case ksmbd_ipc_handle_cases[] = {
	KUNIT_CASE(ipc_handle_not_reused_after_free),
	KUNIT_CASE(ipc_late_response_not_delivered_to_new_request),
	KUNIT_CASE(ipc_handle_skips_live_rpc_ids_and_wraps),
	{}
};

static struct kunit_suite ksmbd_ipc_handle_suite = {
	.name = "ksmbd_ipc_handle",
	.test_cases = ksmbd_ipc_handle_cases,
};
kunit_test_suite(ksmbd_ipc_handle_suite);
