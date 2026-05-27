#ifndef SESSION_H
#define SESSION_H

#define SESSION_MAX_SESSIONS 64
#define SESSION_NAME_LEN 128

typedef enum {
    SESSION_TYPE_SSH,
    SESSION_TYPE_SERIAL
} SessionType;

typedef struct {
    char name[SESSION_NAME_LEN];
    SessionType type;
    union {
        struct {
            char host[256];
            int port;
            char username[128];
            char password[256];
        } ssh;
        struct {
            char portName[32];
            int baudRate;
            int dataBits;
            int parity;
            int stopBits;
        } serial;
    };
} SavedSession;

typedef struct {
    SavedSession sessions[SESSION_MAX_SESSIONS];
    int count;
} SessionStore;

void session_store_init(SessionStore *store);
void session_store_load(SessionStore *store);
void session_store_save(const SessionStore *store);
int session_store_add(SessionStore *store, const SavedSession *session);
void session_store_remove(SessionStore *store, int index);
const char *session_get_config_path(void);

#endif
