#pragma once
#include <windows.h>
// Keep a passive companion out of unrelated applications' foreground space.
inline bool panelOwnsForeground(HWND game) {
    DWORD gamePid=0, foregroundPid=0;
    GetWindowThreadProcessId(game,&gamePid);
    HWND foreground=GetForegroundWindow();
    if(foreground) GetWindowThreadProcessId(foreground,&foregroundPid);
    return foreground && gamePid && foregroundPid==gamePid;
}
