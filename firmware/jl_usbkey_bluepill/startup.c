// Minimal Cortex-M3 startup for the STM32F103C8: vector table (core
// exceptions only, no interrupts are used), .data copy, .bss clear, main().
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

void Reset_Handler(void) {
  uint32_t *src = &_sidata, *dst = &_sdata;
  while (dst < &_edata)
    *dst++ = *src++;
  for (dst = &_sbss; dst < &_ebss;)
    *dst++ = 0;
  main();
  while (1) {
  }
}

void Default_Handler(void) {
  while (1) {
  }
}

__attribute__((section(".isr_vector"), used)) static void (*const vectors[16])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    Default_Handler, // NMI
    Default_Handler, // HardFault
    Default_Handler, // MemManage
    Default_Handler, // BusFault
    Default_Handler, // UsageFault
    0, 0, 0, 0,
    Default_Handler, // SVCall
    Default_Handler, // DebugMon
    0,
    Default_Handler, // PendSV
    Default_Handler, // SysTick (not enabled)
};
