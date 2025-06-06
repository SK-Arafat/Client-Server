#ifndef _WIN32
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#define closesocket close
#define SOCKET int
#define INVALID_SOCKET (-1)
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#define DEFAULT_PORT "60000"
#define DEFAULT_HOST "localhost"
#define MAX_BUFFER 4096
#define CONNECT_TIMEOUT_SECONDS 5

/* Logging function */
void log_message(const char *format, ...) {
    va_list args;
    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(stderr, "[%s] ", timestamp);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* Cleanup function for Windows */
#ifdef _WIN32
void cleanup_winsock(WSADATA *wsaData) {
    if (WSACleanup() != 0) {
        log_message("WSACleanup failed: %d", WSAGetLastError());
    }
}
#endif

/* Set socket timeout */
int set_socket_timeout(SOCKET sock, int timeout_seconds) {
    struct timeval tv;
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;
    
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0 ||
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        log_message("Failed to set socket timeout: %d", errno);
        return -1;
    }
    return 0;
}

/* Create and connect socket */
SOCKET create_and_connect_socket(const char *host, const char *port) {
    struct addrinfo hints, *res = NULL, *p;
    SOCKET sock = INVALID_SOCKET;
    int status;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;    // Support IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    log_message("Resolving host %s:%s", host, port);
    if ((status = getaddrinfo(host, port, &hints, &res)) != 0) {
        log_message("getaddrinfo failed: %s", gai_strerror(status));
        return INVALID_SOCKET;
    }

    // Try each address until we successfully connect
    for (p = res; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == INVALID_SOCKET) {
            log_message("Socket creation failed: %d", errno);
            continue;
        }

        if (set_socket_timeout(sock, CONNECT_TIMEOUT_SECONDS) < 0) {
            closesocket(sock);
            continue;
        }

        log_message("Attempting to connect...");
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
            break;  // Success
        }

        log_message("Connection failed: %d", errno);
        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    freeaddrinfo(res);

    if (sock == INVALID_SOCKET) {
        log_message("Unable to connect to server");
    }
    return sock;
}

/* Send data to server */
int send_data(SOCKET sock, const char *data) {
    size_t len = strlen(data);
    ssize_t sent = send(sock, data, len, 0);
    
    if (sent < 0) {
        log_message("Send failed: %d", errno);
        return -1;
    }
    if ((size_t)sent < len) {
        log_message("Partial send: %zd of %zu bytes", sent, len);
        return -1;
    }
    
    log_message("Sent %zd bytes", sent);
    return 0;
}

/* Receive and process server response */
int receive_data(SOCKET sock) {
    char buffer[MAX_BUFFER];
    ssize_t received;

    while ((received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[received] = '\0';
        if (fwrite(buffer, 1, received, stdout) != (size_t)received) {
            log_message("Error writing to stdout");
            return -1;
        }
        fflush(stdout);
    }

    if (received < 0) {
        log_message("Receive failed: %d", errno);
        return -1;
    }
    
    log_message("Connection closed by server");
    return 0;
}

int main(int argc, char *argv[]) {
    const char *host = (argc > 1) ? argv[1] : DEFAULT_HOST;
    const char *port = (argc > 2) ? argv[2] : DEFAULT_PORT;
    SOCKET sock = INVALID_SOCKET;
    int ret = 0;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log_message("WSAStartup failed: %d", WSAGetLastError());
        return 1;
    }
#endif

    sock = create_and_connect_socket(host, port);
    if (sock == INVALID_SOCKET) {
        ret = 1;
        goto cleanup;
    }

    // Send initial message
    char message[MAX_BUFFER];
    snprintf(message, sizeof(message), "arafat shaik\r\n");
    if (send_data(sock, message) < 0) {
        ret = 1;
        goto cleanup;
    }

    // Receive and process server response
    if (receive_data(sock) < 0) {
        ret = 1;
        goto cleanup;
    }

cleanup:
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
#ifdef _WIN32
    cleanup_winsock(&wsaData);
#endif
    return ret;
}
