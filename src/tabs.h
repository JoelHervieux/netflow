#ifndef TABS_H
#define TABS_H

#include <winsock2.h>
#include <windows.h>
#include "terminal.h"
#include "ui.h"

/* Forward decls for connection types — your project's headers */
#include "ssh.h"
#include "serial.h"

#define MAX_TABS  16

typedef enum {
    TAB_TYPE_SSH,
    TAB_TYPE_SERIAL,
} TabType;

typedef union {
    SshConnection    *ssh;
    SerialConnection *serial;
} TabConn;

typedef struct {
    BOOL      active;
    TabType   type;
    char      title[128];
    Terminal *terminal;
    TabConn   conn;

    /* Animation state — drive from a global 60Hz timer */
    UiAnim    hover;        /* 0..1, chases mgr->hoverTab membership */
    UiAnim    underline;    /* 0..1, chases mgr->activeTab membership */
} Tab;

typedef struct {
    HWND parentHwnd;
    int  tabBarHeight;
    Tab  tabs[MAX_TABS];
    int  count;
    int  activeTab;

    /* Live hover state, updated by WM_MOUSEMOVE */
    int  hoverTab;
    int  hoverClose;
    BOOL hoverPlus;
} TabManager;

void  tabs_init       (TabManager *mgr, HWND parent);
int   tabs_add        (TabManager *mgr, TabType type, const char *title);
void  tabs_remove     (TabManager *mgr, int index);
void  tabs_set_active (TabManager *mgr, int index);
Tab  *tabs_get_active (TabManager *mgr);

void  tabs_paint_bar      (TabManager *mgr, HDC hdc, RECT *rect);
int   tabs_hit_test       (TabManager *mgr, int x, int y);
int   tabs_hit_test_close (TabManager *mgr, int x, int y);
BOOL  tabs_hit_test_plus  (TabManager *mgr, int x, int y);

BOOL  tabs_animate (TabManager *mgr, float dt);   /* returns TRUE if redraw needed */

#endif
