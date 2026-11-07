/* Bootloader entry B (ch32v203_hal.c hal_reset_to_bootloader): before anything
   else, a mark in RAM sends the reset straight to system flash. The mark is
   cleared first so the next reset starts the application. */
	li t0, 0x20001810
	lw t1, 0(t0)
	li t2, 0xB0075EED
	bne t1, t2, 9f
	sw zero, 0(t0)
	li t0, 0x1FFF8000
	jr t0
9:
