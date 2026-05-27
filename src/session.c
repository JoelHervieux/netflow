#include "session.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

static char configPath[MAX_PATH] = {0};

const char *session_get_config_path(void) {
    if (configPath[0]) return configPath;

    char appdata[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
        snprintf(configPath, MAX_PATH, "%s\\NetFlow", appdata);
        CreateDirectoryA(configPath, NULL);
    } else {
        strncpy(configPath, ".", MAX_PATH);
    }
    return configPath;
}

static void get_sessions_file(char *out, int outLen) {
    snprintf(out, outLen, "%s\\sessions.dat", session_get_config_path());
}

void session_store_init(SessionStore *store) {
    memset(store, 0, sizeof(SessionStore));
}

void session_store_load(SessionStore *store) {
    char path[MAX_PATH];
    get_sessions_file(path, MAX_PATH);

    FILE *f = fopen(path, "rb");
    if (!f) { store->count = 0; return; }

    int count = 0;
    fread(&count, sizeof(int), 1, f);
    if (count < 0 || count > SESSION_MAX_SESSIONS) count = 0;
    store->count = count;

    for (int i = 0; i < count; i++) {
        fread(&store->sessions[i], sizeof(SavedSession), 1, f);
    }
    fclose(f);
}

void session_store_save(const SessionStore *store) {
    char path[MAX_PATH];
    get_sessions_file(path, MAX_PATH);

    FILE *f = fopen(path, "wb");
    if (!f) return;

    fwrite(&store->count, sizeof(int), 1, f);
    for (int i = 0; i < store->count; i++) {
        fwrite(&store->sessions[i], sizeof(SavedSession), 1, f);
    }
    fclose(f);
}

int session_store_add(SessionStore *store, const SavedSession *session) {
    for (int i = 0; i < store->count; i++) {
        if (strcmp(store->sessions[i].name, session->name) == 0) {
            store->sessions[i] = *session;
            session_store_save(store);
            return i;
        }
    }

    if (store->count >= SESSION_MAX_SESSIONS) return -1;
    store->sessions[store->count] = *session;
    store->count++;
    session_store_save(store);
    return store->count - 1;
}

void session_store_remove(SessionStore *store, int index) {
    if (index < 0 || index >= store->count) return;
    for (int i = index; i < store->count - 1; i++) {
        store->sessions[i] = store->sessions[i + 1];
    }
    store->count--;
    session_store_save(store);
}
