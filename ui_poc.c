#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

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
size_t fill_buffer(char *buf, size_t buflen, chloglist *list,
                 size_t rows, size_t cols, size_t scroll);
int num_lines(char *msg, int cols);


int quit = false;
chloglist g_chloglist = {0};
char g_screenbuf[16 * 1024] = {0};
bool g_scroll_at_top = true;


int main() {
    printf("\033[?1049h");

    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h_stdin, &mode);
    mode |= (ENABLE_EXTENDED_FLAGS  
        | ENABLE_WINDOW_INPUT  
        | ENABLE_MOUSE_INPUT 
        | ENABLE_PROCESSED_INPUT);
    if (!SetConsoleMode(h_stdin, mode)) {
        printf("SetConsoleMode failed\n");
        return -1;
    }

    DWORD thread_id = -1;
    HANDLE ui_thread = CreateThread(NULL, 0, ui_main, NULL, 0, &thread_id);
    while (!quit) {
    }
    printf("\033[?1049l");
    return 0;
}

DWORD WINAPI ui_main(LPVOID data) {
    const char *prompt = "> ";
    char screenbuf[500];
    size_t i_screenbuf = 0;
    char uibuf[50];
    char statbuf[1024]; // idk, there are some massive monitors
    size_t i_uibuf = 0;
    bool user_quit = false;

    int scroll = 0;
    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h_stdout, &csbi);

    int cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    size_t rows_in_buf = 0;
    
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
            
            // KEY EVENTS
            KEY_EVENT_RECORD k = irbuf[i].Event.KeyEvent;
            if (!k.bKeyDown) continue;

            // Here we know it's a keydown event.

            if (k.wVirtualKeyCode == VK_BACK && i_uibuf > 0)
                uibuf[--i_uibuf] = '\0';
            if (k.wVirtualKeyCode == VK_ESCAPE)
                user_quit = true; // don't break; we want to reset the colors
            if (k.wVirtualKeyCode == VK_RETURN && i_uibuf > 0) {
                push_chlog_msg(&g_chloglist, uibuf);

                while (i_uibuf > 0) uibuf[i_uibuf--] = '\0';
                assert(i_uibuf == 0);
                uibuf[i_uibuf] = '\0';
                /*
                size_t ir_uibuf = 0;
                while (screenbuf[i_screenbuf++] = uibuf[ir_uibuf]) {
                    uibuf[ir_uibuf++] = '\0';
                    if (i_screenbuf >= 498) {
                        screenbuf[i_screenbuf - 1] = '\n';
                        screenbuf[i_screenbuf] = '\0';
                        i_screenbuf = 0;
                    }
                }
                screenbuf[--i_screenbuf] = '\n';
                screenbuf[++i_screenbuf] = '\0';
                i_uibuf = 0;
                */
            }
            // TODO: else if ctrl+c exit, needed?
            // TODO: need to check union?
//            else if (k.wVirtualKeyCode < 0x30 || k.wVirtualKeyCode > 0x5A)
//                continue;
            else if (k.uChar.AsciiChar > 31 && i_uibuf < sizeof(uibuf) - 1)
                uibuf[i_uibuf++] = k.uChar.AsciiChar;
        }
        GetConsoleScreenBufferInfo(h_stdout, &csbi);

        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;

        int stats_len = sprintf(statbuf, "%dx%d  scr:%d  rib:%d", rows, cols,
                scroll, rows_in_buf);

        size_t rows_statline = stats_len / cols + 1;
        size_t rows_uiline = (strlen(prompt) + i_uibuf - 1) / cols + 1;
        size_t rows_screenbuf = rows - (rows_statline + rows_uiline);

        rows_in_buf = fill_buffer(g_screenbuf, sizeof(g_screenbuf),
                &g_chloglist, rows_screenbuf, cols, scroll);

        // Don't accrue scroll when it's not doing anything.
        if (rows_in_buf < rows_screenbuf) scroll = 0;

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
                statbuf, g_screenbuf, rows - (rows_uiline - 1), prompt, uibuf);
    }
    printf("\033[0m");
    printf("\033[2J");
    quit = true;
    return 0;
}

void push_chlog_msg(chloglist *list, char *msg) {
    assert(list != NULL);
    chlognode *newnode = (chlognode *) malloc(sizeof(chlognode));
    newnode->msg = (char *) malloc(strlen(msg) + 1);
    newnode->prev = newnode->next = NULL;
    strcpy(newnode->msg, msg);
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

size_t fill_buffer(
        char *buf, 
        size_t buflen, 
        chloglist *list,
        size_t rows, 
        size_t cols,
        size_t scroll)
{
    assert(list != NULL);
    if (list->head == NULL) {
        assert(list->tail == NULL);
        return 0;
    }

    size_t rows_used = 0, rows_skipped = 0;
    size_t msg_offset_start = 0, msg_offset_end = 0;
    chlognode *curr_node = list->tail;
    while (curr_node != NULL && rows_used < rows) {
        assert(curr_node != NULL);
        assert(curr_node->msg != NULL);
        size_t msg_rows = num_lines(curr_node->msg, cols);
        if (rows_skipped < scroll) {
            if (rows_skipped + msg_rows <= scroll) {
                rows_skipped += msg_rows;
                curr_node = curr_node->prev;
                continue;
            }
            size_t lines_to_take = msg_rows - (scroll - rows_skipped);
            msg_offset_end = cols * lines_to_take;
            rows_skipped += lines_to_take;
            assert(rows_skipped == scroll);
        }

        if (rows_used + msg_rows <= rows) {
            rows_used += msg_rows;
            // ONLY go to the previous message if there are rows left
            if (rows_used < rows) curr_node = curr_node->prev;
            continue;
        }

        size_t rows_left = rows - rows_used;
        // TODO: For newlines, you would start at 0 and count forward, checking
        // the string, and incrementing "rows_skipped" every 80 chars OR on a 
        // newline encountered (resetting chars counter) until "rows_skipped" is
        // equal to (msg_rows - rows_left). then you're left with proper offset.
        msg_offset_start = cols * (msg_rows - rows_left);
        rows_used += rows_left;
        assert(rows_used == rows);
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
    // Start by writing only from the offset...
    assert(msg_offset_start < strlen(curr_node->msg));
    char *msg = curr_node->msg;
    size_t i_msg = msg_offset_start;

    // We're going to replace newlines in msgs for now (so we can see if the
    // server sends many and if we need to keep them. Probably should.)
    // We also write newlines instead of null terms (except the final one) since
    // the buffer will be printed as one string.
    size_t rows_in_buf = num_lines(curr_node->msg + msg_offset_start, cols);
    while (msg[i_msg] != '\0')
        if ((buf[i_buf++] = msg[i_msg++]) == '\n') buf[i_buf - 1] = '_';
    buf[i_buf++] = '\n';
    curr_node = curr_node->next;
    while (curr_node != NULL && rows_in_buf < rows) {
        // We should never have a display buffer too small to hold the highest
        // possible number of characters that could appear on the screen.
        assert(i_buf + strlen(curr_node->msg) < buflen);
        msg = curr_node->msg;
        size_t msg_rows = num_lines(msg, cols);
        i_msg = 0;
        if (rows_in_buf + msg_rows > rows) {
            // Use the end offset
            while (i_msg < msg_offset_end && msg[i_msg] != '\0') 
                if ((buf[i_buf++] = msg[i_msg++]) == '\n') buf[i_buf - 1] = '_';

            buf[i_buf++] = '\0'; // This is the last message

            rows_in_buf += i_msg / cols + (i_msg % cols == 0 ? 0 : 1);
            assert(rows_in_buf == rows);
            continue;
        }

        while (msg[i_msg] != '\0') 
            if ((buf[i_buf++] = msg[i_msg++]) == '\n') buf[i_buf - 1] = '_';
        
        buf[i_buf++] = '\n';
        rows_in_buf += msg_rows;
        curr_node = curr_node->next;
    }
    // We don't want the last newline; nothing will print there, but it will
    // take up a line in the screen buffer
    buf[i_buf - 1] = '\0';
    return rows_in_buf;
}

int num_lines(char *msg, int cols) {
    size_t len = strlen(msg);
    return len / cols + (len % cols == 0 ? 0 : 1);
}

