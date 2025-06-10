#pragma once

// TODO: very small for now for testing. Probably 1MB is a better default.
// TODO: also need a global max and global tracking.
// TODO: also need per-msg max
#define MSGLOG_DEFAULT_MAX_BYTES 1024 * 10


// This is just a string holder. Timestamp, author info, formatting, etc. should
// all be interpolated into the string already. 'msg' will be copied directly 
// into the screen buffer.
typedef struct msglog_node {
    char *msg;
    struct msglog_node *prev;
    struct msglog_node *next;
} msglog_node;

typedef struct msglog_list {
    msglog_node *head;
    msglog_node *tail;
    int n_msgs;
    int curr_size_bytes;
    int max_size_bytes;
} msglog_list;


typedef enum contexttype {
    CONTEXTTYPE_SERVER_HOME,
    CONTEXTTYPE_SERVER_CHANNEL_LIST,
    CONTEXTTYPE_CHANNEL,
    CONTEXTTYPE_USER_PM,
} contexttype;

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
    msglog_list log;
    channel_user_state state;
} channel;

typedef struct context {
    contexttype type;
    char *display_text; // Channel/server name, "Channels in {Server}", etc.
    unsigned int id; // TODO: needed?
    msglog_list msglog;
    // NULL if type is not CONTEXTTYPE_CHANNEL
    channel *channel_maybe;
} context;

// Call this to get an ID for a new context. This function will not return the
// same ID twice, but may collide with any manually assigned context IDs. Don't
// manually assign context IDs!
unsigned int get_next_context_id(void);

// Copies the provided string for use in the new log entry. Use if you need your
// string after calling this or if passing a stack-allocated char[].
void msglog_pushback_copy(msglog_list *const msglog, const char *const msg);

// Passes ownership of the provided string to the msglog_list, which will free
// it when appropriate. Callers should not free the string, continue accessing 
// the string after passing, or pass a stack-allocated char[].
void msglog_pushback_take(msglog_list *const msglog, char *const msg);

// Removes the least possible number of messages from the back (oldest) of the
// list to free at least the specified number of bytes.
void msglog_evict_min_to_free(msglog_list *const msglog, int free_bytes);

