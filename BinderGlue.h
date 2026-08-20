#ifndef BINDERGLUE_H
#define BINDERGLUE_H

#ifdef __cplusplus
extern "C" {
#endif

void OnBinderReadReady();
int SetupBinderOrCrash();
void wakeUpScreen();

#ifdef __cplusplus
}
#endif

#endif /*BINDERGLUE_H*/
