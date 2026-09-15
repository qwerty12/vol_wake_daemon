#ifndef CLEANUP_H
#define CLEANUP_H

#include <sys/cdefs.h>

#define DEFINE_AUTOPTR_CLEANUP(TypeName, func)                                   \
    static __always_inline inline void _autoptr_cleanup_##TypeName(TypeName * const *_pp) \
    {                                                                            \
        if (*_pp) func(*_pp);                                                    \
    }
#define autoptr(TypeName) __attribute__((cleanup(_autoptr_cleanup_##TypeName))) TypeName*

#define DEFINE_AUTOVAL_CLEANUP(TypeName, func, none_value)                       \
    static __always_inline inline void _autoval_cleanup_##TypeName(const TypeName *_p) \
    {                                                                            \
        if (*_p != (none_value)) func(*_p);                                      \
    }
#define autoval(TypeName) __attribute__((cleanup(_autoval_cleanup_##TypeName))) TypeName

#endif /*CLEANUP_H*/
