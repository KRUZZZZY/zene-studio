/*
 * A loadable module with no LMMS plug-in entry point.
 *
 * Q050-018 uses a synthetic scan-cache descriptor for this file to exercise
 * the case where a module was found during scanning but no longer exports
 * lmms_plugin_main when the project loads it.
 */

int q050018NoEntryModuleAnchor = 0;
