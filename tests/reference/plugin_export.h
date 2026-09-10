
#ifndef PLUGIN_EXPORT_H
#define PLUGIN_EXPORT_H

#ifdef PLUGIN_STATIC_DEFINE
#  define PLUGIN_EXPORT
#  define PLUGIN_NO_EXPORT
#else
#  ifndef PLUGIN_EXPORT
#    if defined(_MSC_VER)
#      define PLUGIN_EXPORT __declspec(dllexport)
#    else
#      define PLUGIN_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef PLUGIN_NO_EXPORT
#    if defined(_MSC_VER)
#      define PLUGIN_NO_EXPORT
#    else
#      define PLUGIN_NO_EXPORT __attribute__((visibility("hidden")))
#    endif
#  endif
#endif

#ifndef PLUGIN_DEPRECATED
#  if defined(_MSC_VER)
#    define PLUGIN_DEPRECATED __declspec(deprecated)
#  else
#    define PLUGIN_DEPRECATED __attribute__ ((__deprecated__))
#  endif
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
