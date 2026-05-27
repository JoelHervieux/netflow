#include "ssh.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static BOOL winsockInitialized = FALSE;

void ssh_init(void) {
    if (!winsockInitialized) {
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2, 2), &wsadata);
        libssh2_init(0);
        winsockInitialized = TRUE;
    }
}

void ssh_cleanup(void) {
    if (winsockInitialized) {
        libssh2_exit();
        WSACleanup();
        winsockInitialized = FALSE;
    }
}

static DWORD WINAPI ssh_read_thread(LPVOID param) {
    SshConnection *conn = (SshConnection *)param;
    char buf[4096];

    libssh2_session_set_blocking(conn->session, 0);

    while (conn->running) {
        int rc = (int)libssh2_channel_read(conn->channel, buf, sizeof(buf));
        if (rc > 0) {
            if (conn->onData) conn->onData(buf, rc, conn->userdata);
        } else if (rc == LIBSSH2_ERROR_EAGAIN) {
            fd_set fds;
            struct timeval tv;
            FD_ZERO(&fds);
            FD_SET(conn->sock, &fds);
            tv.tv_sec = 0;
            tv.tv_usec = 10000;
            select((int)(conn->sock + 1), &fds, NULL, NULL, &tv);
        } else if (rc < 0) {
            break;
        }

        if (libssh2_channel_eof(conn->channel)) break;
    }

    conn->running = FALSE;
    if (conn->onClose) conn->onClose(conn->userdata);
    return 0;
}

SshConnection *ssh_connect(const char *host, int port, const char *username, const char *password) {
    ssh_init();

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);

    if (getaddrinfo(host, portStr, &hints, &res) != 0) return NULL;

    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) { freeaddrinfo(res); return NULL; }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
        closesocket(sock);
        freeaddrinfo(res);
        return NULL;
    }
    freeaddrinfo(res);

    BOOL nodelay = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));

    LIBSSH2_SESSION *session = libssh2_session_init();
    if (!session) { closesocket(sock); return NULL; }

    libssh2_session_set_blocking(session, 1);
    if (libssh2_session_handshake(session, sock)) {
        libssh2_session_free(session);
        closesocket(sock);
        return NULL;
    }

    if (libssh2_userauth_password(session, username, password)) {
        libssh2_session_disconnect(session, "Auth failed");
        libssh2_session_free(session);
        closesocket(sock);
        return NULL;
    }

    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(session);
    if (!channel) {
        libssh2_session_disconnect(session, "Channel open failed");
        libssh2_session_free(session);
        closesocket(sock);
        return NULL;
    }

    libssh2_channel_request_pty(channel, "xterm-256color");
    libssh2_channel_shell(channel);

    SshConnection *conn = calloc(1, sizeof(SshConnection));
    conn->sock = sock;
    conn->session = session;
    conn->channel = channel;
    conn->running = TRUE;
    strncpy(conn->host, host, sizeof(conn->host) - 1);
    conn->port = port;
    strncpy(conn->username, username, sizeof(conn->username) - 1);
    strncpy(conn->password, password, sizeof(conn->password) - 1);

    conn->thread = CreateThread(NULL, 0, ssh_read_thread, conn, 0, NULL);
    return conn;
}

void ssh_disconnect(SshConnection *conn) {
    if (!conn) return;
    conn->running = FALSE;
    if (conn->thread) {
        WaitForSingleObject(conn->thread, 2000);
        CloseHandle(conn->thread);
    }
    if (conn->channel) {
        libssh2_channel_close(conn->channel);
        libssh2_channel_free(conn->channel);
    }
    if (conn->session) {
        libssh2_session_disconnect(conn->session, "Bye");
        libssh2_session_free(conn->session);
    }
    if (conn->sock != INVALID_SOCKET) {
        closesocket(conn->sock);
    }
    free(conn);
}

void ssh_write(SshConnection *conn, const char *data, int len) {
    if (!conn || !conn->channel || !conn->running) return;
    libssh2_session_set_blocking(conn->session, 1);
    libssh2_channel_write(conn->channel, data, len);
    libssh2_session_set_blocking(conn->session, 0);
}

void ssh_resize(SshConnection *conn, int cols, int rows) {
    if (!conn || !conn->channel || !conn->running) return;
    libssh2_session_set_blocking(conn->session, 1);
    libssh2_channel_request_pty_size(conn->channel, cols, rows);
    libssh2_session_set_blocking(conn->session, 0);
}

void ssh_set_callbacks(SshConnection *conn, SshDataCallback onData, SshCloseCallback onClose, void *userdata) {
    if (!conn) return;
    conn->onData = onData;
    conn->onClose = onClose;
    conn->userdata = userdata;
}
