#include "window.h"
#include "ssh.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int cmdShow) {
    (void)hPrev;
    (void)cmdLine;
    (void)cmdShow;

    ssh_init();

    AppWindow *app = window_create(hInst);
    window_run(app);

    ssh_cleanup();
    return 0;
}
