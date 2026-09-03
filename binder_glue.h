#ifndef BINDERGLUE_H
#define BINDERGLUE_H

#include <sys/cdefs.h>

void OnBinderReadReady(void);
int  SetupBinder(void) __wur;

#endif /*BINDERGLUE_H*/
