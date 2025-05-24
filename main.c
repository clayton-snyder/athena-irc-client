#include "msgqueue.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define RECV_BUFF_LEN 1024 * 8
#define INPUT_BUFF_LEN 512 // max IRC message length. change this later

DWORD WINAPI thread_main_recv(LPVOID data);
DWORD WINAPI thread_main_ui(LPVOID data);
void print_addr_info(struct addrinfo* addr_info);

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: client <hostname> <port>\n");
        return 23;
    }

    printf("Ages ago, life was born in the primitive sea.\n");
    
    // TODO: placement?
    init_msg_queues();

    // Initiate use of WS2_32.dll, requesting version 2.2
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        printf("WSAStartup failed, returned: %d\n", result);
        return 23;
    }

    WORD ver_hi = HIBYTE(wsa_data.wVersion);
    WORD ver_lo = LOBYTE(wsa_data.wVersion);
    printf("WSA Version: %u.%u\n", ver_hi, ver_lo);
    if (ver_hi != 2 || ver_lo != 2) {
        printf("Expected version 2.2. Aborting.\n");
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
        printf("getaddrinfo failed, returned: %d\n", result);
        WSACleanup();
        return 23;
    }
    print_addr_info(addr_info);

    SOCKET sock = socket(addr_info->ai_family, addr_info->ai_socktype,
            addr_info->ai_protocol);
    if (sock == INVALID_SOCKET) {
        printf("socket() failed: %lu\n", WSAGetLastError());
        freeaddrinfo(addr_info);
        WSACleanup();
        return 23;
    }

    result = connect(sock, addr_info->ai_addr, (int)addr_info->ai_addrlen);    
    if (result == SOCKET_ERROR) {
        printf("connect() failed: %lu\n", WSAGetLastError());
        // TODO: Try every addr in the linked list (addrinfo.ai_next) 
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    // TODO: do we need to modify this when we try all the addrs?
    // YES! Save the pointer to the head to use here. Use a copy to iterate.
    freeaddrinfo(addr_info);

    if (sock == INVALID_SOCKET) {
        printf("Unable to connect to %s:%s", argv[1], argv[2]);
        WSACleanup();
        return 23;
    }

    printf("Connected, apparently.\n");

    const char *send_buff[] = {
        "NICK code\r\n",
        "USER ircC 0 * :AthenaIRC Client\r\n",
        "JOIN #codetest\r\n",
        "PRIVMSG #codetest :All systems operational.\r\n"
    };

    for (int i = 0; i < sizeof(send_buff) / sizeof(const char*); i++) {
        result = send(sock, send_buff[i], (int)strlen(send_buff[i]), 0);
        if (result == SOCKET_ERROR) {
            // TODO: debug log
            printf("[main] init send() failed: %lu\n", WSAGetLastError());
            closesocket(sock);
            WSACleanup();
            return 23;
        }
        printf("Sent: %s\n", send_buff[i]);
    }

    DWORD recv_thread_id = -1, ui_thread_id = -1;
    HANDLE recv_thread = CreateThread(
            NULL, 0, thread_main_recv, &sock, 0, &recv_thread_id);
    HANDLE ui_thread = CreateThread(
            NULL, 0, thread_main_ui, NULL, 0, &ui_thread_id);

    bool bye = false;
    while (!bye) {
        msglist msgs_in = msg_queue_takeall(QUEUE_IN);
        struct msgnode *curr_msgnode = msgs_in.head;
        while (curr_msgnode != NULL) {
            printf("[main] MSG_IN: %s\n", curr_msgnode->msg);
            curr_msgnode = curr_msgnode->next;
        }

        // UI msgs
        msglist msgs_ui = msg_queue_takeall(QUEUE_UI);
        curr_msgnode = msgs_ui.head;
        while (curr_msgnode != NULL) {
            char* msg = curr_msgnode->msg;
            if (strcmp(msg, "BYE") == 0) {
                printf("[main] received BYE command. BYE!\n");
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
                // TODO: debug log
                printf("[main] UI send() failed: %lu\n", WSAGetLastError());
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
            // TODO: debug log
            printf("[thread_main_ui] gets_s returned NULL.\n");
            // TODO: communicate failure
            return 23;
        }
        if (strcmp(str, "BYE") == 0) {
            printf("[thread_main_ui] received BYE command. BYE!\n");
            bye = true;
        }
        printf("[thread_main_ui] submitting: \"%s\"\n", str);

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
            printf("[thread_main_recv] FATAL: Ran out of buffer.\n");
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

        // TODO: debug log
        printf("[thread_main_recv] delim_found=%d, i_last_delim=%d(%d), "
                "bytes_received=%d, i_buff_offset=%d\n", delim_found,
                i_last_delim, recv_buff[i_last_delim], bytes_received,
                i_buff_offset);

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

            // TODO: debug log
            //printf("[thread_main_recv] new msg: %s\n", &recv_buff[msg_start]);

            // Skip the two null terminators we wrote.
            msg_start = i++ + 2;
        }

        if (msgs.count > 0) {
            msglist_submit(QUEUE_IN, &msgs);
            // TODO: Debug log
            printf("[thread_main_recv] Submitted %d msgs to IN.\n", msgs.count);
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
        printf("recv() failed: %lu\n", WSAGetLastError());
        // TODO: Communicate failure and cleanup
        exit(23);
    }

    // TODO: debug log
    printf("[thread_main_recv] Server disconnected, apparently.\n");

    return 0;
}

void print_addr_info(struct addrinfo* addr_info) {
    printf("family:%d\nsocktype:%d\nprotocol:%d\naddrlen:%zu\ncanonname:%s\n",
            addr_info->ai_family,
            addr_info->ai_socktype,
            addr_info->ai_protocol,
            addr_info->ai_addrlen,
            addr_info->ai_canonname);

    if (addr_info->ai_family == AF_INET) {
        struct sockaddr_in *v4 = (struct sockaddr_in *) addr_info->ai_addr;
        char buff[16];
        inet_ntop(AF_INET, &v4->sin_addr, (char*)&buff, sizeof(buff));
        printf("(IPv4) %s\n", buff);
    } else if (addr_info->ai_family == AF_INET6) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *) addr_info->ai_addr;
        char buff[46];
        inet_ntop(AF_INET6, &v6->sin6_addr, (char*)&buff, sizeof(buff));
        printf("(IPv6) %s\n", buff);
    } else {
        printf("Unexpected ai_family value: %d\n", addr_info->ai_family);
    }
}
