#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

#define ESC "\033"

typedef struct chlognode {
    char *msg;
    struct chlognode *next;
    struct chlognode *prev;
} chlognode;

typedef struct chloglist {
    chlognode *head;
    chlognode *tail;
} chloglist;

DWORD WINAPI ui_main(LPVOID data);
void push_chlog_msg(chloglist *list, char *msg);

// Finds the offset index into "msg" that would result in skipping "rows" number
// of lines in a "cols" width buffer when printed onto the terminal screen 
// (e.g., ignores ANSI escape sequences for color/format).
// WARNING: Do not pass a string with non-printable chars (including newline) or
// ANSI escape sequences other than color/format (i.e., beginning with 'ESC' and
// ending with 'm').
size_t calc_screen_offset(const char *msg, size_t rows, size_t cols);

// Returns the number of on-screen columns the message will occupy (e.g., 
// ignores ANSI escape sequences for color/format). 
// WARNING: Do not pass a string with non-printable chars (including newline) or
// ANSI escape sequences other than color/format (i.e., beginning with 'ESC' and
// ending with 'm').
size_t strlen_on_screen(const char *msg);

// Returns the number of lines that "msg" will take up if printed to a buffer of
// "cols" width.
// WARNING: Does not account for newlines.
int num_lines(char *msg, int cols);

int fill_buffer(char *buf, size_t buflen, chloglist *list,
                 int buf_rows, int term_cols, int scroll);

chloglist g_chloglist = {0};
char g_screenbuf[16 * 1024] = {0};
bool g_scroll_at_top = true;


int main(void) {
    // Use alternative screen buffer
    printf("\033[?1049h");

    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h_stdin == INVALID_HANDLE_VALUE || h_stdout == INVALID_HANDLE_VALUE) {
        printf("Invalid std handle\n");
        return 23;
    }

    DWORD prev_mode;
    if (!GetConsoleMode(h_stdin, &prev_mode)) {
        printf("GetConsoleMode() failed\n");
        return 23;
    }

    // Quick Edit mode seems to interfere with mouse input; before diabling it,
    // scroll events were coming in as key events for up/down arrow.
    // Note that ENABLE_EXTENDED_FLAGS must be enabled to disable Quick Edit.
	DWORD new_mode = (ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS) &
                     ~(ENABLE_QUICK_EDIT_MODE);
    if (!SetConsoleMode(h_stdin, new_mode)) {
        printf("SetConsoleMode failed\n");
        return 23;
    }

    // This doesn't really need to be a thread but it will be in the IRC client
    DWORD thread_id = 0;
    HANDLE ui_thread = CreateThread(NULL, 0, ui_main, NULL, 0, &thread_id);
    WaitForSingleObject(ui_thread, INFINITE);
    CloseHandle(ui_thread);

    // Return from alternative screen buffer
    printf("\033[?1049l");

    if (!SetConsoleMode(h_stdin, prev_mode))
        printf("SetConsoleMode(): Failed to reset to old console mode.\n");

    return 0;
}

DWORD WINAPI ui_main(LPVOID data) {
    UNREFERENCED_PARAMETER(data);

    const char *prompt = "> ";
    char uibuf[50];
    char statbuf[1024]; // idk, there are some massive monitors
    size_t i_uibuf = 0;
    bool user_quit = false;

    int scroll = 0;
    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (h_stdin == INVALID_HANDLE_VALUE || h_stdout == INVALID_HANDLE_VALUE) {
        printf("Invalid std handle\n");
        return 23;
    }

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h_stdout, &csbi);

    int term_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int term_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int rows_filled_in_buf = 0;
    
    INPUT_RECORD irbuf[128];
    DWORD events = 0;
    while (!user_quit) {
        ReadConsoleInput(h_stdin, irbuf, 128, &events);
        for (DWORD i = 0; i < events; i++) {
            if (irbuf[i].EventType == MOUSE_EVENT) {
                MOUSE_EVENT_RECORD m = irbuf[i].Event.MouseEvent;
                // Process the mouse event
                if (!(m.dwEventFlags & MOUSE_WHEELED)) continue;

                bool is_hiword_negative = HIWORD(m.dwButtonState) & 1<<15;

                if (is_hiword_negative) {
                    scroll--;
                    if (scroll < 0) scroll = 0;
                } else {
                    if (!g_scroll_at_top) scroll++;
                }

                continue;
            }

            if (irbuf[i].EventType != KEY_EVENT) continue;

            KEY_EVENT_RECORD k = irbuf[i].Event.KeyEvent;
            if (!k.bKeyDown) continue;
            if (k.wVirtualKeyCode == VK_BACK && i_uibuf > 0)
                uibuf[--i_uibuf] = '\0';
            if (k.wVirtualKeyCode == VK_ESCAPE)
                user_quit = true; // don't break; we want to reset the colors
            if (k.wVirtualKeyCode == VK_RETURN && i_uibuf > 0) {
                push_chlog_msg(&g_chloglist, uibuf);

                while (i_uibuf > 0) uibuf[i_uibuf--] = '\0';
                assert(i_uibuf == 0);
                uibuf[i_uibuf] = '\0';
            }
            else if (k.uChar.AsciiChar > 31 && i_uibuf < sizeof(uibuf) - 1)
                uibuf[i_uibuf++] = k.uChar.AsciiChar;
        }
        GetConsoleScreenBufferInfo(h_stdout, &csbi);

        term_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        term_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;

        int stats_len = sprintf_s(statbuf, sizeof(statbuf),
                "%dx%d  scr:%d",
                term_rows, term_cols, scroll);

        int rows_statline = stats_len / term_cols + 1;
        int rows_uiline = (strlen(prompt) + i_uibuf - 1) / term_cols + 1;
        int rows_screenbuf = term_rows - (rows_statline + rows_uiline);

        rows_filled_in_buf = fill_buffer(g_screenbuf, sizeof(g_screenbuf),
                &g_chloglist, rows_screenbuf, term_cols, scroll);

        // Don't accrue scroll when it's not doing anything.
        if (rows_filled_in_buf < rows_screenbuf) scroll = 0;

        printf("\033 7" // Save cursor position
                "\033[0m\033[2J" // Reset colors, erase screen
                "\033[H\033[48;5;252m\033[2K" // Draw statline bg, lgray, top 
                "\033[38;5;233m%s" // Draw statline text, dark gray
                "\033[0m\033[1E%s" // Reset color, print buffer next line
                "\033[%d;0H\033[48;5;27m\033[2K" // Draw input line blue bg
                "\033[38;5;190m%s" // Draw prompt, yellow
                "\033[38;5;15m%s" // Draw uibuf, white
                "\033[0m" // Reset colors
                "\033 8", // Restore cursor position
                statbuf, g_screenbuf, term_rows - (rows_uiline - 1),
                prompt, uibuf);
    }
    printf("\033[0m");
    printf("\033[2J");

    return 0;
}

void push_chlog_msg(chloglist *list, char *msg) {
    assert(list != NULL);


    chlognode *newnode = (chlognode *) malloc(sizeof(chlognode));

    rsize_t msg_size = strlen(msg) + 1;
    newnode->msg = (char *) malloc(msg_size);
    newnode->prev = newnode->next = NULL;
    strcpy_s(newnode->msg, msg_size, msg);
    if (list->head == NULL) {
        assert(list->tail == NULL);
        list->head = newnode;
        list->tail = newnode;
        return;
    }

    list->tail->next = newnode;
    newnode->prev = list->tail;
    list->tail = newnode;
}

int fill_buffer(
        char *buf, 
        size_t buflen, 
        chloglist *list,
        int buf_rows, 
        int term_cols,
        int scroll)
{
    assert(list != NULL);
    if (list->head == NULL) {
        assert(list->tail == NULL);
        return 0;
    }

    int rows_skipped = 0;
    size_t msg_offset_start = 0;
    chlognode *curr_node = list->tail;
    while (rows_skipped < scroll && curr_node != NULL) {
        rows_skipped += num_lines(curr_node->msg, term_cols);
        curr_node = curr_node->prev;
    }
    // Account for the partial message we'll write at the end, if one fits.
    int rows_used = rows_skipped - scroll;
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
        msg_offset_start = 
            calc_screen_offset(curr_node->msg, msg_rows - rows_left, term_cols);
        rows_used += rows_left;
        assert(rows_used == buf_rows);
        // Keep curr_node in place because we start from there
    }

    // I know it's weird. Need the second condition for g_scroll_at_top and this
    // is cleaner than an extra if block.
    if (curr_node == NULL || curr_node == list->head) {
        curr_node = list->head;
        g_scroll_at_top = true;
    }
    else g_scroll_at_top = false;

    size_t i_buf = rows_used = 0;
    // Start by writing only from the offset.
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
            //size_t msg_offset_end = term_cols * (buf_rows - rows_filled_in_buf);
            size_t msg_offset_end = calc_screen_offset(
                    msg, buf_rows - rows_filled_in_buf, term_cols);

            while (i_msg < msg_offset_end && msg[i_msg] != '\0') 
                if ((buf[i_buf++] = msg[i_msg++]) == '\n') buf[i_buf - 1] = '_';

            buf[i_buf++] = '\0'; // This is the last message

            rows_filled_in_buf += 
                i_msg / term_cols + (i_msg % term_cols == 0 ? 0 : 1);
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
    return rows_filled_in_buf;
}

int num_lines(char *msg, int cols) {
    size_t len = strlen_on_screen(msg);
    return len / cols + (len % cols == 0 ? 0 : 1);
}

size_t calc_screen_offset(const char *msg, size_t rows, size_t cols) {
    size_t msglen = strlen(msg);

    assert(msg != NULL);
    assert(msglen > rows * cols);

    // Offset is rows * cols plus all chars in each ANSI escape seq.
    size_t offset = rows * cols;
    size_t i = 0, printchars = 0;
    while (printchars < rows * cols) {
        assert(i < msglen);
        assert(msg[i] != '\0');
        assert(msg[i] != '\n');

        // If we reach the end of the string before finding enough printable 
        // chars, in theory it means the offset is 0 (whole string fits). In
        // practice, it probably means a string with an unterminated ANSI escape
        // sequence. Print it all so it can't hide.
        // (This is all assuming asserts are turned off).
        if (i >= msglen) return 0;

        // Redundant, but the specificity of the earlier asserts is handy.
        assert(msg[i] >= ' ' || msg[i] == ESC[0]);
        if (msg[i] == ESC[0]) {
            while (msg[i] != 'm') {
                offset++;
                i++;
                assert(msg[i] >= ' ');
                assert(i < msglen);
                // See earlier comment on this return case; this probably means
                // a bad string, so don't let it hide.
                if (i >= msglen) return 0;
            }
            offset++;
        } else printchars++; 
        i++;
    }
    return offset;
}

// This impl has tons of asserts because it's difficult to really validate the
// ANSI escape sequences. Someone could send an unterminatd ANSI escape and blow
// the whole thing up, so I'm aggressively flagging those.
size_t strlen_on_screen(const char *msg) {
    size_t msglen = strlen(msg);
    size_t on_screen_len = 0;
    for (size_t i = 0; i < msglen; i++) {
        assert(msg[i] != '\n');
        // Redundant, but the specificity of the newline assert is handy.
        assert(msg[i] >= ' ' || msg[i] == ESC[0]);

        // Fast-forward through ANSI escapes
        if (msg[i] == ESC[0]) {
            while (msg[i++] != 'm' && i < msglen) {
                assert(msg[i] != '\0');
                assert(msg[i] != '\n');
                // Redundant, but the specificity of the above two is handy.
                assert(msg[i] >= ' ');
                assert(i < msglen);
            }
        }
        if (msg[i] != '\0') on_screen_len++;
    }

    return on_screen_len;
}

