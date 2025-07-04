#include "screen_framework.h"

#include "log.h"
#include "stringutils.h"
#include "terminalutils.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h> // TODO: REMOVe WHEN DONE WITH UNREFERENCED_PARAMETER()

#define SCREEN_ID_HOME 0
#define N_SCRSLOTS 8
#define SCREEN_DISPLAY_TEXT_MAXLEN 1024

// ANSI escape character used to signal the beginning of a formatting sequence.
#define ESC "\033"

#define DEFAULT_PROMPT "> "

// This is just a string holder. Timestamp, author info, formatting, etc. should
// all be interpolated into the string already. 
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

typedef struct screen {
    char topic[SCREEN_DISPLAY_TEXT_MAXLEN];
    screenlog_list scrlog;
    screen_ui_state ui_state;
    char name[CHANNEL_NAME_MAXLEN];
    bool unread;
} screen;

typedef struct format_toggles {
    bool bold;
    bool italic;
    bool underline;
    bool striketh;
} format_toggles;


/************************ INTERNAL SCR MGMT **********************************/
static void internal__set_active(size_t i_scr);
static void internal__create_screen_at(size_t i_scr, const_str name);
static int internal__find_open_slot(void);
// Returns -1 if the name was not found.
static int internal__find_screen(const_str find_name);
static int internal__find_screen_startswith(const_str prefix);


/************************** BUF FMT UTILITIES ********************************/
static void format_toggles_clear(format_toggles *const toggles);
static bool is_digit(char c);

// Returns the offset index up to which the provided message could be printed
// and fit in the specified number of rows and columns.
// If provided, n_visible_chars will be populated with the actual number of
// visible characters that would be printed up to that offset (i.e., excluding
// characters that are part of ANSI escape sequences).
// If not NULL, replaybuf will be populated in-order with any ANSI escape
// sequences in msg prior to the returned offset value, allowing callers to
// replay those sequences to reach the correct formatting state at the offset 
// value in the original string. 
// NOTE: If replaybuf_size <= strlen(msg), replaybuf will NOT be populated.
static size_t calc_screen_offset(
        const_str msg, size_t rows, size_t cols,
        size_t *const n_visible_chars,
        char *const replaybuf, size_t replaybuf_size);

// Looks at the char in src at i_src and writes to buf at i_buf accordingly.
// If there's nothing special about src[i_src], this simply writes one char.
// If src[i_src] is an IRC format control character, this function will write
// the ANSI escape sequences to match the format specification.
// This function will modify the passed-in index pointers for both the buffer
// and the source string according to what it processes, so no incrementing by
// the caller is necessary.
// The format_toggles pointer is required to track the context, since many IRC
// formatting options work as toggles. All toggles should be set back to false
// after each message, in case the message did not explicitly toggle it off.
static size_t translate_src_char_to_buf(
        char *const buf, size_t *const i_buf, size_t bufsize,
        const char *const src, size_t *const i_src, size_t srclen,
        format_toggles *const togs);
        
static int num_lines(char *msg, int cols);
static size_t strlen_on_screen(const char *msg);




/********************* INTERNAL SCREENLOG API ********************************/
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


/***************************** STATIC VARS ***********************************/
static screen s_scr_home = {
    .topic = {"Not Connected"},
    .scrlog = { .max_size_bytes = SCREENLOG_DEFAULT_MAX_BYTES },
    .ui_state = {
        .prompt = "> ",
        .inputbuf = {0}, 
        .i_inputbuf = 0,
        .scroll = 0,
        .scroll_at_top = true
    },
    .name = "home",
    .unread = false
};

static screen* s_scrslots[N_SCRSLOTS] = { &s_scr_home };

static screen *s_scr_active = &s_scr_home;

// Used for capturing the skipped ANSI escape sequences for formatting when 
// printing a partial wrapped message so they can be replayed in order.
static char s_replaybuf[SCREENMSG_BUF_SIZE] = { 0 };


/*****************************************************************************/
/****************************** SCREENLOG IMPLs ******************************/

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
/***************************** PUB API IMPLs *********************************/
bool scrmgr_create_or_switch(const_str channel_name) {
    int i_scr = internal__find_screen(channel_name);
    assert(i_scr >= -1);
    assert(i_scr < N_SCRSLOTS);

    if (i_scr == -1) {
        i_scr = internal__find_open_slot();
        assert(i_scr >= -1);
        assert(i_scr < N_SCRSLOTS);

        // No available slots
        if (i_scr == -1) return false;

        internal__create_screen_at(i_scr, channel_name);
    }

    internal__set_active(i_scr);

    return true;
}

void scrmgr_deliver_copy(const_str deliver_to_name, const_str msg) {
    int i_scr = internal__find_screen(deliver_to_name);
    assert(i_scr >= -1);
    assert(i_scr < N_SCRSLOTS);

    screen *const deliver_scr = (i_scr != -1 ? s_scrslots[i_scr] : &s_scr_home);
    screenlog_push_copy(&deliver_scr->scrlog, msg);
    
    if (deliver_scr != s_scr_active) deliver_scr->unread = true;
}

screen_ui_state *const scrmgr_get_active_ui_state(void) {
    return &s_scr_active->ui_state;
}

const_str scrmgr_get_active_name(void) {
    return s_scr_active->name;
}

bool scrmgr_show_index(int i_scr) {
    if (i_scr < 0 || i_scr >= N_SCRSLOTS) {
        log_fmt(LOGLEVEL_ERROR, "[scrmgr_show_index] Invalid index: %d", i_scr);
        return false;
    }
    if (s_scrslots[i_scr] == NULL) {
        log_fmt(LOGLEVEL_WARNING, "[scrmgr_show_index] Tried to show slot %d "
                "but there is no screen there.", i_scr);
        return false;
    }

    internal__set_active(i_scr);
    return true;
}

bool scrmgr_show_name(const_str scr_name) {
    assert(scr_name != NULL);

    int i_scr = internal__find_screen(scr_name);
    if (i_scr < 0) {
        log_fmt(LOGLEVEL_WARNING,
                "[scrmgr_show_name] No screen with name '%s'.", scr_name);
        return false;
    }
    assert(i_scr < N_SCRSLOTS);

    return scrmgr_show_index(i_scr);
}

bool scrmgr_show_name_startswith(const_str prefix) {
    assert(prefix != NULL);

    int i_scr = internal__find_screen_startswith(prefix);
    if (i_scr < 0) {
        log_fmt(LOGLEVEL_WARNING, "[scrmgr_show_name] No screen with name that "
                "starts with '%s'.", prefix);
        return false;
    }
    assert(i_scr < N_SCRSLOTS);

    return scrmgr_show_index(i_scr);
}

bool scrmgr_set_topic(const_str scr_name, const_str topic) {
    int i_scr = internal__find_screen(scr_name);
    if (i_scr < 0) {
        log_fmt(LOGLEVEL_WARNING,
                "[scrmgr_set_topic] No screen with name '%s'", scr_name);
        return false;
    }
    assert(i_scr < N_SCRSLOTS);

    screen *scr = s_scrslots[i_scr];
    return strcpy_s(scr->topic, sizeof(scr->topic), topic) == 0;
}

/*****************************************************************************/
/*********************** INTERNAL SCR MGMT IMPLs *****************************/

static void internal__set_active(size_t i_scr) {
    s_scr_active = s_scrslots[i_scr];
    s_scr_active->unread = false;
}

static void internal__create_screen_at(size_t i_scr, const_str name) {
    assert(i_scr < N_SCRSLOTS);
    assert(s_scrslots[i_scr] == NULL);

    // TODO: check memory
    screen *new_screen = (screen *) calloc(1, sizeof(screen));
    strcpy_s(new_screen->name, sizeof(new_screen->name), name);
    new_screen->scrlog.max_size_bytes = SCREENLOG_DEFAULT_MAX_BYTES;
    new_screen->ui_state.prompt = DEFAULT_PROMPT;
    new_screen->unread = false;

    s_scrslots[i_scr] = new_screen;
}

static int internal__find_open_slot(void) {
    for (int i = 0; i < N_SCRSLOTS; i++)
        if ((s_scrslots[i]) == NULL) return i;
    return -1;
}

static int internal__find_screen(const_str find_name) {
    int i_found = -1, i_scr = 0;
    while (i_scr < N_SCRSLOTS && i_found == -1) {
        if (s_scrslots[i_scr] == NULL) {
            assert(s_scrslots[i_scr]->name != NULL);
            i_scr++;
            continue;
        }
        if (strut_strcmpi(s_scrslots[i_scr]->name, find_name) == 0)
            i_found = i_scr;

        i_scr++;
    }
    return i_found;
}

static int internal__find_screen_startswith(const_str prefix) {
    int i_found = -1, i_scr = 0;
    while (i_scr < N_SCRSLOTS && i_found == -1) {
        if (s_scrslots[i_scr] == NULL) {
            assert(s_scrslots[i_scr]->name != NULL);
            i_scr++;
            continue;
        }
        if (strut_startswithi(s_scrslots[i_scr]->name, prefix))
            i_found = i_scr;

        i_scr++;
    }
    return i_found;
}

/*****************************************************************************/
/***************************** PUB FMT API ***********************************/

int screen_fmt_header(char *buf, size_t bufsize, int term_cols) {
    UNREFERENCED_PARAMETER(term_cols);

    const_str topic = s_scr_active->topic;
    size_t n_vis_chars = strlen_on_screen(topic);
    // TODO: instead, take offset_end return and only draw to there
//     calc_screen_offset(topic, 1, term_cols, &n_vis_chars, NULL, 0);


    format_toggles toggles = { 0 };
    size_t topic_len = strlen(topic);
    size_t i_topic = 0, i_buf = 0;
    while (topic[i_topic] != '\0')
        translate_src_char_to_buf(
              buf, &i_buf, bufsize, topic, &i_topic, topic_len, &toggles);
    buf[i_buf] = '\0';

    return n_vis_chars;
}

int screen_fmt_tabs(char *buf, size_t bufsize, int term_cols) {
    UNREFERENCED_PARAMETER(term_cols);
    // TODO: change formatting type for very small values of term_cols, down to
    // just returning an empty string.
    
    // TODO: Calculate this based on term_cols. Can be step-function
    size_t abbrev_over = 10;
    size_t i_cutoff = abbrev_over - 3; // -1 to index, -2 for periods..
    
    size_t i_scr = 0, i_buf = 0;
    while (s_scrslots[i_scr] != NULL) {
        screen *const scr = s_scrslots[i_scr];
        if (strlen(scr->name) <= abbrev_over) {
            if (scr->unread)
                i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
                      "\033[1m %zu %s \033[22m|", i_scr, scr->name);
            else if (scr == s_scr_active)
                // TODO: parameterize statline bg color...
                i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
                      "\033[42m %zu %s \033[48;5;252m|", i_scr, scr->name);
            else
                i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
                      " %zu %s |", i_scr, scr->name);
        }
        else {
            char tmp = scr->name[i_cutoff];
            scr->name[i_cutoff] = '\0';
            if (scr->unread)
                i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
                      "\033[1m %zu %s..\033[22m |", i_scr, scr->name);
            else if (scr == s_scr_active)
                // TODO: parameterize statline bg color...
                i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
                      "\033[42m %zu %s.. \033[48;5;252m|", i_scr, scr->name);
            else
                i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
                      " %zu %s.. |", i_scr, scr->name);
            scr->name[i_cutoff] = tmp;
        }

        i_scr++;
    }

    return i_buf;
}

int screen_fmt_to_buf(char *buf, size_t bufsize, int buf_rows, int term_cols) {
    assert(buf != NULL);
    assert(s_scr_active != NULL);
    assert(buf_rows > 0);
    assert(term_cols > 0);

    screenlog_list list = s_scr_active->scrlog;
    if (list.head == NULL) {
        assert(list.tail == NULL);
        buf[0] = '\0';
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
        msg_offset_start = calc_screen_offset(
                    curr_node->msg, msg_rows - rows_left, term_cols,
                    NULL,
                    s_replaybuf, sizeof(s_replaybuf));
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


    // First, write any ANSI or IRC format sequences we skipped.
    format_toggles toggles = { 0 };
    size_t i_buf = 0, i_rebuf = 0;
    size_t rebuf_len = strlen(s_replaybuf);
    assert(rebuf_len == 0 || rebuf_len < msg_offset_start);
    while (s_replaybuf[i_rebuf] != '\0')
        translate_src_char_to_buf(
              buf, &i_buf, bufsize, s_replaybuf, &i_rebuf, rebuf_len, &toggles);
    s_replaybuf[0] = '\0';


    // Then write the first msg from the offset.
    // We're going to replace newlines in msgs for now (so we can see if the
    // server sends many and if we need to keep them. Probably should.)
    // We also write newlines instead of null terms (except the final one) since
    // the buffer will be printed as one string.
    char *msg = curr_node->msg;
    size_t i_msg = msg_offset_start;
    size_t msglen = strlen(msg);
    assert(i_msg < msglen);
    int rows_filled_in_buf = num_lines(&msg[i_msg], term_cols);
    while (msg[i_msg] != '\0')
        translate_src_char_to_buf(
                buf, &i_buf, bufsize, msg, &i_msg, msglen, &toggles);
    i_buf += termutils_reset_all_buf(buf + i_buf, bufsize - (i_buf + 1));
    buf[i_buf++] = '\n';

    curr_node = curr_node->next;
    while (curr_node != NULL && rows_filled_in_buf < buf_rows) {
        // We should never have a display buffer too small to hold the highest
        // possible number of characters that could appear on the screen.
        msg = curr_node->msg;
        msglen = strlen(msg);
        assert(i_buf + msglen < bufsize);
        int msg_rows = num_lines(msg, term_cols);
        i_msg = 0;
        format_toggles_clear(&toggles);
        if (rows_filled_in_buf + msg_rows > buf_rows) {
            size_t n_visible_chars = 0;
            size_t msg_offset_end = calc_screen_offset(
                  msg, buf_rows - rows_filled_in_buf, term_cols,
                  &n_visible_chars, NULL, 0);

            while (i_msg < msg_offset_end && msg[i_msg] != '\0') 
                translate_src_char_to_buf(
                        buf, &i_buf, bufsize, msg, &i_msg, msglen, &toggles);

            buf[i_buf++] = '\0'; // This is the last message

            rows_filled_in_buf += 
                n_visible_chars / term_cols
                + (n_visible_chars % term_cols == 0 ? 0 : 1);
            assert(rows_filled_in_buf == buf_rows);
            continue;
        }
        while (msg[i_msg] != '\0')
            translate_src_char_to_buf(
                    buf, &i_buf, bufsize, msg, &i_msg, msglen, &toggles);

        i_buf += termutils_reset_all_buf(buf + i_buf, bufsize - (i_buf + 1));
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

size_t screen_DEBUG_char_details_buf(
        char *buf, const_str msg, size_t bufsize,
        size_t entries_per_row,
        size_t offset_calc_rows, size_t offset_calc_cols)
{
    size_t i_buf = 0;
    unsigned char c;
    for (size_t i_msg = 0; i_msg < strlen(msg); i_msg++) {
        if (i_msg > 0 && i_msg % entries_per_row == 0)
            i_buf += sprintf_s(buf + i_buf, bufsize - i_buf, "\n");

        c = msg[i_msg];
        if (c < ' ') {
            i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
                    "\033[38;5;246m%03zu: \033[38;5;242m%02x~ ",
                    i_msg, c);
        } else {
            i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
                 "\033[38;5;246m%03zu:\033[1;31m%c\033[22m\033[38;5;242m%02x  ",
                 i_msg, c, c);
        }
    }

    size_t visch = 0;
    char rebuf[1000];
    size_t offset =  calc_screen_offset(msg, offset_calc_rows, offset_calc_cols,
            &visch, rebuf, sizeof(rebuf));

    i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
           "\n\033[33m(%zux%zu) offset=%zu, visch=%zu, rebuflen=%zu\n\033[0m",
           offset_calc_rows, offset_calc_cols, offset, visch, strlen(rebuf));

    size_t irb = 0;
    while (rebuf[irb] != '\0') {
        if (irb > 0 && irb % entries_per_row == 0)
            i_buf += sprintf_s(buf + i_buf, bufsize - i_buf, "\n");

        c = rebuf[irb];
        if (c < ' ') {
            i_buf += sprintf_s( buf + i_buf, bufsize - i_buf,
                    "\033[38;5;246m%03zu: \033[38;5;242m%02x~ ",
                    irb, c);
        } else {
            i_buf += sprintf_s(buf + i_buf, bufsize - i_buf,
                 "\033[38;5;246m%03zu:\033[1;31m%c\033[22m\033[38;5;242m%02x  ",
                 irb, c, c);
        }
        irb++;
    }

    i_buf += sprintf_s(buf + i_buf, bufsize - i_buf, "\033[0m\n");
    return i_buf;
}

/*****************************************************************************/
/*************************** INTERNAL FMT UTIL *******************************/

#define IRC_FMT_BOLD 0x02
#define IRC_FMT_ITALIC 0x1D
#define IRC_FMT_UNDERLINE 0x1F
#define IRC_FMT_STRIKETH 0x1E
#define IRC_FMT_RESET 0x0F
#define IRC_FMT_COLOR 0x03
#define IRC_FMT_COLOR_HEX 0x04
#define IRC_FMT_COLOR_REV 0x16

const unsigned int irc_to_ansi256_color[] = {
    [0] =  7, [1] =232, [2] =  4, [3] =  2, [4] =  1, [5] = 94, [6] = 93,
    [7] =214, [8] =  3, [9] = 82, [10]=  6, [11]= 51, [12]= 33, [13]=201, 
    [14]=243, [15]=253
};

const char irc_ctrl_chars[] = {
    IRC_FMT_BOLD, IRC_FMT_ITALIC, IRC_FMT_UNDERLINE, IRC_FMT_STRIKETH,
    IRC_FMT_RESET, IRC_FMT_COLOR, IRC_FMT_COLOR_HEX, IRC_FMT_COLOR_REV
};

static void format_toggles_clear(format_toggles *const toggles) {
    toggles->bold = false;
    toggles->italic = false;
    toggles->underline = false;
    toggles->striketh = false;
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static size_t translate_src_char_to_buf(
        char *const buf, size_t *const i_buf, size_t bufsize,
        const char *const src, size_t *const i_src, size_t srclen,
        format_toggles *const togs)
{
    assert(i_buf != NULL);
    assert(buf != NULL);
    assert(i_src != NULL);
    assert(src != NULL);
    assert(togs != NULL);
    assert(bufsize > *i_buf + 1);
    assert(srclen > *i_src);

    size_t n_written = 0;
    size_t n_read = 1;
    size_t maxlen = bufsize - (*i_buf + 1);
    char *wbuf = buf + *i_buf;
    switch(src[*i_src]) {
    case IRC_FMT_COLOR:
        size_t max_inc = srclen - (*i_src + 1), curr_inc = 1;
        int color1 = -1, color2 = -1;
        do {
            char c = src[*i_src + curr_inc];
            if (is_digit(c)) {
                color1 = (int)c - (int)'0';
                n_read++;
            }
            else break;

            if (++curr_inc > max_inc) break;

            c = src[*i_src + curr_inc];
            if (is_digit(c)) {
                color1 *= 10;
                color1 += (int)c - (int)'0';
                n_read++;
                c = src[*i_src + ++curr_inc];
            }
            if (c != ',' || ++curr_inc > max_inc) break;

            c = src[*i_src + curr_inc];
            if (is_digit(c)) {
                color2 = (int)c - (int)'0';
                n_read += 2; // Include the comma if it was a delimiter
            }
            else break;

            if (++curr_inc > max_inc) break;

            c = src[*i_src + curr_inc];
            if (is_digit(c)) {
                color2 *= 10;
                color2 += (int)c - (int)'0';
                n_read++;
            }
        } while (0);
        assert(color1 < 100);
        assert(color2 < 100);

        // TODO: fix this when the full ANSI color map array is filled out
        if (color1 < 0) {
            n_written = termutils_reset_text_color_buf(wbuf, maxlen);
            n_written += termutils_reset_bg_color_buf(
                    wbuf + n_written, maxlen - n_written);
        }
        else {
            int ansi256 = color1 > 15 ? 141 : irc_to_ansi256_color[color1];
            assert(ansi256 < 256);
            n_written = termutils_set_text_color_256_buf(wbuf, maxlen, ansi256);
            if (color2 >= 0) {
                ansi256 = color2 > 15 ? 82 : irc_to_ansi256_color[color2];
                assert(ansi256 < 256);
                n_written += termutils_set_bg_color_256_buf(
                        wbuf + n_written, maxlen - n_written, ansi256);
            }
        }
        break;
    case IRC_FMT_BOLD:
        togs->bold = !togs->bold;
        n_written = termutils_set_bold_buf(wbuf, maxlen, togs->bold);
        break;
    case IRC_FMT_ITALIC:
        togs->italic = !togs->italic;
        n_written = termutils_set_italic_buf(wbuf, maxlen, togs->italic);
        break;
    case IRC_FMT_UNDERLINE:
        togs->underline = !togs->underline;
        n_written = termutils_set_underline_buf(wbuf, maxlen, togs->underline);
        break;
    case IRC_FMT_STRIKETH:
        togs->striketh = !togs->striketh;
        n_written = termutils_set_striketh_buf(wbuf, maxlen, togs->striketh);
        break;
    case IRC_FMT_RESET:
        togs->bold = togs->italic = togs->underline = togs->striketh = false;
        n_written = termutils_reset_all_buf(wbuf, maxlen);
        break;
    default:
        buf[*i_buf] = src[*i_src];
        if (buf[*i_buf] == '\n') buf[*i_buf] = '_';
        n_written = 1;
    }
    assert(n_written > 0);
    *i_buf += n_written;
    *i_src += n_read;
    assert(n_written < maxlen);
    assert(*i_src <= srclen);

    return n_written;
}

static int num_lines(char *msg, int cols) {
    size_t len = strlen_on_screen(msg);
    return len / cols + (len % cols == 0 ? 0 : 1);
}

// The asserts are aggressive here to catch any unterminated ANSI esc sequences.
static size_t strlen_on_screen(const char *msg) {
    const_str logpfx = "strlen_on_screen()";
    size_t msglen = strlen(msg);
    size_t on_screen_len = 0;
    for (size_t i = 0; i < msglen; i++) {
        // Shouldn't see \r\n here. If it's a delimiter, the parser should have
        // excluded it; if it's in the middle of the message, it should have
        // repalced it in-line.
        assert(msg[i] != '\r');
        assert(msg[i] != '\n');
        assert(msg[i] != '\0');

        // We expect control characters (other than NUL, CR, LF) because they're
        // used for message formatting within IRC messages.
        if (msg[i] < ' ' && msg[i] != ESC[0] && msg[i] != IRC_FMT_COLOR)
            continue;

        // Fast-forward through ANSI escapes
        while (msg[i] == ESC[0]) {
            while (msg[i++] != 'm' && i < msglen) {
                if (msg[i] < ' ') 
                    log_fmt(LOGLEVEL_ERROR,
                            "[%s] Ctrl char in ASCII seq at index %zu: (%d)\n"
                            "Msg: '%s'",
                            logpfx, i, (int)msg[i], msg);

                // Within an ANSI escape, we do not expect control characters.
                assert(msg[i] >= ' ');
            }
        }
        // Fast-forward through IRC color formatting
        while (msg[i] == IRC_FMT_COLOR && msg[++i] != '\0') {
            if (is_digit(msg[i])) i++;
            else continue;
            if (is_digit(msg[i])) i++;

            if (msg[i] == ',') i++;
            else continue;

            if (is_digit(msg[i])) i++;
            else {
                i--; // Un-count the comma if it wasn't a delim
                continue;
            }
            if (is_digit(msg[i])) i++;
        }
        assert(msg[i] != '\r');
        assert(msg[i] != '\n');
        if (msg[i] >= ' ') on_screen_len++;
    }

    return on_screen_len;
}

static size_t calc_screen_offset(
        const_str msg, size_t rows, size_t cols,
        size_t *const n_visible_chars,
        char *replaybuf, size_t replaybuf_size)
{
    const_str logpfx = "calc_screen_offset()";
    assert(msg != NULL);
    size_t msglen = strlen(msg);
    assert(replaybuf_size == 0 || replaybuf_size > msglen);
    assert(msglen > rows * cols);

    if (replaybuf_size < msglen) replaybuf = NULL;

    // Offset is rows * cols plus all chars in each ANSI escape seq.
    size_t offset = rows * cols;
    size_t i_msg = 0, i_rebuf = 0, vischars = 0;
    while (vischars < rows * cols) {
        // If we reach the end of the message without finding enough visible
        // chars, then either this function or its caller messed up some math.
        // Or there's an unterminated ANSI escape sequence. All crashworthy.
//         assert(i_msg < msglen);
        
        // If we're not crashing from this, count the last char and tell the 
        // caller it all should fit.
        if (i_msg >= msglen) {
            if (replaybuf != NULL) replaybuf[0] = '\0';
            if ((unsigned char) msg[i_msg] >= ' ') vischars++;
            if (n_visible_chars != NULL) *n_visible_chars = vischars;
            return 0;
        }

        // Shouldn't see \r\n here. If it's a delimiter, the parser should have
        // excluded it; if it's in the middle of the message, it should have
        // repalced it in-line.
        assert(msg[i_msg] != '\r');
        assert(msg[i_msg] != '\n');
        assert(msg[i_msg] != '\0');

        if (msg[i_msg] == ESC[0]) {
            while (msg[i_msg] != 'm') {
                offset++;
                if (replaybuf != NULL)
                    replaybuf[i_rebuf++] = msg[i_msg];
                i_msg++;

                if ((unsigned char) msg[i_msg] < ' ') 
                    log_fmt(LOGLEVEL_ERROR,
                            "[%s] Ctrl char in ASCII seq at index %zu: (%d)\n"
                            "Msg: '%s'",
                            logpfx, i_msg, (int)msg[i_msg], msg);

                // Within an ANSI escape, we do not expect control characters.
                assert((unsigned char) msg[i_msg] >= ' ');

                if (i_msg >= msglen) {
                    log_fmt(LOGLEVEL_ERROR, "[%s] Reached end of string while "
                            "searching for ANSI escape terminator.\n"
                            "vischars=%zu, i_msg=%zu, msglen=%zu\n"
                            "Str: '%s'", logpfx, vischars, i_msg, msglen, msg);

                    assert(i_msg < msglen);

                    if (n_visible_chars != NULL) *n_visible_chars = vischars;
                    if (replaybuf != NULL) replaybuf[0] = '\0';
                    return 0;
                }
            }
            if (replaybuf != NULL) replaybuf[i_rebuf++] = 'm';
            offset++;
        } 
        else if (msg[i_msg] == IRC_FMT_COLOR) {
            // Advance i_search to the last index of the color sequence.
            size_t i_search = i_msg;
            if (is_digit(msg[i_search + 1])) {
                i_search++;
                if (is_digit(msg[i_search + 1])) i_search++;

                // Comma is only a delim if the next char is a digit.
                if (msg[i_search + 1] == ',' && is_digit(msg[i_search + 2]))
                    i_search += is_digit(msg[i_search + 3]) ? 3 : 2;
            }
            offset += i_search - i_msg + 1;

            assert(replaybuf == NULL ||
                    i_rebuf + (i_search - i_msg) < replaybuf_size);
            if (replaybuf != NULL) replaybuf[i_rebuf++] = msg[i_msg];
            while (i_msg < i_search) {
                i_msg++;
                if (replaybuf != NULL) replaybuf[i_rebuf++] = msg[i_msg];
            }
        }
        else if (strchr(irc_ctrl_chars, msg[i_msg]) != NULL) {
            // TODO: handle 0x04 for HEX color...
            if (replaybuf != NULL) replaybuf[i_rebuf++] = msg[i_msg];
            offset++;
        }
        else {
            // Unsigned is critical here; extended ASCII chars are common and
            // evaluate negative as signed chars, but they are printable!
            unsigned char c = msg[i_msg];
            if (c >= ' ') {
                vischars++;
            } else {
                offset++;
            }
        }
        i_msg++;
    }
    if (n_visible_chars != NULL) *n_visible_chars = vischars;
    if (replaybuf != NULL) replaybuf[i_rebuf] = '\0';
    return offset;
}

