#ifndef BINDERGLUE_H
#define BINDERGLUE_H

#include <sys/cdefs.h>

__LIBC_HIDDEN__ void OnBinderReadReady(void);
__LIBC_HIDDEN__ __attribute__((noinline)) int SetupBinder(void) __wur;

#endif /*BINDERGLUE_H*/
