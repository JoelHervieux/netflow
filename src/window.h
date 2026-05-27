#ifndef WINDOW_H
#define WINDOW_H

#include <winsock2.h>
#include <windows.h>
#include "ui.h"
#include "tabs.h"
#include "terminal.h"
#include "ssh.h"
#include "serial.h"
#include "session.h"

typedef struct {
    HWND hwnd;

    /* Tabs + connections */
    TabManager      tabs;
    SessionStore    sessions;

    /* Modal state */
    ModalState      modal;

    /* Hover state */
    int             emptyHoverBtn;   /* 0 = SSH, 1 = Serial, -1 = none */
    int             titleHoverBtn;   /* 0 = min, 1 = max, 2 = close   */

    /* Animations driven by the 60Hz timer */
    UiAnim          emptyAnim[2];    /* matches emptyHoverBtn */

    /* Font preferences */
    int             fontSize;

    /* Fullscreen */
    BOOL            fullscreen;
    WINDOWPLACEMENT wpPrev;
} AppWindow;

AppWindow *window_create (HINSTANCE hInst);
void       window_run    (AppWindow *app);

#endif
