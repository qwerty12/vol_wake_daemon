#ifndef ISINTERACTIVE_H
#define ISINTERACTIVE_H

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

bool ConnectPowerService(void);
int  IsInteractive(void);

#ifdef __cplusplus
}
#endif

#endif /*ISINTERACTIVE_H*/
