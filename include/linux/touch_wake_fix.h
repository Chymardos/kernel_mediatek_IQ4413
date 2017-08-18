#ifndef _LINUX_TOUCH_WAKE_FIX_H
#define _LINUX_TOUCH_WAKE_FIX_H

extern int screen_state; // If screen on return 1; else screen off return 0

int cm36283_ps_check(void); // If proximity sensor no close return 1; else return 0

int touch_vibrate_set(void);

#endif // _LINUX_TOUCH_WAKE_FIX_H
