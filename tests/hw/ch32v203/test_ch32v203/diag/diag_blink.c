// Diagnostic only: no USB. Blinks PC13 at about 1 Hz so the MCU run state can be seen.
#include <stdint.h>

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018u)
#define GPIOC_CFGHR (*(volatile uint32_t *)0x40011004u)
#define GPIOC_BSHR  (*(volatile uint32_t *)0x40011010u)
#define GPIOC_BCR   (*(volatile uint32_t *)0x40011014u)

int main(void) {
  RCC_APB2ENR |= (1u << 4); // IOPCEN
  GPIOC_CFGHR &= ~(0xFu << 20);
  GPIOC_CFGHR |= (0x2u << 20); // PC13 output push-pull 2 MHz

  while (1) {
    GPIOC_BSHR = (1u << 13);
    for (volatile uint32_t i = 0; i < 2000000u; i++) {}
    GPIOC_BCR = (1u << 13);
    for (volatile uint32_t i = 0; i < 2000000u; i++) {}
  }
}
