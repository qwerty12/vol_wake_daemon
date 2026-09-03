#ifndef ISINTERACTIVE_H
#define ISINTERACTIVE_H

#include <stdbool.h>
#include <sys/cdefs.h>

bool ConnectPowerService(void) __wur;
int  IsInteractive(void) __wur;

#endif /*ISINTERACTIVE_H*/
