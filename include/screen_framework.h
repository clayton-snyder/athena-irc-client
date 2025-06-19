#pragma once

#include "athena_types.h"

#include <stdbool.h>
#include <stddef.h>

// 511 bytes for the message + metadata (timestamp, author) + ANSI escapes
// for formatting. 1024 is generous, but these should get re-used.
#define SCREENMSG_BUF_SIZE 1024

// TODO: very small for now for testing. Probably 1MB is a better default.
// TODO: also need a global max and global tracking.
// TODO: in header or impl?
#define SCREENLOG_DEFAULT_MAX_BYTES 1024 * 10

// TODO: Re-eval this. Effectively it's the max # of characters user can type.
#define UI_INPUT_BUF_SIZE 200

// State info for the UI on a particular screen. One per screen. This can be
// used to save/restore the UI state when switching between screens.
typedef struct screen_ui_state {
    const char* prompt;
    char inputbuf[UI_INPUT_BUF_SIZE];
    size_t i_inputbuf;
    int scroll;
    bool scroll_at_top;
} screen_ui_state;

typedef unsigned int screen_id;

screen_id screen_getid_home(void);

screen_id screen_getid_active(void);

// Only the active screen's UI state can be accessed. This could be changed, but
// I'll wait until there's a use case.
screen_ui_state *const screen_get_active_ui_state(void);

// bool screen_set_active(screen_id scrid);

// Copies the provided string for use in the new log entry. Use if you need your
// string after calling this or if passing a stack-allocated char[].
void screen_pushmsg_copy(screen_id scrid, const_str msg);

// Passes ownership of the provided string to the screenlog_list, which will 
// free it when appropriate. Callers should not free the string, continue
// accessing the string after passing, or pass a stack-allocated char[].
void screen_pushmsg_take(screen_id scrid, char *const msg);

// Writes the section (according to scroll) of the active screen's message log
// that will fit in the specified number of rows and columns to the global
// screen buffer.
int screen_fmt_to_buf(char *buf, size_t buflen, int buf_rows, int term_cols);

// Prints the details of every character in 'msg', showing its index, unsigned
// hex value, and printing the character. If the character is non-printable, a
// space is printed and a tilde (~) is placed after the entry to signify that
// it's not an actual space character. Each entry is exactly the same width so
// they line up and are easy to read/scan through. The index and hex value are
// gray, while the character itself is red and bold.
// After printing 'msg', the results of a call to 'calc_screen_offset()' with
// the provided rows/cols args are displayed, and the replay buffer is printed
// similarly to the 'msg' buffer.
// This function is extremely useful for detecting problems with control 
// character counting and parsing. You can send the populated buffer to the
// logger to print, or pass it to somewhere with access to an output file handle
// to write yourself.
// WARNINGS:
//      * This function prints a lot of characters, so pass it a big buffer, and
//        don't print the results in a tight loop. It should be called ad-hoc,
//        not once per frame.
//      * The asserts in calc_screen_offset() will fire if the message is large
//        enough to fit entirely in the rows and cols you provide.
size_t screen_DEBUG_char_details_buf(
        char *buf, const_str msg, size_t bufsize,
        size_t entries_per_row,
        size_t offset_calc_rows, size_t offset_calc_cols);
