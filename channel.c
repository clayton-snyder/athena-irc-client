#include "channel.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// This is extremely costly because it traverses the entire list twice (forward
// and backward). Only for use validating the chanlog list code during dev.
static void DEBUG_validate_chanlog_list(const chanlog_list *const log);

void chanlog_pushback_copy(chanlog_list *const log, const char *const msg) {
    assert(log != NULL);
    assert(msg != NULL);

    // TODO: debug mode only!
    DEBUG_validate_chanlog_list(log);

    // TODO: Change OOM asserts to proper failure/abort
    chanlog_node *new_node = (chanlog_node *) malloc(sizeof(chanlog_node));
    assert(new_node != NULL);

    rsize_t msg_bytes = (strlen(msg) + 1) * sizeof(char);
    new_node->msg = (char *) malloc(msg_bytes);
    assert(new_node->msg != NULL);

    strcpy_s(new_node->msg, msg_bytes, msg);

    new_node->next = NULL;
    if (log->tail != NULL) {
        assert(log->head != NULL);
        new_node->prev = log->tail;
        log->n_msgs++;
        log->curr_size_bytes += msg_bytes;
        int bytes_left = log->max_size_bytes - log->curr_size_bytes;
        if (bytes_left < 0)
            chanlog_evict_min_to_free(log, 0 - bytes_left);
    }
    else {
        assert(log->head == NULL);
        assert((int) msg_bytes > 0);
        assert(log->max_size_bytes >= (int) msg_bytes);
        new_node->prev = NULL;
        log->head = new_node;
        log->tail = new_node;
        log->n_msgs = 1;
        log->curr_size_bytes = msg_bytes;
    }

    DEBUG_validate_chanlog_list(log);
}

void chanlog_pushback_take(chanlog_list *const log, char *const msg) {
    assert(log != NULL);
    assert(msg != NULL);

    // TODO: debug mode only!
    DEBUG_validate_chanlog_list(log);

    // TODO: Change OOM asserts to proper failure/abort
    chanlog_node *new_node = (chanlog_node *) malloc(sizeof(chanlog_node));
    assert(new_node != NULL);

    new_node->msg = msg;

    new_node->next = NULL;
    if (log->tail != NULL) {
        assert(log->head != NULL);
        new_node->prev = log->tail;
        log->n_msgs++;
        log->curr_size_bytes += strlen(new_node->msg) + 1;
        int bytes_left = log->max_size_bytes - log->curr_size_bytes;
        if (bytes_left < 0)
            chanlog_evict_min_to_free(log, 0 - bytes_left);
    }
    else {
        assert(log->head == NULL);
        new_node->prev = NULL;
        log->head = new_node;
        log->tail = new_node;
        log->n_msgs = 1;
        log->curr_size_bytes = strlen(new_node->msg) + 1;
    }

    DEBUG_validate_chanlog_list(log);
}

void chanlog_evict_min_to_free(chanlog_list *const log, int free_bytes) {
    assert(log != NULL);
    assert(log->head != NULL);
    assert(log->tail != NULL);
    assert(free_bytes > 0);
    assert(free_bytes <= log->curr_size_bytes);

    DEBUG_validate_chanlog_list(log);

    chanlog_node *curr = log->head, *evictme = NULL;
    int freed_bytes = 0, evicted_nodes = 0;
    while (freed_bytes < free_bytes) {
        assert (curr != NULL);
        evictme = curr;
        curr = curr->next;

        size_t msg_bytes = strlen(evictme->msg) + 1;

        free(evictme->msg);
        freed_bytes += msg_bytes; // TODO: type mismatch (cast strlen to int)

        free(evictme);
        evicted_nodes++;

        if (curr)
            curr->prev = NULL;
    }
    log->curr_size_bytes -= freed_bytes;
    log->n_msgs -= evicted_nodes;
    log->head = curr;
    if (curr == NULL) log->tail = curr;

    assert(log->curr_size_bytes >= 0);
    assert(log->n_msgs >= 0);

    DEBUG_validate_chanlog_list(log);
}

static void DEBUG_validate_chanlog_list(const chanlog_list *const log) {
    assert(log != NULL);

    // Sanity for counting values. Max should never be set or left at 0.
    assert(log->n_msgs >= 0);
    assert(log->curr_size_bytes >= 0);
    assert(log->max_size_bytes > 0);

    // If head or tail is NULL, so is the other, and it's an empty list.
    if (log->head == NULL) {
        assert(log->tail == NULL);
        assert(log->n_msgs == 0);
        assert(log->curr_size_bytes == 0);
    } else if (log->tail == NULL) {
        assert(log->head == NULL);
    }

    // If a counting value is zero, the size should be too and the list empty.
    if (log->n_msgs == 0) {
        assert(log->curr_size_bytes == 0);
        assert(log->head == NULL);
        assert(log->tail == NULL);
    } else if (log->curr_size_bytes == 0) {
        assert(log->n_msgs == 0);
        assert(log->head == NULL);
        assert(log->tail == NULL);
    } 

    // Now actually validate the values...
    chanlog_node *curr = log->head, *last = log->head;
    int actual_size_bytes = 0, actual_n_msgs = 0;
    while (curr != NULL) {
        actual_size_bytes += strlen(curr->msg) + 1;
        actual_n_msgs++;
        last = curr;
        curr = curr->next;
    }
    assert(last == log->tail);
    assert(actual_size_bytes == log->curr_size_bytes);
    assert(actual_n_msgs == log->n_msgs);

    // Validate the prev pointers too
    curr = last = log->tail;
    actual_size_bytes = actual_n_msgs = 0;
    while (curr != NULL) {
        actual_size_bytes += strlen(curr->msg) + 1;
        actual_n_msgs++;
        last = curr;
        curr = curr->prev;
    }
    assert(last == log->head);
    assert(actual_size_bytes == log->curr_size_bytes);
    assert(actual_n_msgs == log->n_msgs);
}
