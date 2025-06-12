#pragma once

#include "athena_types.h"

// TODO: very small for now for testing. Probably 1MB is a better default.
// TODO: also need a global max and global tracking.
// TODO: also need per-msg max
#define SCREENLOG_DEFAULT_MAX_BYTES 1024 * 10


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
    CONTEXTTYPE_SERVER_HOME,
    CONTEXTTYPE_SERVER_CHANNEL_LIST,
    CONTEXTTYPE_CHANNEL,
    CONTEXTTYPE_USER_PM,
} screentype;

// TODO: re-eval this. bitfield is possible if necessary
// TODO: lookup 
typedef enum channel_user_state { 
    CHANNEL_USER_STATE_NEVER,
    CHANNEL_USER_STATE_JOINED,
    CHANNEL_USER_STATE_PARTED,
    CHANNEL_USER_STATE_KICKED,
    CHANNEL_USER_STATE_BANNED,
} channel_user_state;

typedef struct channel {
    char *identifier; // E.g., "#testchannel". TODO: can this be fixed size?
    channel_user_state state;
} channel;

typedef struct screen {
    screentype type;
    char *display_text; // Channel/server name, "Channels in {Server}", etc.
    unsigned int id; // TODO: needed?
    screenlog_list scrlog;
    // NULL if type is not CONTEXTTYPE_CHANNEL
    channel *channel_maybe;
} screen;

// Call this to get an ID for a new screen. This function will not return the
// same ID twice, but may collide with any manually assigned screen IDs. Don't
// manually assign screen IDs!
unsigned int get_next_screen_id(void);

// Copies the provided string for use in the new log entry. Use if you need your
// string after calling this or if passing a stack-allocated char[].
void screenlog_pushback_copy(screenlog_list *const screenlog, const_str msg);

// Passes ownership of the provided string to the screenlog_list, which will free
// it when appropriate. Callers should not free the string, continue accessing 
// the string after passing, or pass a stack-allocated char[].
void screenlog_pushback_take(screenlog_list *const screenlog, char *const msg);

// Removes the least possible number of messages from the back (oldest) of the
// list to free at least the specified number of bytes.
void screenlog_evict_min_to_free(screenlog_list *const screenlog, int free_bytes);

