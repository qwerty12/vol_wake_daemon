#ifndef HAVE_LOCAL_LIBBINDER_NDK

#define BINDER_NDK_COMPAT_IMPL
#include "binder_ndk_compat.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <sys/cdefs.h>

extern void __log_msg(const char *fmt, ...);

__attribute__((constructor)) static void binder_ndk_compat_init(void)
{
    p_AServiceManager_getService = dlsym(RTLD_DEFAULT, "AServiceManager_getService");
    p_ABinderProcess_setThreadPoolMaxThreadCount = dlsym(RTLD_DEFAULT, "ABinderProcess_setThreadPoolMaxThreadCount");

    if (__predict_false(!p_AServiceManager_getService || !p_ABinderProcess_setThreadPoolMaxThreadCount)) {
        __log_msg("failed to resolve a required symbol from libbinder_ndk");
        exit(EXIT_FAILURE);
    }
}

#endif /*!HAVE_LOCAL_LIBBINDER_NDK*/