#include "serial.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setupapi.h>
#include <devguid.h>

static DWORD WINAPI serial_read_thread(LPVOID param) {
    SerialConnection *conn = (SerialConnection *)param;
    char buf[1024];
    DWORD bytesRead;

    while (conn->running) {
        BOOL ok = ReadFile(conn->hPort, buf, sizeof(buf), &bytesRead, NULL);
        if (ok && bytesRead > 0) {
            if (conn->onData) conn->onData(buf, (int)bytesRead, conn->userdata);
        } else if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED) continue;
            break;
        }
    }

    conn->running = FALSE;
    if (conn->onClose) conn->onClose(conn->userdata);
    return 0;
}

SerialConnection *serial_connect(const char *portName, int baudRate, int dataBits, int parity, int stopBits) {
    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "\\\\.\\%s", portName);

    HANDLE hPort = CreateFileA(fullPath,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (hPort == INVALID_HANDLE_VALUE) return NULL;

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(hPort, &dcb);
    dcb.BaudRate = baudRate;
    dcb.ByteSize = (BYTE)dataBits;
    dcb.Parity = (BYTE)parity;
    dcb.StopBits = (stopBits == 2) ? TWOSTOPBITS : ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = (parity != NOPARITY);
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(hPort, &dcb)) {
        CloseHandle(hPort);
        return NULL;
    }

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 10;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hPort, &timeouts);

    SerialConnection *conn = calloc(1, sizeof(SerialConnection));
    conn->hPort = hPort;
    conn->running = TRUE;
    conn->baudRate = baudRate;
    conn->dataBits = dataBits;
    conn->parity = parity;
    conn->stopBits = stopBits;
    strncpy(conn->portName, portName, sizeof(conn->portName) - 1);

    conn->thread = CreateThread(NULL, 0, serial_read_thread, conn, 0, NULL);
    return conn;
}

void serial_disconnect(SerialConnection *conn) {
    if (!conn) return;
    conn->running = FALSE;
    CancelIoEx(conn->hPort, NULL);
    if (conn->thread) {
        WaitForSingleObject(conn->thread, 2000);
        CloseHandle(conn->thread);
    }
    CloseHandle(conn->hPort);
    free(conn);
}

void serial_write(SerialConnection *conn, const char *data, int len) {
    if (!conn || !conn->running) return;
    DWORD written;
    WriteFile(conn->hPort, data, len, &written, NULL);
}

void serial_set_callbacks(SerialConnection *conn, SerialDataCallback onData, SerialCloseCallback onClose, void *userdata) {
    if (!conn) return;
    conn->onData = onData;
    conn->onClose = onClose;
    conn->userdata = userdata;
}

int serial_list_ports(char ports[][32], int maxPorts) {
    int count = 0;
    for (int i = 1; i <= 256 && count < maxPorts; i++) {
        char name[16];
        char fullPath[32];
        snprintf(name, sizeof(name), "COM%d", i);
        snprintf(fullPath, sizeof(fullPath), "\\\\.\\COM%d", i);

        HANDLE h = CreateFileA(fullPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            strncpy(ports[count], name, 31);
            ports[count][31] = 0;
            count++;
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_ACCESS_DENIED) {
                strncpy(ports[count], name, 31);
                ports[count][31] = 0;
                count++;
            }
        }
    }
    return count;
}
