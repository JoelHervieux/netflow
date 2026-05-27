#ifndef SERIAL_H
#define SERIAL_H

#include <windows.h>

typedef void (*SerialDataCallback)(const char *data, int len, void *userdata);
typedef void (*SerialCloseCallback)(void *userdata);

typedef struct {
    HANDLE hPort;
    HANDLE thread;
    volatile BOOL running;
    SerialDataCallback onData;
    SerialCloseCallback onClose;
    void *userdata;
    char portName[32];
    int baudRate;
    int dataBits;
    int parity;
    int stopBits;
} SerialConnection;

SerialConnection *serial_connect(const char *portName, int baudRate, int dataBits, int parity, int stopBits);
void serial_disconnect(SerialConnection *conn);
void serial_write(SerialConnection *conn, const char *data, int len);
void serial_set_callbacks(SerialConnection *conn, SerialDataCallback onData, SerialCloseCallback onClose, void *userdata);

int serial_list_ports(char ports[][32], int maxPorts);

#endif
