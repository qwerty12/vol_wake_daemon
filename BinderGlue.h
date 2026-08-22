#ifndef BINDERGLUE_H
#define BINDERGLUE_H

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

void OnBinderReadReady(void);
int  SetupBinder(void);
bool ConnectInputService(void);
void WakeUpScreen(void);

#ifdef __cplusplus
}
#endif

#endif /*BINDERGLUE_H*/
