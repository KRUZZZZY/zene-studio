
#ifndef CARLABASE_EXPORT_H
#define CARLABASE_EXPORT_H

#ifdef CARLABASE_STATIC_DEFINE
#  define CARLABASE_EXPORT
#  define CARLABASE_NO_EXPORT
#else
#  ifndef CARLABASE_EXPORT
#    ifdef carlabase_EXPORTS
        /* We are building this library */
#      define CARLABASE_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define CARLABASE_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef CARLABASE_NO_EXPORT
#    define CARLABASE_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef CARLABASE_DEPRECATED
#  define CARLABASE_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef CARLABASE_DEPRECATED_EXPORT
#  define CARLABASE_DEPRECATED_EXPORT CARLABASE_EXPORT CARLABASE_DEPRECATED
#endif

#ifndef CARLABASE_DEPRECATED_NO_EXPORT
#  define CARLABASE_DEPRECATED_NO_EXPORT CARLABASE_NO_EXPORT CARLABASE_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef CARLABASE_NO_DEPRECATED
#    define CARLABASE_NO_DEPRECATED
#  endif
#endif

#endif /* CARLABASE_EXPORT_H */
