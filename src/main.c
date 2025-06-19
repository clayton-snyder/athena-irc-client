#include "log.h"
#include "handlers.h"
#include "msgqueue.h"
#include "msgutils.h"
#include "screen_framework.h"
#include "terminalutils.h"

#include <assert.h>
#include <share.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define RECV_BUFF_LEN 1024 * 8

// A longer input buffer helps because we can tell the user exactly how much
// they need to reduce their message by as long as we can read it all.
#define INPUT_BUFF_LEN 1024 * 4

// Needs to be large enough to hold all displayed characters plus all formatting
// data from ANSI escape codes. The screen buff is re-used so there's little 
// downside to severely overestimating this.
// 512 columns, 128 rows, doubled for formatting space = 131kb
// If you are displaying more text than that on a single screen, seek help.
#define SCREEN_BUFF_SIZE 512 * 128 * 2

// The status line is a single line across the top of the screen. Same logic as
// screen buf on the size, but only for one line.
#define STATLINE_BUFF_SIZE 512 * 2

// Longest timestamp we'll write is: YYYY-MM-DD HH:MM:SS + \0.
// Usually it will just be the time, though.
#define TIMESTAMP_BUFF_SIZE 20

#define MAX_NICK_LEN 9
#define MAX_CHANNEL_NAME_LEN 50

const_str logfile_name = "athena.log";

DWORD WINAPI thread_main_recv(LPVOID data);
DWORD WINAPI thread_main_ui(LPVOID);
void DEBUG_print_addr_info(struct addrinfo* addr_info);

// Reads keyboard and mouse input from the provided input source and writes into
// the active screen's UI state.
static bool process_console_input(HANDLE h_stdin);

// Orchestrates the drawing of the entire UI, including the active screen's
// log w/ scroll, input buffer, the global status status bar, etc., all within
// the current terminal width/height.
static void draw_screen(HANDLE h_stdout,
        char *screenbuf, size_t screenbuf_size,
        char *statbuf, size_t statbuf_size);


int main(int argc, char* argv[]) {
    // TODO: platform-specific code. Windows requires this weird _fsopen() in
    // order to have the file be shareable, which is important for reading a
    // log while using the client.
    /*
    FILE *logfile = NULL;
    errno_t err = fopen_s(&logfile, logfile_name, "w");
    if (err != 0) {
        printf("[main] Can't open '%s' for write (%d).\n", logfile_name, err);
        return 23;
    }
    */
    FILE *logfile = _fsopen(logfile_name, "w", _SH_DENYWR);
    if (logfile == NULL) {
        printf("[main] Can't open '%s' for write.\n", logfile_name);
        return 23;
    }
    log_init(logfile);

    if (argc < 3) {
        log(LOGLEVEL_ERROR,
                "Usage: client <hostname> <port> [loglevel=\"warning\"]");
        return 23;
    }

    if (argc >= 4) {
        if (strcmp(argv[3], "spam") == 0) {
            set_logger_level(LOGLEVEL_SPAM);
        } else if (strcmp(argv[3], "dev") == 0) {
            set_logger_level(LOGLEVEL_DEV);
        } else if (strcmp(argv[3], "info") == 0) {
            set_logger_level(LOGLEVEL_INFO);
        } else if (strcmp(argv[3], "warning") == 0) {
            set_logger_level(LOGLEVEL_WARNING);
        } else if (strcmp(argv[3], "error") == 0) {
            set_logger_level(LOGLEVEL_ERROR);
        } else {
            set_logger_level(LOGLEVEL_WARNING);
            log_fmt(LOGLEVEL_WARNING, "Unknown loglevel '%s'. Options are "
                    "'spam', 'dev', info', 'warning', and 'error'. Defaulting "
                    "to 'warning'.", argv[3]);
        }
    } else {
        set_logger_level(LOGLEVEL_WARNING);
        log_fmt(LOGLEVEL_WARNING, "No loglevel specified; set to 'warning'.");
    }

    const char *nick = "anon";
    if (argc >= 5) {
        if (strlen(argv[4]) > MAX_NICK_LEN) {
            log_fmt(LOGLEVEL_ERROR, "Nickname cannot be longer than %d "
                    "characters.", MAX_NICK_LEN);
            return 23;
        }
        nick = argv[4];
    }

    const char *channel = "#codetest";
    if (argc >= 6) {
        if (strlen(argv[5]) > MAX_CHANNEL_NAME_LEN) {
            log_fmt(LOGLEVEL_ERROR, "Invalid channel name '%s': can't be longer"
                     " than %d characters.", argv[5], MAX_CHANNEL_NAME_LEN);
            return 23;
        }
        if (strchr("&#+!", argv[5][0]) == NULL) {
            log_fmt(LOGLEVEL_ERROR, "Invalid channel name '%s': must begin with"
                    " one of '&', '#', '+', or '!'.", argv[5]);
            return 23;
        }
        channel = argv[5];
    }


    log(LOGLEVEL_INFO, "Ages ago, life was born in the primitive sea.");
    
    // TODO: placement?
    init_msg_queues();

    // Initiate use of WS2_32.dll, requesting version 2.2
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        log_fmt(LOGLEVEL_ERROR, "[main] WSAStartup failed: %d", result);
        return 23;
    }

    WORD ver_hi = HIBYTE(wsa_data.wVersion);
    WORD ver_lo = LOBYTE(wsa_data.wVersion);
    log_fmt(LOGLEVEL_INFO, "[main] WSA Version: %u.%u", ver_hi, ver_lo);
    if (ver_hi != 2 || ver_lo != 2) {
        log(LOGLEVEL_ERROR, "[main] Expected version 2.2. Aborting.");
        WSACleanup();
        fclose(logfile);
        return 23;
    }

    struct addrinfo hints, *addr_info = NULL;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    result = getaddrinfo(argv[1], argv[2], &hints, &addr_info);
    if (result != 0) {
        log_fmt(LOGLEVEL_ERROR, "[main] getaddrinfo failed: %d", result);
        WSACleanup();
        fclose(logfile);
        return 23;
    }
    DEBUG_print_addr_info(addr_info);

    SOCKET sock = socket(addr_info->ai_family, addr_info->ai_socktype,
            addr_info->ai_protocol);
    if (sock == INVALID_SOCKET) {
        log_fmt(LOGLEVEL_ERROR, "[main] socket() failed: %lu",
                WSAGetLastError());
        freeaddrinfo(addr_info);
        WSACleanup();
        fclose(logfile);
        return 23;
    }

    result = connect(sock, addr_info->ai_addr, (int)addr_info->ai_addrlen);    
    if (result == SOCKET_ERROR) {
        log_fmt(LOGLEVEL_ERROR, "[main] connect() failed: %lu",
                WSAGetLastError());
        // TODO: Try every addr in the linked list (addrinfo.ai_next) 
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    // TODO: do we need to modify this when we try all the addrs?
    // YES! Save the pointer to the head to use here. Use a copy to iterate.
    freeaddrinfo(addr_info);

    if (sock == INVALID_SOCKET) {
        log_fmt(LOGLEVEL_ERROR, "[main] Unable to connect to %s:%s",
                argv[1], argv[2]);
        WSACleanup();
        fclose(logfile);
        return 23;
    }

    log(LOGLEVEL_INFO, "Connected, apparently.\n");

    // TODO: tell server we're not capable of processing tags
    char cmdbuf_nick[MAX_NICK_LEN + 7 + 1];
    sprintf_s(cmdbuf_nick, sizeof(cmdbuf_nick), "NICK %s\r\n", nick);
    char cmdbuf_join[MAX_CHANNEL_NAME_LEN + 7 + 1];
    sprintf_s(cmdbuf_join, sizeof(cmdbuf_join), "JOIN %s\r\n", channel);
    const char *send_buff[] = {
        cmdbuf_nick,
        "USER ircC 0 * :AthenaIRC Client\r\n",
        cmdbuf_join
    };

    for (int i = 0; i < sizeof(send_buff) / sizeof(const char*); i++) {
        result = send(sock, send_buff[i], (int)strlen(send_buff[i]), 0);
        if (result == SOCKET_ERROR) {
            log_fmt(LOGLEVEL_ERROR, "[main] init send() failed: %lu",
                    WSAGetLastError());
            closesocket(sock);
            WSACleanup();
            return 23;
        }
        log_fmt(LOGLEVEL_DEV, "[main] Sent: %s", send_buff[i]);
    }

    DWORD recv_thread_id = 0; //, ui_thread_id = 0;
    HANDLE h_recv_thread = CreateThread(
            NULL, 0, thread_main_recv, &sock, 0, &recv_thread_id);

    // Use alternative screen buffer
    printf("\033[?1049h");

    HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h_stdin == INVALID_HANDLE_VALUE || h_stdout == INVALID_HANDLE_VALUE) {
        printf("Invalid std handle\n");
        WSACleanup();
        fclose(logfile);
        return 23;
    }

    DWORD prev_mode;
    if (!GetConsoleMode(h_stdin, &prev_mode)) {
        printf("GetConsoleMode() failed\n");
        WSACleanup();
        fclose(logfile);
        return 23;
    }

    // Quick Edit mode seems to interfere with mouse input; before diabling it,
    // scroll events were coming in as key events for up/down arrow.
    // Note that ENABLE_EXTENDED_FLAGS must be enabled to disable Quick Edit.
	DWORD new_mode = (ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS) &
                     ~(ENABLE_QUICK_EDIT_MODE);
    if (!SetConsoleMode(h_stdin, new_mode)) {
        printf("SetConsoleMode failed\n");
        WSACleanup();
        fclose(logfile);
        return 23;
    }

    bool bye = false;
    char drawbuf_screen[SCREEN_BUFF_SIZE] = { 0 };
    char drawbuf_statline[STATLINE_BUFF_SIZE] = { 0 };

    while (!bye) {
        char timestamp_buf[TIMESTAMP_BUFF_SIZE];
        msgutils_get_timestamp(timestamp_buf, sizeof(timestamp_buf), false,
                TIMESTAMP_FORMAT_TIME_ONLY);

        // INCOMING msgs
        msglist msgs_in = msg_queue_takeall(QUEUE_IN);
        struct msgnode *curr_msgnode = msgs_in.head;
        while (curr_msgnode != NULL) {
            // TODO: Get all of the initial connection server msgs printable
            log_fmt(LOGLEVEL_DEV, "[main] [%s] SERVER SAYS: \"%s\"",
                    timestamp_buf, curr_msgnode->msg);

            ircmsg *ircm = msgutils_ircmsg_parse(curr_msgnode->msg);
            curr_msgnode = curr_msgnode->next;
            if (ircm == NULL) continue;
 
            handle_ircmsg(ircm, timestamp_buf);
            msgutils_ircmsg_free(ircm);
        }
 
        msglist_free(&msgs_in);

        // UI msgs
        msglist msgs_ui = msg_queue_takeall(QUEUE_UI);
        curr_msgnode = msgs_ui.head;
        while (curr_msgnode != NULL) {
            char* msg = curr_msgnode->msg;
            assert(msg != NULL);
            curr_msgnode = curr_msgnode->next;

            bye = handle_user_command(msg, nick, channel, sock, timestamp_buf);
            if (bye) break;
        }
        msglist_free(&msgs_ui);
        // TODO: do we still have out msgs??

        // Update the UI
        // TODO: read bool and exit if appropriate
        process_console_input(h_stdin);

        draw_screen(h_stdout,
                drawbuf_screen, sizeof(drawbuf_screen),
                drawbuf_statline, sizeof(drawbuf_statline));
        
        fflush(logfile);
    }

    WaitForSingleObject(h_recv_thread, INFINITE);

    printf("\033[0m"); // Reset all formatting modes
    printf("\033[2J"); // Clear entire screen
    printf("\033[?1049l"); // Return from alternative screen buffer

    if (!SetConsoleMode(h_stdin, prev_mode))
        printf("SetConsoleMode(): Failed to reset to old console mode.\n");

    closesocket(sock);
    WSACleanup();
    fclose(logfile);

    return 0;
}

// TODO: platform-specific code
static bool process_console_input(HANDLE h_stdin) {
    bool user_quit = false;
    screen_ui_state *const st = screen_get_active_ui_state();
    assert(st != NULL);

    // ReadConsoleInput blocks until it reads at least one input event, so we 
    // need to avoid calling it when there's nothing to read.
    if (WaitForSingleObject(h_stdin, 0) != WAIT_OBJECT_0) return false;

    // TODO: make irbuf size constant
    static INPUT_RECORD irbuf[128];
    DWORD n_events = 0;
    ReadConsoleInput(h_stdin, irbuf, 128, &n_events);
    for (DWORD i = 0; i < n_events; i++) {
        if (irbuf[i].EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD m = irbuf[i].Event.MouseEvent;
            // Process the mouse event
            if (!(m.dwEventFlags & MOUSE_WHEELED)) continue;

            bool is_hiword_negative = HIWORD(m.dwButtonState) & 1<<15;

            if (is_hiword_negative) {
                st->scroll--;
                if (st->scroll < 0) st->scroll = 0;
            } else {
                if (!st->scroll_at_top) st->scroll++;
            }

            continue;
        }

        if (irbuf[i].EventType != KEY_EVENT) continue;

        KEY_EVENT_RECORD k = irbuf[i].Event.KeyEvent;
        if (!k.bKeyDown) continue;
        if (k.wVirtualKeyCode == VK_BACK && st->i_inputbuf > 0)
            st->inputbuf[--st->i_inputbuf] = '\0';
        if (k.wVirtualKeyCode == VK_ESCAPE)
            user_quit = true; // don't break; we want to reset the colors
        if (k.wVirtualKeyCode == VK_RETURN && st->i_inputbuf > 0) {
            msgqueue_pushback_copy(QUEUE_UI, st->inputbuf);

            while (st->i_inputbuf > 0)
                st->inputbuf[st->i_inputbuf--] = '\0';

            assert(st->i_inputbuf == 0);
            st->inputbuf[st->i_inputbuf] = '\0';
        }
        else if (k.uChar.AsciiChar > 31 &&
                 st->i_inputbuf < sizeof(st->inputbuf) - 1)
        {
            st->inputbuf[st->i_inputbuf++] = k.uChar.AsciiChar;
        }
    }

    return user_quit;
}

static void draw_screen(HANDLE h_stdout,
        char *screenbuf, size_t screenbuf_size,
        char *statbuf, size_t statbuf_size)
{
    screen_ui_state *const st = screen_get_active_ui_state();
    assert(st != NULL);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h_stdout, &csbi);

    int term_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int term_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;

    int stats_len = sprintf_s(statbuf, statbuf_size,
            "%dx%d  scr:%d",
            term_rows, term_cols, st->scroll);

    int rows_statline = stats_len / term_cols + 1;
    int rows_uiline = (strlen(st->prompt) + st->i_inputbuf - 1) / term_cols + 1;
    int rows_screenbuf = term_rows - (rows_statline + rows_uiline);

    screen_fmt_to_buf(screenbuf, screenbuf_size, rows_screenbuf, term_cols);

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
            statbuf, screenbuf, term_rows - (rows_uiline - 1),
            st->prompt, st->inputbuf);
}

DWORD WINAPI thread_main_recv(LPVOID data) {
    SOCKET *sock = (SOCKET *)data;
    char recv_buff[RECV_BUFF_LEN];
    int bytes_received = 0, i_buff_offset = 0;
    while ((bytes_received = recv(
                    *sock,
                    recv_buff + i_buff_offset,
                    RECV_BUFF_LEN - i_buff_offset,
                    0)) > 0)
    {
        i_buff_offset += bytes_received;
        if (i_buff_offset >= RECV_BUFF_LEN) {
            log(LOGLEVEL_ERROR, "[thread_main_recv] FATAL: Ran out of buffer.");
            // TODO: communiate failure and clean up
            return 23;
        }

        // Start at end of the unprocessed data and search backwards for \r\n
        int i_last_delim = i_buff_offset;
        bool delim_found = false;
        while (!delim_found && --i_last_delim > 0) {
            delim_found = recv_buff[i_last_delim - 1] == '\r' &&
                          recv_buff[i_last_delim] == '\n';
        }

        log_fmt(LOGLEVEL_DEV, "[thread_main_recv] delim_found=%d, "
                "i_last_delim=%d(%d), bytes_received=%d, i_buff_offset=%d",
                delim_found, i_last_delim, recv_buff[i_last_delim],
                bytes_received, i_buff_offset);

        if (!delim_found)
            continue;

        // Parse recv_buffer data into messages and build a msglist
        msglist msgs = { .head = NULL, .tail = NULL, .count = 0 };
        size_t msg_start = 0;
        for (int i = 0; i < i_last_delim; i++) {
            if (!(recv_buff[i] == '\r' && recv_buff[i + 1] == '\n'))
                continue;

            // msglist msgs should be null terminated.
            recv_buff[i] = '\0';
            recv_buff[i + 1] = '\0';
            msglist_pushback_copy(&msgs, &recv_buff[msg_start]);

            // Skip the two null terminators we wrote.
            msg_start = i++ + 2;
        }

        if (msgs.count > 0) {
            msglist_submit(QUEUE_IN, &msgs);
            log_fmt(LOGLEVEL_DEV, "[thread_main_recv] Submitted %d msgs to IN.",
                    msgs.count);
        }

        assert(i_buff_offset < RECV_BUFF_LEN);
        assert(i_last_delim < i_buff_offset);

        // Move any buffer data after the last delim to the beginning.
        int copy_from = i_last_delim + 1, copy_to = 0;
        while (copy_from < i_buff_offset) 
            recv_buff[copy_to++] = recv_buff[copy_from++];
        i_buff_offset = copy_to;
    }

    if (bytes_received < 0) {
        log_fmt(LOGLEVEL_ERROR, "[thread_main_recv] recv() failed: %lu",
                WSAGetLastError());
        // TODO: Communicate failure and cleanup
        return 23;
    }

    log(LOGLEVEL_WARNING, "[thread_main_recv] Server disconnected, apparently");

    return 0;
}

void DEBUG_print_addr_info(struct addrinfo* addr_info) {
    log_fmt(LOGLEVEL_DEV,
            "family:%d\nsocktype:%d\nprotocol:%d\naddrlen:%zu\ncanonname:%s",
            addr_info->ai_family,
            addr_info->ai_socktype,
            addr_info->ai_protocol,
            addr_info->ai_addrlen,
            addr_info->ai_canonname);

    if (addr_info->ai_family == AF_INET) {
        struct sockaddr_in *v4 = (struct sockaddr_in *) addr_info->ai_addr;
        char buff[16];
        inet_ntop(AF_INET, &v4->sin_addr, (char*)&buff, sizeof(buff));
        log_fmt(LOGLEVEL_DEV, "[print_addr_info] (IPv4) %s", buff);
    } else if (addr_info->ai_family == AF_INET6) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *) addr_info->ai_addr;
        char buff[46];
        inet_ntop(AF_INET6, &v6->sin6_addr, (char*)&buff, sizeof(buff));
        log_fmt(LOGLEVEL_DEV, "[print_addr_info] (IPv6) %s", buff);
    } else {
        log_fmt(LOGLEVEL_WARNING,
                "[print_addr_info] Unexpected ai_family value: %d",
                addr_info->ai_family);
    }
}

