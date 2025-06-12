#include "screen_framework.h"

#include "log.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Monotonically increasing counter for assigning screen IDs.
static unsigned int screen_id_counter = 1;

// This is extremely costly because it traverses the entire list twice (forward
// and backward). Only for use validating the screenlog list code during dev.
static void DEBUG_validate_screenlog_list(const screenlog_list *const log);

unsigned int get_next_screen_id(void) {
    return screen_id_counter++;
}

void screenlog_pushback_copy(screenlog_list *const scrlog, const_str msg) {
    assert(scrlog != NULL);
    assert(msg != NULL);

    // TODO: debug mode only!
    DEBUG_validate_screenlog_list(scrlog);

    // TODO: Change OOM asserts to proper failure/abort
    screenlog_node *new_node = 
        (screenlog_node *) malloc(sizeof(screenlog_node));
    assert(new_node != NULL);

    size_t msg_bytes = (strlen(msg) + 1) * sizeof(char);
    new_node->msg = (char *) malloc(msg_bytes);
    assert(new_node->msg != NULL);

    strcpy_s(new_node->msg, msg_bytes, msg);

    new_node->next = NULL;
    if (scrlog->tail != NULL) {
        assert(scrlog->head != NULL);
        new_node->prev = scrlog->tail;
        scrlog->tail->next = new_node;
        scrlog->tail = new_node;
        scrlog->n_msgs++;
        scrlog->curr_size_bytes += msg_bytes;
        int bytes_left = scrlog->max_size_bytes - scrlog->curr_size_bytes;
        if (bytes_left < 0)
            screenlog_evict_min_to_free(scrlog, 0 - bytes_left);
    }
    else {
        assert(scrlog->head == NULL);
        assert((int) msg_bytes > 0);
        assert(scrlog->max_size_bytes >= (int) msg_bytes);
        new_node->prev = NULL;
        scrlog->head = new_node;
        scrlog->tail = new_node;
        scrlog->n_msgs = 1;
        scrlog->curr_size_bytes = msg_bytes;
    }

    DEBUG_validate_screenlog_list(scrlog);
}

void screenlog_pushback_take(screenlog_list *const screenlog, char *const msg) {
    assert(screenlog != NULL);
    assert(msg != NULL);

    // TODO: debug mode only!
    DEBUG_validate_screenlog_list(screenlog);

    // TODO: Change OOM asserts to proper failure/abort
    screenlog_node *new_node =
        (screenlog_node *) malloc(sizeof(screenlog_node));
    assert(new_node != NULL);

    new_node->msg = msg;

    new_node->next = NULL;
    if (screenlog->tail != NULL) {
        assert(screenlog->head != NULL);
        new_node->prev = screenlog->tail;
        screenlog->tail->next = new_node;
        screenlog->tail = new_node;
        screenlog->n_msgs++;
        screenlog->curr_size_bytes += strlen(new_node->msg) + 1;
        int bytes_left = screenlog->max_size_bytes - screenlog->curr_size_bytes;
        if (bytes_left < 0)
            screenlog_evict_min_to_free(screenlog, 0 - bytes_left);
    }
    else {
        assert(screenlog->head == NULL);
        new_node->prev = NULL;
        screenlog->head = new_node;
        screenlog->tail = new_node;
        screenlog->n_msgs = 1;
        screenlog->curr_size_bytes = strlen(new_node->msg) + 1;
    }

    DEBUG_validate_screenlog_list(screenlog);
}

void screenlog_evict_min_to_free(screenlog_list *const scrlog, int free_bytes) {
    assert(scrlog != NULL);
    assert(scrlog->head != NULL);
    assert(scrlog->tail != NULL);
    assert(free_bytes > 0);
    assert(free_bytes <= scrlog->curr_size_bytes);

    DEBUG_validate_screenlog_list(scrlog);

    screenlog_node *curr = scrlog->head, *evictme = NULL;
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
    scrlog->curr_size_bytes -= freed_bytes;
    scrlog->n_msgs -= evicted_nodes;
    scrlog->head = curr;
    if (curr == NULL) scrlog->tail = curr;

    assert(scrlog->curr_size_bytes >= 0);
    assert(scrlog->n_msgs >= 0);

    DEBUG_validate_screenlog_list(scrlog);
}

static void DEBUG_validate_screenlog_list(const screenlog_list *const scrlog) {
    assert(scrlog != NULL);

    // Sanity for counting values. Max should never be set or left at 0.
    assert(scrlog->n_msgs >= 0);
    assert(scrlog->curr_size_bytes >= 0);
    assert(scrlog->max_size_bytes > 0);

    // If head or tail is NULL, so is the other, and it's an empty list.
    if (scrlog->head == NULL) {
        assert(scrlog->tail == NULL);
        assert(scrlog->n_msgs == 0);
        assert(scrlog->curr_size_bytes == 0);
    } else if (scrlog->tail == NULL) {
        assert(scrlog->head == NULL);
    }

    // If a counting value is zero, the size should be too and the list empty.
    if (scrlog->n_msgs == 0) {
        assert(scrlog->curr_size_bytes == 0);
        assert(scrlog->head == NULL);
        assert(scrlog->tail == NULL);
    } else if (scrlog->curr_size_bytes == 0) {
        assert(scrlog->n_msgs == 0);
        assert(scrlog->head == NULL);
        assert(scrlog->tail == NULL);
    } 

    // Now actually validate the values...
    screenlog_node *curr = scrlog->head, *last = scrlog->head;
    int actual_size_bytes = 0, actual_n_msgs = 0;
    while (curr != NULL) {
        actual_size_bytes += strlen(curr->msg) + 1;
        actual_n_msgs++;
        last = curr;
        curr = curr->next;
    }
    assert(last == scrlog->tail);
    log_fmt(LOGLEVEL_DEV, "actual_size_bytes=%d, curr_size_bytes=%d\n"
            "actual_n_msgs=%d, n_msgs=%d\n",
            actual_size_bytes, scrlog->curr_size_bytes,
            actual_n_msgs, scrlog->n_msgs);
    assert(actual_size_bytes == scrlog->curr_size_bytes);
    assert(actual_n_msgs == scrlog->n_msgs);

    // Validate the prev pointers too
    curr = last = scrlog->tail;
    actual_size_bytes = actual_n_msgs = 0;
    while (curr != NULL) {
        actual_size_bytes += strlen(curr->msg) + 1;
        actual_n_msgs++;
        last = curr;
        curr = curr->prev;
    }
    assert(last == scrlog->head);
    assert(actual_size_bytes == scrlog->curr_size_bytes);
    assert(actual_n_msgs == scrlog->n_msgs);
}
