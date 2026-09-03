#ifndef HAVE_LOCAL_LIBBINDER_NDK

#define BINDER_NDK_COMPAT_IMPL
#include "binder_ndk_compat.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <sys/cdefs.h>

#define BINDER_NDK_COMPAT_RESOLVE(name) p_##name = dlsym(RTLD_DEFAULT, __STRING(name));
#define BINDER_NDK_COMPAT_CHECK(name)   !p_##name ||
#define BINDER_NDK_COMPAT_CHECK_END     0

extern __printflike(1, 2) void __log_msg(const char *fmt, ...);

__attribute__((constructor)) static void binder_ndk_compat_init(void)
{
    BINDER_NDK_COMPAT_SYMS(BINDER_NDK_COMPAT_RESOLVE)

    if (__predict_false(BINDER_NDK_COMPAT_SYMS(BINDER_NDK_COMPAT_CHECK) BINDER_NDK_COMPAT_CHECK_END)) {
        __log_msg("failed to resolve a required symbol from libbinder_ndk");
        exit(EXIT_FAILURE);
    }
}

#undef BINDER_NDK_COMPAT_RESOLVE
#undef BINDER_NDK_COMPAT_CHECK
#undef BINDER_NDK_COMPAT_CHECK_END

#endif /*!HAVE_LOCAL_LIBBINDER_NDK*/