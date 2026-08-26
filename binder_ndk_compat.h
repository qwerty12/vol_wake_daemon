#ifndef BINDER_NDK_COMPAT_H
#define BINDER_NDK_COMPAT_H

#include <android/binder_manager.h>
#include <android/binder_process.h>

#ifndef HAVE_LOCAL_LIBBINDER_NDK

#define BINDER_NDK_COMPAT_SYMS(X) \
    X(AServiceManager_getService) \
    X(ABinderProcess_setThreadPoolMaxThreadCount)

#ifdef BINDER_NDK_COMPAT_IMPL
#define BINDER_NDK_COMPAT_DECL(name) typeof(name) *p_##name;
#else
#define BINDER_NDK_COMPAT_DECL(name) extern typeof(name) *p_##name;
#endif

BINDER_NDK_COMPAT_SYMS(BINDER_NDK_COMPAT_DECL)
#undef BINDER_NDK_COMPAT_DECL

#ifndef BINDER_NDK_COMPAT_IMPL
#define AServiceManager_getService p_AServiceManager_getService
#define ABinderProcess_setThreadPoolMaxThreadCount p_ABinderProcess_setThreadPoolMaxThreadCount
#endif /*!BINDER_NDK_COMPAT_IMPL*/

#endif /*!HAVE_LOCAL_LIBBINDER_NDK*/
#endif /*BINDER_NDK_COMPAT_H*/