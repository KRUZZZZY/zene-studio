
#ifndef LMMS_EXPORT_H
#define LMMS_EXPORT_H

#ifdef LMMS_STATIC_DEFINE
#  define LMMS_EXPORT
#  define LMMS_NO_EXPORT
#else
#  ifndef LMMS_EXPORT
#    ifdef lmmsobjs_EXPORTS
        /* We are building this library */
#      define LMMS_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define LMMS_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef LMMS_NO_EXPORT
#    define LMMS_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef LMMS_DEPRECATED
#  define LMMS_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef LMMS_DEPRECATED_EXPORT
#  define LMMS_DEPRECATED_EXPORT LMMS_EXPORT LMMS_DEPRECATED
#endif

#ifndef LMMS_DEPRECATED_NO_EXPORT
#  define LMMS_DEPRECATED_NO_EXPORT LMMS_NO_EXPORT LMMS_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef LMMS_NO_DEPRECATED
#    define LMMS_NO_DEPRECATED
#  endif
#endif

#endif /* LMMS_EXPORT_H */
