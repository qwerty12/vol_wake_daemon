#ifndef ISINTERACTIVE_H
#define ISINTERACTIVE_H

#include <stdbool.h>
#include <sys/cdefs.h>

__LIBC_HIDDEN__ bool ConnectPowerService(void) __wur;
__LIBC_HIDDEN__ int  IsInteractive(void) __wur;

#endif /*ISINTERACTIVE_H*/
