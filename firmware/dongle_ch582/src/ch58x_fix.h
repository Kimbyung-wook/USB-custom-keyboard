// Force-included before every file (ch58x.mk -include).
// The SDK header CH58x_common.h defines __INTERRUPT with "WCH-Interrupt-fast",
// which only WCH's GCC knows. Its #ifndef lets us give the standard form.
#ifndef CH58X_FIX_H
#define CH58X_FIX_H
#define __INTERRUPT __attribute__((interrupt("machine")))
#endif
