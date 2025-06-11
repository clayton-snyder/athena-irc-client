#include "context_framework.h"

#include "log.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Monotonically increasing counter for assigning context IDs.
static unsigned int context_id_counter = 1;

// This is extremely costly because it traverses the entire list twice (forward
// and backward). Only for use validating the msglog list code during dev.
static void DEBUG_validate_msglog_list(const msglog_list *const msglog);

unsigned int get_next_context_id(void) {
    return context_id_counter++;
}

void msglog_pushback_copy(msglog_list *const msglog, const_str msg) {
    assert(msglog != NULL);
    assert(msg != NULL);

    // TODO: debug mode only!
    DEBUG_validate_msglog_list(msglog);

    // TODO: Change OOM asserts to proper failure/abort
    msglog_node *new_node = (msglog_node *) malloc(sizeof(msglog_node));
    assert(new_node != NULL);

    size_t msg_bytes = (strlen(msg) + 1) * sizeof(char);
    new_node->msg = (char *) malloc(msg_bytes);
    assert(new_node->msg != NULL);

    strcpy_s(new_node->msg, msg_bytes, msg);

    new_node->next = NULL;
    if (msglog->tail != NULL) {
        assert(msglog->head != NULL);
        new_node->prev = msglog->tail;
        msglog->tail->next = new_node;
        msglog->tail = new_node;
        msglog->n_msgs++;
        msglog->curr_size_bytes += msg_bytes;
        int bytes_left = msglog->max_size_bytes - msglog->curr_size_bytes;
        if (bytes_left < 0)
            msglog_evict_min_to_free(msglog, 0 - bytes_left);
    }
    else {
        assert(msglog->head == NULL);
        assert((int) msg_bytes > 0);
        assert(msglog->max_size_bytes >= (int) msg_bytes);
        new_node->prev = NULL;
        msglog->head = new_node;
        msglog->tail = new_node;
        msglog->n_msgs = 1;
        msglog->curr_size_bytes = msg_bytes;
    }

    DEBUG_validate_msglog_list(msglog);
}

void msglog_pushback_take(msglog_list *const msglog, char *const msg) {
    assert(msglog != NULL);
    assert(msg != NULL);

    // TODO: debug mode only!
    DEBUG_validate_msglog_list(msglog);

    // TODO: Change OOM asserts to proper failure/abort
    msglog_node *new_node = (msglog_node *) malloc(sizeof(msglog_node));
    assert(new_node != NULL);

    new_node->msg = msg;

    new_node->next = NULL;
    if (msglog->tail != NULL) {
        assert(msglog->head != NULL);
        new_node->prev = msglog->tail;
        msglog->tail->next = new_node;
        msglog->tail = new_node;
        msglog->n_msgs++;
        msglog->curr_size_bytes += strlen(new_node->msg) + 1;
        int bytes_left = msglog->max_size_bytes - msglog->curr_size_bytes;
        if (bytes_left < 0)
            msglog_evict_min_to_free(msglog, 0 - bytes_left);
    }
    else {
        assert(msglog->head == NULL);
        new_node->prev = NULL;
        msglog->head = new_node;
        msglog->tail = new_node;
        msglog->n_msgs = 1;
        msglog->curr_size_bytes = strlen(new_node->msg) + 1;
    }

    DEBUG_validate_msglog_list(msglog);
}

void msglog_evict_min_to_free(msglog_list *const msglog, int free_bytes) {
    assert(msglog != NULL);
    assert(msglog->head != NULL);
    assert(msglog->tail != NULL);
    assert(free_bytes > 0);
    assert(free_bytes <= msglog->curr_size_bytes);

    DEBUG_validate_msglog_list(msglog);

    msglog_node *curr = msglog->head, *evictme = NULL;
    int freed_bytes = 0, evicted_nodes = 0;
    while (freed_bytes < free_bytes) {
        assert (curr != NULL);
        evictme = curr;
        curr = curr->next;

        size_t msg_bytes = strlen(evictme->msg) + 1;

        free(evictme->msg);
        freed_bytes += msg_bytes;

        free(evictme);
        evicted_nodes++;

        if (curr)
            curr->prev = NULL;
    }
    msglog->curr_size_bytes -= freed_bytes;
    msglog->n_msgs -= evicted_nodes;
    msglog->head = curr;
    if (curr == NULL) msglog->tail = curr;

    assert(msglog->curr_size_bytes >= 0);
    assert(msglog->n_msgs >= 0);

    DEBUG_validate_msglog_list(msglog);
}

static void DEBUG_validate_msglog_list(const msglog_list *const msglog) {
    assert(msglog != NULL);

    // Sanity for counting values. Max should never be set or left at 0.
    assert(msglog->n_msgs >= 0);
    assert(msglog->curr_size_bytes >= 0);
    assert(msglog->max_size_bytes > 0);

    // If head or tail is NULL, so is the other, and it's an empty list.
    if (msglog->head == NULL) {
        assert(msglog->tail == NULL);
        assert(msglog->n_msgs == 0);
        assert(msglog->curr_size_bytes == 0);
    } else if (msglog->tail == NULL) {
        assert(msglog->head == NULL);
    }

    // If a counting value is zero, the size should be too and the list empty.
    if (msglog->n_msgs == 0) {
        assert(msglog->curr_size_bytes == 0);
        assert(msglog->head == NULL);
        assert(msglog->tail == NULL);
    } else if (msglog->curr_size_bytes == 0) {
        assert(msglog->n_msgs == 0);
        assert(msglog->head == NULL);
        assert(msglog->tail == NULL);
    } 

    // Now actually validate the values...
    msglog_node *curr = msglog->head, *last = msglog->head;
    int actual_size_bytes = 0, actual_n_msgs = 0;
    while (curr != NULL) {
        actual_size_bytes += strlen(curr->msg) + 1;
        actual_n_msgs++;
        last = curr;
        curr = curr->next;
    }
    assert(last == msglog->tail);
    log_fmt(LOGLEVEL_DEV, "actual_size_bytes=%d, curr_size_bytes=%d\n"
            "actual_n_msgs=%d, n_msgs=%d\n",
            actual_size_bytes, msglog->curr_size_bytes,
            actual_n_msgs, msglog->n_msgs);
    assert(actual_size_bytes == msglog->curr_size_bytes);
    assert(actual_n_msgs == msglog->n_msgs);

    // Validate the prev pointers too
    curr = last = msglog->tail;
    actual_size_bytes = actual_n_msgs = 0;
    while (curr != NULL) {
        actual_size_bytes += strlen(curr->msg) + 1;
        actual_n_msgs++;
        last = curr;
        curr = curr->prev;
    }
    assert(last == msglog->head);
    assert(actual_size_bytes == msglog->curr_size_bytes);
    assert(actual_n_msgs == msglog->n_msgs);
}
