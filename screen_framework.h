#pragma once

#include "athena_types.h"

// 511 bytes for the message + metadata (timestamp, author) + ANSI escapes
// for formatting. 1024 is generous, but these should get re-used.
#define SCREENMSG_BUF_SIZE 1024

// TODO: very small for now for testing. Probably 1MB is a better default.
// TODO: also need a global max and global tracking.
// TODO: in header or impl?
#define SCREENLOG_DEFAULT_MAX_BYTES 1024 * 10

typedef unsigned int screen_id;

screen_id screen_getid_home(void);

screen_id screen_getid_active(void);

// bool screen_set_active(screen_id scrid);

// Copies the provided string for use in the new log entry. Use if you need your
// string after calling this or if passing a stack-allocated char[].
void screen_pushmsg_copy(screen_id scrid, const_str msg);

// Passes ownership of the provided string to the screenlog_list, which will 
// free it when appropriate. Callers should not free the string, continue
// accessing the string after passing, or pass a stack-allocated char[].
void screen_pushmsg_take(screen_id scrid, char *const msg);

