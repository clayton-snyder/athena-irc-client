#include "log.h"
#include "msgqueue.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define RECV_BUFF_LEN 1024 * 8
#define INPUT_BUFF_LEN 512 // max IRC message length. change this later

DWORD WINAPI thread_main_recv(LPVOID data);
DWORD WINAPI thread_main_ui(LPVOID data);
void DEBUG_print_addr_info(struct addrinfo* addr_info);

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

        // UI msgs
        msglist msgs_ui = msg_queue_takeall(QUEUE_UI);
        curr_msgnode = msgs_ui.head;
        while (curr_msgnode != NULL) {
            char* msg = curr_msgnode->msg;
            if (strcmp(msg, "BYE") == 0) {
                log(LOGLEVEL_INFO, "[main] received BYE command. BYE!");
                bye = true;
                break;
            }

            // We need to replace \0 with \r\n
            size_t msg_len = strlen(msg) + 2;
            assert(msg_len < INPUT_BUFF_LEN + 1);
            char send_buff[INPUT_BUFF_LEN + 1];
            strcpy(send_buff, msg);
            send_buff[msg_len - 2] = '\r';
            send_buff[msg_len - 1] = '\n';

            result = send(sock, send_buff, msg_len, 0);
            if (result == SOCKET_ERROR) {
                log_fmt(LOGLEVEL_ERROR, "[main] UI send() failed: %lu",
                        WSAGetLastError());
                closesocket(sock);
                WSACleanup();
                return 23;
            }
            curr_msgnode = curr_msgnode->next;
        }
        // Out msgs
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}

DWORD WINAPI thread_main_ui(LPVOID data) {
    char buff[INPUT_BUFF_LEN];
    msglist msgs = { .head = NULL, .tail = NULL, .count = 0 };

    bool bye = false;
    while (!bye) {
        char* str = gets_s(buff, INPUT_BUFF_LEN);
        if (str == NULL) {
            log(LOGLEVEL_ERROR, "[thread_main_ui] gets_s returned NULL.\n");
            // TODO: communicate failure
            exit(23);
        }
        if (strcmp(str, "BYE") == 0) {
            log(LOGLEVEL_INFO, "[thread_main_ui] received BYE command. BYE!\n");
            bye = true;
        }
        log_fmt(LOGLEVEL_DEV, "[thread_main_ui] submitting: \"%s\"", str);

        msglist_pushback(&msgs, str);
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
            msglist_pushback(&msgs, &recv_buff[msg_start]);

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

