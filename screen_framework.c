#include "screen_framework.h"

#include "log.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h> // TODO: REMOVE THIS WHEN SCREEN DISPLAYING
#include <string.h>

#define SCREEN_ID_HOME 0
#define SCREEN_DISPLAY_TEXT_MAXLEN 1024

// This is just a string holder. Timestamp, author info, formatting, etc. should
// all be interpolated into the string already. 'msg' will be copied directly 
// into the screen buffer.
typedef struct screenlog_node {
    char *msg;
    struct screenlog_node *prev;
    struct screenlog_node *next;
} screenlog_node;

typedef struct screenlog_list {
    screenlog_node *head;
    screenlog_node *tail;
    int n_msgs;
    int curr_size_bytes;
    int max_size_bytes;
} screenlog_list;

typedef enum screentype {
    SCREENTYPE_HOME,
    SCREENTYPE_CHANNEL_LIST,
    SCREENTYPE_CHANNEL,
    SCREENTYPE_USER_PM,
} screentype;

// TODO: does channel belong in screen_framework?
// TODO: re-eval this. bitfield is possible if necessary
// TODO: lookup 
typedef enum channel_user_state { 
    CHANNEL_USER_STATE_NEVER,
    CHANNEL_USER_STATE_JOINED,
    CHANNEL_USER_STATE_PARTED,
    CHANNEL_USER_STATE_KICKED,
    CHANNEL_USER_STATE_BANNED,
} channel_user_state;

// TODO: does channel belong in screen_framework?
typedef struct channel {
    char *identifier; // E.g., "#testchannel". TODO: can this be fixed size?
    channel_user_state state;
} channel;

typedef struct screen {
    screentype type;
    char display_text[SCREEN_DISPLAY_TEXT_MAXLEN];
    screen_id id;
    screenlog_list scrlog;
    // NULL if type is not SCREENTYPE_CHANNEL
    channel *channel_maybe;
} screen;

// Monotonically increasing counter for assigning screen IDs.
static screen_id screen_id_counter = SCREEN_ID_HOME + 1;

// Call this to get an ID for a new screen. This function will not return the
// same ID twice, but may collide with any manually assigned screen IDs. Don't
// manually assign screen IDs!
static screen_id get_next_screen_id(void);

static screen s_scr_home = {
    .type = SCREENTYPE_HOME,
    .display_text = {"Not Connected"},
    .id = SCREEN_ID_HOME,
    .scrlog = { .max_size_bytes = SCREENLOG_DEFAULT_MAX_BYTES },
    .channel_maybe = NULL
};

static screen *s_scr_active = &s_scr_home;


// This is extremely costly because it traverses the entire list twice (forward
// and backward). Only for use validating the screenlog list code during dev.
static void DEBUG_validate_screenlog_list(const screenlog_list *const scrlog);

// Copies the provided string for use in the new log entry. Use if you need your
// string after calling this or if passing a stack-allocated char[].
static void screenlog_push_copy(screenlog_list *const scrlog, const_str msg);

// Passes ownership of the provided string to the screenlog_list, which will 
// free it when appropriate. Callers should not free the string, continue
// accessing the string after passing, or pass a stack-allocated char[].
static void screenlog_push_take(screenlog_list *const scrlog, char *const msg);

// Removes the least possible number of messages from the back (oldest) of the
// list to free at least the specified number of bytes.
static void screenlog_evict_min_to_free(
        screenlog_list *const scrlog, int free_bytes);


/*****************************************************************************/
/******************************* INTERNAL IMPLs ******************************/

static screen_id get_next_screen_id(void) {
    return screen_id_counter++;
}

static void screenlog_push_copy(screenlog_list *const scrlog, const_str msg) {
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

    // TODO: remove this when screen is drawing
    // AND REMOVE stdio.h!!!
    printf("%s\n", new_node->msg);
}

static void screenlog_push_take(screenlog_list *const scrlog, char *const msg) {
    assert(scrlog != NULL);
    assert(msg != NULL);

    // TODO: debug mode only!
    DEBUG_validate_screenlog_list(scrlog);

    // TODO: Change OOM asserts to proper failure/abort
    screenlog_node *new_node =
        (screenlog_node *) malloc(sizeof(screenlog_node));
    assert(new_node != NULL);

    new_node->msg = msg;

    new_node->next = NULL;
    if (scrlog->tail != NULL) {
        assert(scrlog->head != NULL);
        new_node->prev = scrlog->tail;
        scrlog->tail->next = new_node;
        scrlog->tail = new_node;
        scrlog->n_msgs++;
        scrlog->curr_size_bytes += strlen(new_node->msg) + 1;
        int bytes_left = scrlog->max_size_bytes - scrlog->curr_size_bytes;
        if (bytes_left < 0)
            screenlog_evict_min_to_free(scrlog, 0 - bytes_left);
    }
    else {
        assert(scrlog->head == NULL);
        new_node->prev = NULL;
        scrlog->head = new_node;
        scrlog->tail = new_node;
        scrlog->n_msgs = 1;
        scrlog->curr_size_bytes = strlen(new_node->msg) + 1;
    }

    DEBUG_validate_screenlog_list(scrlog);
}

static void screenlog_evict_min_to_free(
        screenlog_list *const scrlog, int free_bytes)
{
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



/*****************************************************************************/
/********************************* API IMPLs *********************************/

screen_id screen_getid_home(void) {
    return s_scr_home.id;
}

screen_id screen_getid_active(void) {
    return s_scr_active->id;
}

// bool screen_set_active(screen_id scrid) {
//     // Look up scrid in array.
//     // If active, return true
//     // If doesn't exist, return false
//     // Update s_scr_active to point at screen in arr[scrid]
//     // HOW DO WE UPDATE THE UI?
// }

void screen_pushmsg_copy(screen_id scrid, const_str msg) {
    log_fmt(LOGLEVEL_WARNING, "screen_pushmsg_copy(%zd)\n", scrid);
    // TODO: look up the screen first!
    screenlog_push_copy(&s_scr_active->scrlog, msg); 
}

void screen_pushmsg_take(screen_id scrid, char *const msg) {
    log_fmt(LOGLEVEL_WARNING, "screen_pushmsg_take(%zd)\n", scrid);
    // TODO: look up the screen first!
    screenlog_push_take(&s_scr_active->scrlog, msg);
}
