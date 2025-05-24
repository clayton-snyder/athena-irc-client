// Manages atomic access to message queues.
//
// A `msglist` is a list of messages that clients can use for two purposes:
//     1. To build a list of messages to submit to the global queue.
//     2. To receive a list of messages from the global queue to process.
//
// `msg_queue` refers to one of two global `msglist`s managed by this module to
// atomically orchestrate the flow of messages between threads:
//      * The in queue (QUEUE_IN), which holds messages received from the server
//        that have not yet been processed.
//      * The out queue (QUEUE_OUT) holds messages submitted by the user that
//        have not yet been sent to the server.
//
// `msglist`s should only be added to (and then only when building a list to
// submit to QUEUE_OUT). No popback() is provided because the intention is for 
// the whole list to be processed at once then freed. When receiving a list,
// i.e., by taking a queue with msg_queue_takeall(), prefer to loop the entire
// thing, processing each message accordingly, then pass it to msglist_free().

#include <stddef.h>

struct msgnode {
    char* msg;
    struct msgnode* next;
};

typedef struct msglist {
    struct msgnode* head;
    struct msgnode* tail;
    size_t count;
} msglist;

typedef enum { QUEUE_IN, QUEUE_OUT, QUEUE_UI } msg_queue_id;

// Add message to the end of the provided list. `msg` must be a null-terminated
// string, and it will be copied into the appended list entry, so it's safe to
// pass stack allocated strings.
void msglist_pushback(msglist *list, char* msg);

// Must be called prior to using any msg_queue-related functionality.
void init_msg_queues();

// If there are no messages in the specified queue, `head` and `tail` of the
// returned msglist will be null and `count` will be 0.
// If the returned msglist is NULL, the provided msg_queue_id was invalid.
msglist msg_queue_takeall(msg_queue_id id);

// Use to add a `msglist` to the end of the specified global message queue.
void msglist_submit(msg_queue_id id, msglist *msgs);

// Frees all allocations made for the provided list and sets head/tail to NULL.
void msglist_free(msglist *list);

// Takes a mutex lock, but won't affect the queue.
void DEBUG_print_queue(msg_queue_id id);

// Won't affect the list.
void DEBUG_print_msglist(msglist *list);

