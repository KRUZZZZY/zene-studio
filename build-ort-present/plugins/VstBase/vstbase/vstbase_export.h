
#ifndef VSTBASE_EXPORT_H
#define VSTBASE_EXPORT_H

#ifdef VSTBASE_STATIC_DEFINE
#  define VSTBASE_EXPORT
#  define VSTBASE_NO_EXPORT
#else
#  ifndef VSTBASE_EXPORT
#    ifdef vstbase_EXPORTS
        /* We are building this library */
#      define VSTBASE_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define VSTBASE_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef VSTBASE_NO_EXPORT
#    define VSTBASE_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef VSTBASE_DEPRECATED
#  define VSTBASE_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef VSTBASE_DEPRECATED_EXPORT
#  define VSTBASE_DEPRECATED_EXPORT VSTBASE_EXPORT VSTBASE_DEPRECATED
#endif

#ifndef VSTBASE_DEPRECATED_NO_EXPORT
#  define VSTBASE_DEPRECATED_NO_EXPORT VSTBASE_NO_EXPORT VSTBASE_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef VSTBASE_NO_DEPRECATED
#    define VSTBASE_NO_DEPRECATED
#  endif
#endif

#endif /* VSTBASE_EXPORT_H */
