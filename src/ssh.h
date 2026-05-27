#ifndef SSH_H
#define SSH_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <libssh2.h>

typedef void (*SshDataCallback)(const char *data, int len, void *userdata);
typedef void (*SshCloseCallback)(void *userdata);

typedef struct {
    SOCKET sock;
    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel;
    HANDLE thread;
    volatile BOOL running;
    SshDataCallback onData;
    SshCloseCallback onClose;
    void *userdata;
    char host[256];
    int port;
    char username[128];
    char password[256];
} SshConnection;

void ssh_init(void);
void ssh_cleanup(void);

SshConnection *ssh_connect(const char *host, int port, const char *username, const char *password);
void ssh_disconnect(SshConnection *conn);
void ssh_write(SshConnection *conn, const char *data, int len);
void ssh_resize(SshConnection *conn, int cols, int rows);
void ssh_set_callbacks(SshConnection *conn, SshDataCallback onData, SshCloseCallback onClose, void *userdata);

#endif
