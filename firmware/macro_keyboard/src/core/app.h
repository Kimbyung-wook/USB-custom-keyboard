#ifndef __MK_APP_H__
#define __MK_APP_H__

// Firmware main sequence, shared by the chip entry points:
// macro_keyboard.ino (CH552, Arduino setup/loop) and hal/ch32v203/main.c
void app_setup(void);
void app_loop(void);

#endif
