#include "msgqueue.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>


static int b_initialized = false;

static msglist msg_queue_in, msg_queue_out;
static HANDLE mtx_msg_queue_in, mtx_msg_queue_out;

// Returns false if no queue corresponding to `queue_id` was found. Otherwise,
// p_queue and p_mtx will be populated with the corresponding msglist/mtx pair.
static bool select_queue(msg_queue_id id, msglist **p_queue, HANDLE **p_mtx);

static void DEBUG_assert_msglist_valid(msglist *list);

void init_msg_queues() {
    msg_queue_in.head = msg_queue_in.tail = NULL;
    msg_queue_out.head = msg_queue_out.tail = NULL;
    msg_queue_in.count = msg_queue_out.count = 0;

    mtx_msg_queue_in = CreateMutex(NULL, FALSE, NULL);
    mtx_msg_queue_out = CreateMutex(NULL, FALSE, NULL);

    b_initialized = true;
}

void msglist_pushback(msglist *list, char* msg) {
    assert(msg != NULL);
    DEBUG_assert_msglist_valid(list);

    struct msgnode *node = (struct msgnode*)malloc(sizeof(struct msgnode));
    char *msgcpy = (char*)malloc(strlen(msg) + 1);
    if (node == NULL || msgcpy == NULL) {
        // TODO: communicate fatal error
        printf("[msglist_pushback()] FATAL: out of memory.\n");
        exit(23);
    }

    strcpy(msgcpy, msg);
    node->msg = msgcpy;
    node->next = NULL;

    if (list->head == NULL) {
        list->head = list->tail = node;
        list->count = 1;
        return;
    }

    list->tail->next = node;
    list->tail = node;
    list->count++;
}

void msglist_free(msglist *list) {
    DEBUG_assert_msglist_valid(list);

    struct msgnode *prev, *curr;
    prev = curr = list->head;
    while (curr != NULL) {
        free(curr->msg);
        curr = curr->next;
        free(prev);
        prev = curr;
    }

    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

// If there are no messages in the specified queue, `head` and `tail` of the
// returned msglist will be null and `count` will be 0.
// If the returned msglist is NULL, the provided msg_queue_id was invalid.
// NOTE: Callers are required to free the returned `msglist` when they're done
// with it (you can use `msglist_free()` for this).
msglist msg_queue_takeall(msg_queue_id id) {
    assert(b_initialized);

    msglist *p_queue;
    HANDLE *p_mtx;

    if (!select_queue(id, &p_queue, &p_mtx)) {
        printf("[mst_queue_takeall(%d)] No queue found for ID.\n", (int)id);
        // TODO: communicate failure to let main() cleanup
        exit(23);
    }
    assert(p_queue != NULL && p_mtx != NULL);

    DWORD result = WaitForSingleObject(*p_mtx, INFINITE);
    if (result != WAIT_OBJECT_0) {
        printf("[msg_queue_takeall(%d)] WaitForSingleObject() failed: "
                "[%lu] %lu\n", (int)id, result, GetLastError());
        // Right now, any result other than a successful lock is unexpected.
        // TODO: communicate failure to let main() cleanup
        exit(23);
    }

    msglist old_queue = {
        .head = p_queue->head,
        .tail = p_queue->tail,
        .count = p_queue->count
    };

    p_queue->head = p_queue->tail = NULL;
    p_queue->count = 0;

    DEBUG_assert_msglist_valid(&old_queue);
    DEBUG_assert_msglist_valid(p_queue);

    if (!ReleaseMutex(*p_mtx)) {
        // TODO: See if not casting this enum to (int) triggers a warning.
        printf("[msg_queue_takeall(%d)] ReleaseMutex() failed.\n", id);
        // TODO: communicate failure to let main() cleanup
        exit(23);
    }

    return old_queue;
}

void msglist_submit(msg_queue_id id, msglist *list) {
    assert(b_initialized);
    DEBUG_assert_msglist_valid(list);

    msglist *p_queue;
    HANDLE *p_mtx;

    if (!select_queue(id, &p_queue, &p_mtx)) {
        printf("[msglist_submit(%d)] No queue found for ID.\n", (int)id);
        // TODO: communicate failure to let main() cleanup
        exit(23);
    }
    assert(p_queue != NULL && p_mtx != NULL);

    DWORD result = WaitForSingleObject(*p_mtx, INFINITE);
    if (result != WAIT_OBJECT_0) {
        printf("[msglist_submit(%d)] WaitForSingleObject() failed: "
                "[%lu] %lu\n", (int)id, result, GetLastError());
        // Right now, any result other than a successful lock is unexpected.
        // TODO: communicate failure to let main() cleanup
        exit(23);
    }

    DEBUG_assert_msglist_valid(p_queue);
    if (p_queue->head == NULL) {
        p_queue->head = list->head;
        p_queue->tail = list->tail;
        p_queue->count = list->count;
    } else {
        p_queue->tail->next = list->head;
        p_queue->tail = list->tail;
        p_queue->count += list->count;
    }
    DEBUG_assert_msglist_valid(p_queue);

    if (!ReleaseMutex(*p_mtx)) {
        // TODO: See if not casting this enum to (int) triggers a warning.
        printf("[msglist_submit(%d)] ReleaseMutex() failed.\n", id);
        // TODO: communicate failure to let main() cleanup
        exit(23);
    }
}

static bool select_queue(msg_queue_id id, msglist **p_queue, HANDLE **p_mtx) {
    switch (id) {
    case QUEUE_IN:
        *p_queue = &msg_queue_in;
        *p_mtx = &mtx_msg_queue_in;
        return true;
    case QUEUE_OUT:
        *p_queue = &msg_queue_out;
        *p_mtx = &mtx_msg_queue_out;
        return true;
    default:
        *p_queue = NULL;
        *p_mtx = NULL;
        printf("[select_queue(%d)] Invalid msg_queue_id.\n", id);
        return false;
    }
}

static void DEBUG_assert_msglist_valid(msglist *list) {
    assert(list != NULL);

    // If there's no head, there should be no tail.
    if (list->head == NULL) {
        assert(list->tail == NULL);
        return;
    }
    else {
        // If there is a head, there should be a tail...
        assert(list->tail != NULL);
        // ...and the tail should be the end of the list. 
        assert(list->tail->next == NULL);
    }

    // If there's only one element, head and tail should point to the same.
    if (list->head->next == NULL) assert(list->head == list->tail);
}

void DEBUG_print_queue(msg_queue_id id) {
    msglist *p_queue;
    HANDLE *p_mtx;

    if (!select_queue(id, &p_queue, &p_mtx)) {
        printf("[DEBUG_print_queue(%d)] No queue found for ID.\n", (int)id);
        // TODO: communicate failure to let main() cleanup
        exit(23);
    }
    assert(p_queue != NULL && p_mtx != NULL);

    DWORD result = WaitForSingleObject(*p_mtx, INFINITE);
    if (result != WAIT_OBJECT_0) {
        printf("[DEBUG_print_queue(%d)] WaitForSingleObject() failed: "
                "[%lu] %lu\n", (int)id, result, GetLastError());
        // Right now, any result other than a successful lock is unexpected.
        // TODO: communicate failure to let main() cleanup
        exit(23);
    }

    DEBUG_print_msglist(p_queue);

    if (!ReleaseMutex(*p_mtx)) {
        // TODO: See if not casting this enum to (int) triggers a warning.
        printf("[DEBUG_print_queue(%d)] ReleaseMutex() failed.\n", id);
        // TODO: communicate failure to let main() cleanup
        exit(23);
    }

}

void DEBUG_print_msglist(msglist *list) {
    if (list == NULL) {
        printf("{NULL msglist}\n");
        return;
    } else if (list->head == NULL) {
        printf("{Empty msglist}");
        return;
    }

    if (list->tail == NULL) 
        printf("WARNING: List poorly formed (tail is NULL, head is not).\n");
    else if (list->tail->next != NULL)
        printf("WARNING: List poorly formed (tail->next is not NULL).\n");
    

    printf("Count: %zu\n", list->count);
    if (list->head->msg == NULL) printf("HEAD->{msg=NULL}\n");
    else printf("HEAD->{msg=\"%s\"}\n", list->head->msg);

    if (list->tail->msg == NULL) printf("TAIL->{msg=NULL}\n");
    else printf("TAIL->{msg=\"%s\"}\n", list->tail->msg);

    for (struct msgnode *curr = list->head; curr != NULL; curr = curr->next) {
        if (curr->msg == NULL) printf("{msg=NULL}->");
        else printf("{msg=\"%s\"}->", curr->msg);
    }
    printf("NULL\n");
}

