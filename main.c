#include "msgqueue.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define BUFF_LEN 1024 * 8

DWORD WINAPI thread_main_recv(LPVOID data);
void print_addr_info(struct addrinfo* addr_info);

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: client <hostname> <port>\n");
        return 3;
    }

    printf("Ages ago, life was born in the primitive sea.\n");
    
    // TODO: placement?
    init_msg_queues();

    // Initiate use of WS2_32.dll, requesting version 2.2
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        printf("WSAStartup failed, returned: %d\n", result);
        return 1;
    }

    WORD ver_hi = HIBYTE(wsa_data.wVersion);
    WORD ver_lo = LOBYTE(wsa_data.wVersion);
    printf("WSA Version: %u.%u\n", ver_hi, ver_lo);
    if (ver_hi != 2 || ver_lo != 2) {
        printf("Expected version 2.2. Aborting.\n");
        WSACleanup();
        return 2;
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
        return 4;
    }
    print_addr_info(addr_info);

    SOCKET sock = socket(addr_info->ai_family, addr_info->ai_socktype,
            addr_info->ai_protocol);
    if (sock == INVALID_SOCKET) {
        printf("socket() failed: %lu\n", WSAGetLastError());
        freeaddrinfo(addr_info);
        WSACleanup();
        return 5;
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
        printf("Unable to connect to %s:%s (", argv[1], argv[2]);
        WSACleanup();
        return 6;
    }

    printf("Connected, apparently.\n");

    const char *send_buff[2] = {
        "NICK code\r\n",
        "USER ircC 0 * :AthenaIRC Client\r\n"
    };

    for (int i = 0; i < sizeof(send_buff) / sizeof(const char*); i++) {
        result = send(sock, send_buff[i], (int)strlen(send_buff[i]), 0);
        if (result == SOCKET_ERROR) {
            printf("send() failed: %lu\n", WSAGetLastError());
            closesocket(sock);
            WSACleanup();
            return 8;
        }
        printf("Sent: %s\n", send_buff[i]);
    }

    // create thread
    DWORD recv_thread_id = -1;
    HANDLE recv_thread = CreateThread(
            NULL, 0, thread_main_recv, &sock, 0, &recv_thread_id);

    // Continue: MAIN LOOP
    while (1) {
        msglist msgs_in = msg_queue_takeall(QUEUE_IN);
        struct msgnode *curr_msgnode = msgs_in.head;
        while (curr_msgnode != NULL) {
            printf("[main] MSG_IN: %s\n", curr_msgnode->msg);
            curr_msgnode = curr_msgnode->next;
        }

        // UI msgs
        // Out msgs
    }

    closesocket(sock);
    WSACleanup();

    return 0;
}

DWORD WINAPI thread_main_recv(LPVOID data) {
    SOCKET *sock = (SOCKET *)data;
    char recv_buff[BUFF_LEN];
    int bytes_received = 0, buff_offset = 0;
    while ((bytes_received = recv(
                    *sock,
                    recv_buff + buff_offset,
                    BUFF_LEN - buff_offset,
                    0)) > 0)
    {
        buff_offset += bytes_received;
        if (buff_offset >= BUFF_LEN) {
            printf("[thread_main_recv] FATAL: Ran out of buffer.\n");
            // TODO: communiate failure and clean up
            exit(23);
        }

        // Start at end of the unprocessed data and search backwards for \r\n
        int last_delim = buff_offset;
        bool delim_found = false;
        while (--last_delim > 0 && !delim_found) {
            delim_found = recv_buff[last_delim - 1] = '\r' &&
                          recv_buff[last_delim] == '\n';
        }

        // TODO: debug log
        printf("[thread_main_recv] delim_found=%d, last_delim=%d, BUFF_LEN=%d,"
                " bytes_received=%d, buff_offset=%d\n", delim_found, last_delim,
                BUFF_LEN, bytes_received, buff_offset);

        if (!delim_found)
            continue;

        // Parse recv_buffer data into messages and build a msglist
        msglist msgs = { .head = NULL, .tail = NULL, .count = 0 };
        size_t msg_start = 0;
        for (size_t i = 0; i < last_delim; i++) {
            if (!(recv_buff[i] == '\r' && recv_buff[i + 1] == '\n'))
                continue;

            // msglist msgs should be null terminated.
            recv_buff[i] = '\0';
            recv_buff[i + 1] = '\0';
            msglist_pushback(&msgs, &recv_buff[msg_start]);

            // TODO: debug log
            printf("[thread_main_recv] new msg: %s\n", &recv_buff[msg_start]);

            // Skip the two null terminators we wrote.
            msg_start = i++ + 2;
        }

        if (msgs.count > 0) {
            msglist_submit(QUEUE_IN, &msgs);
            // TODO: Debug log
            printf("[thread_main_recv] Submitted %d messages.\n", msgs.count);
        }

        assert(buff_offset < BUFF_LEN);
        assert(last_delim < buff_offset);

        // Move any buffer data after the last delim to the beginning.
        size_t copy_from = last_delim + 1, copy_to = 0;
        while (copy_from < buff_offset) 
            recv_buff[copy_to++] = recv_buff[copy_from++];
        buff_offset = copy_to;
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
