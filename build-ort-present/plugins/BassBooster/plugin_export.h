
#ifndef PLUGIN_EXPORT_H
#define PLUGIN_EXPORT_H

#ifdef PLUGIN_STATIC_DEFINE
#  define PLUGIN_EXPORT
#  define PLUGIN_NO_EXPORT
#else
#  ifndef PLUGIN_EXPORT
#    ifdef bassbooster_EXPORTS
        /* We are building this library */
#      define PLUGIN_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define PLUGIN_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef PLUGIN_NO_EXPORT
#    define PLUGIN_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef PLUGIN_DEPRECATED
#  define PLUGIN_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef PLUGIN_DEPRECATED_EXPORT
#  define PLUGIN_DEPRECATED_EXPORT PLUGIN_EXPORT PLUGIN_DEPRECATED
#endif

#ifndef PLUGIN_DEPRECATED_NO_EXPORT
#  define PLUGIN_DEPRECATED_NO_EXPORT PLUGIN_NO_EXPORT PLUGIN_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef PLUGIN_NO_DEPRECATED
#    define PLUGIN_NO_DEPRECATED
#  endif
#endif

#endif /* PLUGIN_EXPORT_H */
