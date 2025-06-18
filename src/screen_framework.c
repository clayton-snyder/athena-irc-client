#include "screen_framework.h"

#include "log.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h> // TODO: REMOVE THIS WHEN SCREEN DISPLAYING
#include <string.h>
#include <windows.h> // TODO: REMOVe WHEN DONE WITH UNREFERENCED_PARAMETER()

#define SCREEN_ID_HOME 0
#define SCREEN_DISPLAY_TEXT_MAXLEN 1024

// ANSI escape character used to signal the beginning of a formatting sequence.
#define ESC "\033"

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
    screen_ui_state ui_state;
    // NULL if type is not SCREENTYPE_CHANNEL
    channel *channel_maybe;
} screen;

// Monotonically increasing counter for assigning screen IDs.
static screen_id s_screen_id_counter = SCREEN_ID_HOME + 1;

// Used to hold the skipped ANSI escape sequences for formatting when printing
// a partial wrapped message so they can be replayed in order.
static char s_skipped_seqs_buf[SCREENMSG_BUF_SIZE] = { 0 };

// Call this to get an ID for a new screen. This function will not return the
// same ID twice, but may collide with any manually assigned screen IDs. Don't
// manually assign screen IDs!
static screen_id get_next_screen_id(void);

// Utility functions for formatting a scrlog to a rowsXcols screen.

// Returns the offset index up to which the provided message could be printed
// and fit in the specified number of rows and columns.
// If provided, n_visible_chars will be populated with the actual number of
// visible characters that would be printed up to that offset (i.e., excluding
// characters that are part of ANSI escape sequences).
// If not NULL, skipped_seqs_buf will be populated in-order with any ANSI escape
// sequences in msg prior to the returned offset value, allowing callers to
// replay those sequences to reach the correct formatting state at the offset 
// value in the original string. 
// NOTE: If skipped_seqs_buf_size <= strlen(msg), skipped_seqs_buf will NOT be
// populated. If NULL is passed for skipped_seqs_buf, skipped_seqs_buf_size
// should be 0.
static size_t calc_screen_offset(
        const_str msg, size_t rows, size_t cols,
        size_t *const n_visible_chars,
        char *const skipped_seqs_buf, size_t skipped_seqs_buf_size);
static int num_lines(char *msg, int cols);
static size_t strlen_on_screen(const char *msg);

static screen s_scr_home = {
    .type = SCREENTYPE_HOME,
    .display_text = {"Not Connected"},
    .id = SCREEN_ID_HOME,
    .scrlog = { .max_size_bytes = SCREENLOG_DEFAULT_MAX_BYTES },
    .ui_state = {
        .prompt = "> ",
        .inputbuf = {0}, 
        .i_inputbuf = 0,
        .scroll = 0,
        .scroll_at_top = true
    },
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
    return s_screen_id_counter++;
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

screen_ui_state *const screen_get_active_ui_state(void) {
    return &s_scr_active->ui_state;
}

// bool screen_set_active(screen_id scrid) {
//     // Look up scrid in array.
//     // If active, return true
//     // If doesn't exist, return false
//     // Update s_scr_active to point at screen in arr[scrid]
//     // HOW DO WE UPDATE THE UI?
// }

void screen_pushmsg_copy(screen_id scrid, const_str msg) {
    UNREFERENCED_PARAMETER(scrid);
    // TODO: look up the screen first!
    screenlog_push_copy(&s_scr_active->scrlog, msg); 
}

void screen_pushmsg_take(screen_id scrid, char *const msg) {
    UNREFERENCED_PARAMETER(scrid);
    // TODO: look up the screen first!
    screenlog_push_take(&s_scr_active->scrlog, msg);
}

int screen_fmt_to_buf(char *buf, size_t buflen, int buf_rows, int term_cols) {
    assert(buf != NULL);
    assert(s_scr_active != NULL);
    // TODO: do we actually want to assert rows/cols > 0?
    assert(buf_rows > 0);
    assert(term_cols > 0);

    screenlog_list list = s_scr_active->scrlog;
    if (list.head == NULL) {
        assert(list.tail == NULL);
        return 0;
    }

    screen_ui_state *const st = &s_scr_active->ui_state;
    assert(st != NULL);

    int rows_skipped = 0;
    size_t msg_offset_start = 0;
    screenlog_node *curr_node = list.tail;
    while (rows_skipped < st->scroll && curr_node != NULL) {
        rows_skipped += num_lines(curr_node->msg, term_cols);
        curr_node = curr_node->prev;
    }
    // Account for the partial message we'll write at the end, if one fits.
    int rows_used = rows_skipped - st->scroll;
    while (curr_node != NULL && rows_used < buf_rows) {
        assert(curr_node != NULL);
        assert(curr_node->msg != NULL);
        int msg_rows = num_lines(curr_node->msg, term_cols);

        if (rows_used + msg_rows <= buf_rows) {
            rows_used += msg_rows;
            // ONLY go to the previous message if there are rows left
            if (rows_used < buf_rows) curr_node = curr_node->prev;
            continue;
        }

        int rows_left = buf_rows - rows_used;
        // TODO: For newlines, you would start at 0 and count forward, checking
        // the string, and incrementing "rows_skipped" every 80 chars OR on a 
        // newline encountered (resetting chars counter) until "rows_skipped" is
        // equal to (msg_rows - rows_left). then you're left with proper offset.
        //msg_offset_start = term_cols * (msg_rows - rows_left);
        msg_offset_start = calc_screen_offset(
                    curr_node->msg, msg_rows - rows_left, term_cols,
                    NULL,
                    s_skipped_seqs_buf, sizeof(s_skipped_seqs_buf));
        rows_used += rows_left;
        assert(rows_used == buf_rows);
        // Keep curr_node in place because we start from there
    }

    if (curr_node == NULL) {
        curr_node = list.head;
        st->scroll_at_top = true;
        // If the window is widened, this brings down scroll accordingly
        st->scroll =
            rows_used + rows_skipped - (rows_skipped - st->scroll) - buf_rows;
    }
    else st->scroll_at_top = false;

    size_t i_buf = rows_used = 0;
    // First, write any ANSI sequences we skipped.
    // TODO: debug assert
    size_t seqbuf_len = strlen(s_skipped_seqs_buf);
    assert(seqbuf_len == 0 || seqbuf_len < msg_offset_start);
    while ((buf[i_buf] = s_skipped_seqs_buf[i_buf]) != '\0') i_buf++;
    s_skipped_seqs_buf[0] = '\0';

    // Then write the first msg from the offset.
    assert(msg_offset_start < strlen(curr_node->msg));
    char *msg = curr_node->msg;
    size_t i_msg = msg_offset_start;

    // We're going to replace newlines in msgs for now (so we can see if the
    // server sends many and if we need to keep them. Probably should.)
    // We also write newlines instead of null terms (except the final one) since
    // the buffer will be printed as one string.
    int rows_filled_in_buf = num_lines(&msg[i_msg], term_cols);
    while (msg[i_msg] != '\0')
        if ((buf[i_buf++] = msg[i_msg++]) == '\n') buf[i_buf - 1] = '_';
    buf[i_buf++] = '\n';
    curr_node = curr_node->next;
    while (curr_node != NULL && rows_filled_in_buf < buf_rows) {
        // We should never have a display buffer too small to hold the highest
        // possible number of characters that could appear on the screen.
        assert(i_buf + strlen(curr_node->msg) < buflen);
        msg = curr_node->msg;
        int msg_rows = num_lines(msg, term_cols);
        i_msg = 0;
        if (rows_filled_in_buf + msg_rows > buf_rows) {
            size_t n_visible_chars = 0;
            size_t msg_offset_end = calc_screen_offset(
                  msg, buf_rows - rows_filled_in_buf, term_cols,
                  &n_visible_chars, NULL, 0);

            while (i_msg < msg_offset_end && msg[i_msg] != '\0') 
                if ((buf[i_buf++] = msg[i_msg++]) == '\n') buf[i_buf - 1] = '_';

            buf[i_buf++] = '\0'; // This is the last message

            rows_filled_in_buf += 
                n_visible_chars / term_cols
                + (n_visible_chars % term_cols == 0 ? 0 : 1);
            assert(rows_filled_in_buf == buf_rows);
            continue;
        }

        while (msg[i_msg] != '\0') 
            if ((buf[i_buf++] = msg[i_msg++]) == '\n') buf[i_buf - 1] = '_';
        
        buf[i_buf++] = '\n';
        rows_filled_in_buf += msg_rows;
        curr_node = curr_node->next;
    }
    // We don't want the last newline; nothing will print there, but it will
    // take up a line in the screen buffer
    buf[i_buf - 1] = '\0';

    // Don't accrue scroll when it's not doing anything.
    if (rows_filled_in_buf < buf_rows) st->scroll = 0;

    return rows_filled_in_buf;
}



/*****************************************************************************/
/******************************* INTERNAL UTIL *******************************/

static int num_lines(char *msg, int cols) {
    size_t len = strlen_on_screen(msg);
    return len / cols + (len % cols == 0 ? 0 : 1);
}

// This impl has tons of asserts because it's difficult to really validate the
// ANSI escape sequences. Someone could send an unterminatd ANSI escape and blow
// the whole thing up, so I'm aggressively flagging those.
static size_t strlen_on_screen(const char *msg) {
    const_str logpfx = "strlen_on_screen()";
    size_t msglen = strlen(msg);
    size_t on_screen_len = 0;
    for (size_t i = 0; i < msglen; i++) {
        if (msg[i] == '\n')
            log_fmt(LOGLEVEL_ERROR, "[%s] Newline at index %zu.", logpfx, i);
        else if (msg[i] < ' ' && msg[i] != ESC[0]) {
//             log_fmt(LOGLEVEL_WARNING, "[%s] Invisible char at index %zu: (%d)\n"
//                     "Msg: '%s'",
//                     logpfx, i, (int)msg[i], msg);
            continue;
        }

        // Fast-forward through ANSI escapes
        while (msg[i] == ESC[0]) {
            while (msg[i++] != 'm' && i < msglen) {
                assert(msg[i] != '\0');
                assert(msg[i] != '\n');
                // Redundant, but the specificity of the above two is handy.
                if (msg[i] < ' ')
                    log_fmt(LOGLEVEL_ERROR,
                            "[%s] Invis char in ASCII seq at index %zu: (%d)\n"
                            "Msg: '%s'",
                            logpfx, i, (int)msg[i], msg);
                assert(i < msglen);
            }
        }
        if (msg[i] != '\0') on_screen_len++;
    }

    return on_screen_len;
}

static size_t calc_screen_offset(
        const_str msg, size_t rows, size_t cols,
        size_t *const n_visible_chars,
        char *skipped_seqs_buf, size_t skipped_seqs_buf_size)
{
    const_str logpfx = "calc_screen_offset()";
    assert(msg != NULL);
    size_t msglen = strlen(msg);
    assert(skipped_seqs_buf_size == 0 || skipped_seqs_buf_size > msglen);
    assert(msglen > rows * cols);

    if (skipped_seqs_buf_size < msglen) skipped_seqs_buf = NULL;

    // Offset is rows * cols plus all chars in each ANSI escape seq.
    size_t offset = rows * cols;
    size_t i_msg = 0, i_seqs = 0, vischars = 0;
    while (vischars < rows * cols) {
        assert(i_msg < msglen);
        assert(msg[i_msg] != '\0');
        if (msg[i_msg] == '\n')
            log_fmt(LOGLEVEL_ERROR, "[%s] NL at index %zu.", logpfx, i_msg);
        else if (msg[i_msg] < ' ' && msg[i_msg] != ESC[0]) {
//             log_fmt(LOGLEVEL_WARNING, "[%s] Invisible char at index %zu: (%d)\n"
//                     "Msg: '%s'",
//                     logpfx, i_msg, (int)msg[i_msg], msg);
            i_msg++;
            continue;
        }

        // If we reach the end of the string before finding enough printable 
        // chars, in theory it means the offset is 0 (whole string fits). In
        // practice, it probably means a string with an unterminated ANSI escape
        // sequence. Print it all so it can't hide.
        // (This is all assuming asserts are turned off).
        if (i_msg >= msglen) {
            if (skipped_seqs_buf != NULL) skipped_seqs_buf[0] = '\0';
            if (n_visible_chars != NULL) *n_visible_chars = vischars;
            return 0;
        }

        if (msg[i_msg] == ESC[0]) {
            while (msg[i_msg] != 'm') {
                offset++;
                if (skipped_seqs_buf != NULL)
                    skipped_seqs_buf[i_seqs++] = msg[i_msg];
                i_msg++;
                if (msg[i_msg] < ' ') 
                    log_fmt(LOGLEVEL_ERROR,
                            "[%s] Invis char in ASCII seq at index %zu: (%d)\n"
                            "Msg: '%s'",
                            logpfx, i_msg, (int)msg[i_msg], msg);
                assert(i_msg < msglen);
                // See earlier comment on this return case; this probably means
                // a bad string, so don't let it hide.
                if (i_msg >= msglen) {
                    if (n_visible_chars != NULL) *n_visible_chars = vischars;
                    if (skipped_seqs_buf != NULL) skipped_seqs_buf[0] = '\0';
                    return 0;
                }
            }
            if (skipped_seqs_buf != NULL) skipped_seqs_buf[i_seqs++] = 'm';
            offset++;
        } else vischars++; 
        i_msg++;
    }
    if (n_visible_chars != NULL) *n_visible_chars = vischars;
    if (skipped_seqs_buf != NULL) skipped_seqs_buf[i_seqs] = '\0';
    return offset;
}


