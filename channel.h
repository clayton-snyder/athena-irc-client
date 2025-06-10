#pragma once

// TODO: very small for now for testing. Probably 1MB is a better default.
// TODO: also need a global max and global tracking.
// TODO: also need per-msg max
#define CHANLOG_DEFAULT_MAX_BYTES 1024 * 10

// This is just a string holder. Timestamp, author info, formatting, etc. should
// all be interpolated into the string already. 'msg' will be copied directly 
// into the screen buffer.
typedef struct chanlog_node {
    char *msg;
    struct chanlog_node *prev;
    struct chanlog_node *next;
} chanlog_node;

typedef struct chanlog_list {
    char *title; // TODO: do we need this here? Maybe just ID?
    int id;
    chanlog_node *head;
    chanlog_node *tail;
    int n_msgs;
    int curr_size_bytes;
    int max_size_bytes;
} chanlog_list;

// TODO: re-eval this. bitfield is possible if necessary
typedef enum channel_user_state { 
    CHANNEL_USER_STATE_NEVER,
    CHANNEL_USER_STATE_JOINED,
    CHANNEL_USER_STATE_PARTED,
    CHANNEL_USER_STATE_KICKED,
    CHANNEL_USER_STATE_BANNED,
}

typedef struct channel {
    char *name; // TODO: can this be fixed size?
    chanlog_list log;
    channel_user_state state;
}

// Copies the provided string for use in the new log entry. Use if you need your
// string after calling this or if passing a stack-allocated char[].
void chanlog_pushback_copy(chanlog_list *const log, const char *const msg);

// Passes ownership of the provided string to the chanlog_list, which will free
// it when appropriate. Callers should not free the string, continue accessing 
// the string after passing, or pass a stack-allocated char[].
void chanlog_pushback_take(chanlog_list *const log, char *const msg);

// Removes the least possible number of messages from the back (oldest) of the
// list to free at least the specified number of bytes.
void chanlog_evict_min_to_free(chanlog_list *const log, int free_bytes);

