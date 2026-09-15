/*
 * clap-test-broken.c - CLAP modules that fail in exactly ONE way each
 *
 * Built from the pinned CLAP 1.2.10 headers (MIT), one shared module per fault
 * mode, as the test subjects for the host's TYPED error path
 * (plugins/ClapEffect/ClapLoader.h, tests/src/plugins/ClapLoaderErrorTest.cpp).
 *
 * WHY ONE SOURCE, FOUR MODULES. The host's promise is that a load that fails
 * says WHICH failure it was -- no such file, not a CLAP module, a version the
 * host cannot host, an entry point that refuses to initialize, a module with no
 * plugin factory -- instead of handing a caller a null pointer or one sentence
 * that covers all five. Only a real module can prove that, and each fault has
 * to be structural: a module that does not export `clap_entry` at all cannot be
 * made to export one by a flag the test sets. So this ONE file is compiled once
 * per fault (see CMakeLists.txt in this directory):
 *
 *   CLAP_TEST_FAULT_NO_SYMBOL     loads fine, exports no `clap_entry`
 *   CLAP_TEST_FAULT_OLD_VERSION   exports `clap_entry` with clap_version.major = 0
 *                                 (the CLAP 0.x development series, which the
 *                                 host refuses by version)
 *   CLAP_TEST_FAULT_INIT_FAILS    a good, current `clap_entry` whose init()
 *                                 returns false
 *   CLAP_TEST_FAULT_NO_FACTORY    a good, current `clap_entry` whose init()
 *                                 succeeds and whose get_factory() returns null
 *
 * The faults are mutually exclusive by construction and nothing else about the
 * modules differs, so a test that sees the wrong code has found a real defect in
 * the host rather than a difference between four fixtures.
 *
 * Nothing here is LMMS code and nothing here is linked into the product: these
 * modules exist to be loaded, rejected, and unloaded.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <clap/clap.h>

#if defined(CLAP_TEST_FAULT_NO_SYMBOL)

/* -------------------------------------------------------------------------
 * NO_SYMBOL: a perfectly loadable module with no CLAP entry point in it.
 *
 * The decoy symbol is deliberate: it makes `clap_entry` the ONE name missing
 * from the export table, so a host that looked up anything else would still
 * find it, and a test that got LibraryUnavailable instead of SymbolMissing is
 * not looking at "the file could not be opened".
 * ------------------------------------------------------------------------- */
CLAP_EXPORT int clap_test_broken_decoy_symbol = 1;

#elif defined(CLAP_TEST_FAULT_OLD_VERSION)

/* -------------------------------------------------------------------------
 * OLD_VERSION: the entry point is there and is otherwise perfect; only the
 * version it declares is one this host cannot host. clap_entry.init() is
 * never called for it (the host checks the version first), which is why the
 * module can report that it was not called.
 * ------------------------------------------------------------------------- */
static bool old_version_entry_init(const char* plugin_path)
{
	(void)plugin_path;
	printf("clap-test-broken(old-version): init() called; the host was supposed to reject the version first\n");
	return true;
}

static void old_version_entry_deinit(void) {}

static const void* old_version_entry_get_factory(const char* factory_id)
{
	(void)factory_id;
	return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
	.clap_version = {0, 0, 0},
	.init = old_version_entry_init,
	.deinit = old_version_entry_deinit,
	.get_factory = old_version_entry_get_factory,
};

#elif defined(CLAP_TEST_FAULT_INIT_FAILS)

/* -------------------------------------------------------------------------
 * INIT_FAILS: a current, well-formed entry point that refuses to initialize.
 * deinit() must NOT be called for a module whose init() failed, so it says so.
 * ------------------------------------------------------------------------- */
static bool init_fails_entry_init(const char* plugin_path)
{
	(void)plugin_path;
	return false;
}

static void init_fails_entry_deinit(void)
{
	printf("clap-test-broken(init-fails): deinit() called after init() failed\n");
}

static const void* init_fails_entry_get_factory(const char* factory_id)
{
	(void)factory_id;
	return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
	.clap_version = CLAP_VERSION_INIT,
	.init = init_fails_entry_init,
	.deinit = init_fails_entry_deinit,
	.get_factory = init_fails_entry_get_factory,
};

#elif defined(CLAP_TEST_FAULT_NO_FACTORY)

/* -------------------------------------------------------------------------
 * NO_FACTORY: init() succeeds and the module has no plugin factory to offer --
 * a well-formed module that simply is not a plug-in container.
 * ------------------------------------------------------------------------- */
static bool no_factory_entry_init(const char* plugin_path)
{
	(void)plugin_path;
	return true;
}

static void no_factory_entry_deinit(void) {}

static const void* no_factory_entry_get_factory(const char* factory_id)
{
	(void)factory_id;
	return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
	.clap_version = CLAP_VERSION_INIT,
	.init = no_factory_entry_init,
	.deinit = no_factory_entry_deinit,
	.get_factory = no_factory_entry_get_factory,
};

#else
#error "CLAP_TEST_FAULT_<MODE> must be defined: exactly one fault per module"
#endif
