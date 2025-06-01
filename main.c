#include "log.h"
#include "msgqueue.h"
#include "stringutils.h"
#include "terminalutils.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define RECV_BUFF_LEN 1024 * 8

/* BUF ALLOCATION GUIDE:
 * buffer to hold snprintf result: char cmd_buf[MAX_CMD_LEN + 1]
 * buffer to send to IRC server: char cmd_buf[IRC_MSG_BUF_LEN]
 */

// Max IRC message allowed is 512 including the CRLF delimiter. This macro value
// should be used when validating strlen() of messages prior to preparing them
// with CRLF delimiter. Use this macro value + 1 when allocating buffers to 
// prepare messages to pass to send_as_irc().
#define MAX_CMD_LEN 510

// Use this to allocate buffers that will be sent to the IRC server. The two 
// extra bytes are reserved for CR and LF.
#define IRC_MSG_BUF_LEN (MAX_CMD_LEN + 2)

// A longer input buffer helps because we can tell the user exactly how much
// they need to reduce their message by as long as we can read it all.
#define INPUT_BUFF_LEN 1024 * 4

DWORD WINAPI thread_main_recv(LPVOID data);
DWORD WINAPI thread_main_ui(LPVOID data);
void handlecmd_channel(char *cmd, SOCKET sock);
void DEBUG_print_addr_info(struct addrinfo* addr_info);

// TODO: rename, and do we need the bool return?
static bool try_send_as_irc(SOCKET sock, const char* fmt, ...);
static int send_as_irc(SOCKET sock, const char* msg);
// TODO: should this return bool?
bool handle_local_command(char *cmd_str, SOCKET sock);

int main(int argc, char* argv[]) {
    if (argc < 3) {
        log(LOGLEVEL_ERROR,
                "Usage: client <hostname> <port> [loglevel=\"warning\"]");
        return 23;
    }

    if (argc == 4) {
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
        return 23;
    }

    log(LOGLEVEL_INFO, "Connected, apparently.\n");

    const char *send_buff[] = {
        "NICK code\r\n",
        "USER ircC 0 * :AthenaIRC Client\r\n",
        "JOIN #codetest\r\n",
        "PRIVMSG #codetest :All systems operational.\r\n"
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

    DWORD recv_thread_id = -1, ui_thread_id = -1;
    HANDLE recv_thread = CreateThread(
            NULL, 0, thread_main_recv, &sock, 0, &recv_thread_id);
    HANDLE ui_thread = CreateThread(
            NULL, 0, thread_main_ui, NULL, 0, &ui_thread_id);

    bool bye = false;
    while (!bye) {
        // INCOMING msgs
        msglist msgs_in = msg_queue_takeall(QUEUE_IN);
        struct msgnode *curr_msgnode = msgs_in.head;
        while (curr_msgnode != NULL) {
//            printf("[main] SERVER SAYS: \"%s\"\n", curr_msgnode->msg);
            log_fmt(LOGLEVEL_SPAM, "[main] SERVER SAYS: \"%s\"", curr_msgnode->msg);
            curr_msgnode = curr_msgnode->next;
        }
        msglist_free(&msgs_in);

        // UI msgs
        msglist msgs_ui = msg_queue_takeall(QUEUE_UI);
        curr_msgnode = msgs_ui.head;
        while (curr_msgnode != NULL) {
            char* msg = curr_msgnode->msg;
            assert(msg != NULL);
            curr_msgnode = curr_msgnode->next;

            // TODO: Check type of command. Options: 
            // * It's a local command prefix. Try to process it
            // * It's a "send to server" prefix. Validate minimally, remove
            //      prefix, send to OQ.
            // * No prefix. Validate user is in a channel, and len, send to OQ. 
            // NOTE: Things going to OQ should be IRC messages!!

            // TODO: refactor this garb
            if (msg[0] == '!') {
                // Local command
                bye = handle_local_command(msg + 1, sock);
                if (bye) break;
            }
            else if (msg[0] == '`') {
                try_send_as_irc(sock, (msg + 1));
            } else {
                // TODO: Validate if they're in a channel
                // TODO: move this to a global string
                char* fmt = "PRIVMSG %s :%s";
                // TODO: insert actual channel param.
                try_send_as_irc(sock, fmt, "#codetest", msg);
            }
        }
        msglist_free(&msgs_ui);
        // Out msgs
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}

static int send_as_irc(SOCKET sock, const char* msg) {
    // We need to replace \0 with \r\n
    size_t msg_len = strlen(msg) + 2;
    assert(msg_len <= IRC_MSG_BUF_LEN);
    char send_buff[IRC_MSG_BUF_LEN];
    strcpy(send_buff, msg);
    send_buff[msg_len - 2] = '\r';
    send_buff[msg_len - 1] = '\n';

    return send(sock, send_buff, msg_len, 0);
}

static bool try_send_as_irc(SOCKET sock, const char* fmt, ...) {
    // TODO: validate if this works with empty va_list. I think it just has to
    // match the number of formatters in fmt...
    va_list fmt_args;
    va_start(fmt_args, fmt);
    // vsnprintf writes up to bufsz - 1 characters, plus the null term. Ergo
    // MAX_CMD_LEN + 1 gives us room for CRLF if we overwrite the null term.
    char irc_cmd[MAX_CMD_LEN + 1];
    int full_len = vsnprintf(irc_cmd, sizeof(irc_cmd), fmt, fmt_args);
    va_end(fmt_args);

    // vsnprintf returns the would-be expanded string length (without the null
    // term), so we can use it to check for truncation.
    if (full_len + 2 > IRC_MSG_BUF_LEN) {
        log_fmt(LOGLEVEL_WARNING, "[main] Expanded message too long (%d); not "
                "sent: \'%s\'(truncated)", full_len, irc_cmd);
        //TODO: OUTPUT
        termutils_set_text_color(TERMUTILS_COLOR_YELLOW);
        termutils_set_bold(true);
        printf("Your message is too long. Reduce it by %d characters.\n",
                full_len + 2 - IRC_MSG_BUF_LEN);
        termutils_reset_all();
        return false;
    }

    int result = send_as_irc(sock, irc_cmd);
    if (result == SOCKET_ERROR) {
        // TODO: communicate failure
        log_fmt(LOGLEVEL_ERROR, "[try_send_as_irc] send() failed: %lu",
                WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        exit(23);
    }
    return true;
}

// Returns true if program should exit.
bool handle_local_command(char *cmd, SOCKET sock) {
    assert(cmd != NULL);
    assert(sock != INVALID_SOCKET);
    log_fmt(LOGLEVEL_DEV, "[handle_local_command] Processing '%s'", cmd);
    if (strcmp(cmd, "BYE") == 0) return true;
    if (strutils_startswith(cmd, "channel ") || strcmp(cmd, "channel") == 0)
        handlecmd_channel(cmd, sock);
    return false;
}

void handlecmd_channel(char *cmd, SOCKET sock) {
    assert(cmd != NULL);
    assert(sock != INVALID_SOCKET);

    char* tk_cmd = strtok(cmd, " ");
    assert(strcmp(tk_cmd, "channel") == 0);
    char* tk_action = strtok(NULL, " ");
    if (tk_action == NULL || strcmp(tk_action, "help") == 0) {
        // TODO: output
        termutils_set_text_color(TERMUTILS_COLOR_YELLOW);
        // TODO: HELP string
        puts("Usage: !channel <action> [args...]");
        puts("Avaiable actions for !channel:\n"
                "* list [names]\n"
                "\tDisplay info about the listed channels."
                "\n\t[names] is a comma-separated list (with NO spaces)."
                "\n\tIf [names] is omitted, lists all channels.\n"
                "* join <name>\n"
                "\tJoin or create the specified channel.\n"
                "* switch <name>\n"
                "\tSwitch the active (displayed) channel to the one specified."
                "\n\tYou must have previously joined the channel.\n"
                "* part <name>\n"
                "\tLeave the specified channel.\n");
    }
    else if (strcmp(tk_action, "list") == 0) {
        char* tk_names = strtok(NULL, " ");
        if (tk_names != NULL && strtok(NULL, " ") != NULL) {
            // TODO: output
            termutils_set_text_color(TERMUTILS_COLOR_YELLOW);
            termutils_set_bold(true);
            puts("Too many arguments for 'list' command. [names] should be "
                    "a comma-separated list (with no spaces) of the channel"
                    " names you wish to display info about.");
            termutils_reset_all();
        }
        try_send_as_irc(sock, "LIST %s", tk_names ? tk_names : "");
    }
    // TODO: other !channel actions
    log_fmt(LOGLEVEL_DEV, "Performed !channel %s", tk_action ? tk_action : "");
}


DWORD WINAPI thread_main_ui(LPVOID data) {
    // TODO: This function should be refactored to use a string reading function
    // that mallocs to tolerate any user input size so we can always tell the
    // user how much smaller their message needs to be and to avoid an
    // unnecessary string copy. `getline()` does this, but Windows doesn't have
    // that, so I need to write it myself. For now we use a really huge buffer
    // and admonish the user if they exceed it.
    msglist msgs = { .head = NULL, .tail = NULL, .count = 0 };
    char ui_buff[INPUT_BUFF_LEN];

    bool bye = false;
    while (!bye) {
        char *str = fgets(ui_buff, sizeof(ui_buff), stdin);
        size_t str_len = strlen(str);
        log_fmt(LOGLEVEL_DEV, "[thread_main_ui] fgets(): str_len=%d", str_len);
        
        if (str == NULL) {
            log_fmt(LOGLEVEL_ERROR, "[thread_main_ui] fgets() gave NULL str. "
                    "ferror(stdin): %d", ferror(stdin));
            // TODO: communicate failure
            exit(23);
        }

        // TODO: certainly this can be refactored.
        if (ui_buff[str_len - 1] != '\n') {
            if (str_len < INPUT_BUFF_LEN - 1) {
                log_fmt(LOGLEVEL_ERROR,
                        "[thread_main_ui] End of string is not \\n, but buff "
                        "was not filled. Because input is stdin, this is "
                        "unexpected. str_len=%d, INPUT_BUFF_LEN=%d, "
                        "sizeof(ui_buff)=%d, str='%s'", str_len, INPUT_BUFF_LEN,
                        sizeof(ui_buff), str);
                // TODO: communicate failure
                exit(23);
            }
            //TODO:output
            termutils_set_text_color(TERMUTILS_COLOR_YELLOW);
            termutils_set_bold(true);
            printf("You have *far* exceeded the allowable message length. "
                    "Enter a message shorter than %d characters and I can tell "
                    "you specifically how much smaller your message must be.\n",
                    sizeof(ui_buff));
            termutils_reset_all();
        }

        // fgets() should include the delimiting \n in the buffer...
        assert(ui_buff[str_len - 1] == '\n');
        ui_buff[--str_len] = '\0'; // ... and we don't want it.

        if (strcmp(str, "BYE") == 0) {
            log(LOGLEVEL_INFO, "[thread_main_ui] received BYE command. BYE!\n");
            bye = true;
            // TODO: output
        }
        log_fmt(LOGLEVEL_DEV, "[thread_main_ui] submitting: \"%s\"", str);

        // TODO: this can change to msglist_pushback_take when we impl getline()
        msglist_pushback_copy(&msgs, str);
        msglist_submit(QUEUE_UI, &msgs);
        msgs.head = msgs.tail = NULL;
        msgs.count = 0;
    }

    return 0;
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
            exit(23);
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
        for (size_t i = 0; i < i_last_delim; i++) {
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
        size_t copy_from = i_last_delim + 1, copy_to = 0;
        while (copy_from < i_buff_offset) 
            recv_buff[copy_to++] = recv_buff[copy_from++];
        i_buff_offset = copy_to;
    }

    if (bytes_received < 0) {
        log_fmt(LOGLEVEL_ERROR, "[thread_main_recv] recv() failed: %lu",
                WSAGetLastError());
        // TODO: Communicate failure and cleanup
        exit(23);
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

