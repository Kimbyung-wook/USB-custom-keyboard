// RISC-V (CH32V20x) only: the Arduino (CH552, SDCC) build compiles every file under src/
#ifdef __riscv
// CH32V203 entry point: the sequence in core/app.c, as macro_keyboard.ino
#include "core/app.h"

extern const char firmwareId[];

int main(void) {
  app_setup();
  (void)*(volatile const char *)firmwareId; // keeps the ID string in the image
  while (1) {
    app_loop();
  }
}

#endif // __riscv
