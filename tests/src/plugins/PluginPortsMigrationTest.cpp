/*
 * PluginPortsMigrationTest.cpp - Part C (task #585) behaviour preservation
 *
 * Loads the migrated plugin modules built by the normal build and compares
 * their output against PluginPortsMigrationReference, which renders the same
 * input through the verbatim pre-migration (base commit 4ac5c3e38) sources in
 * tests/reference/. Both sides go through the production Effect API
 * (loadSettings + processAudioBuffer(AudioBus&)), so a sample difference can
 * only come from the migration.
 *
 * Windows: no test host can load the migrated modules (an MSVC module's
 * import descriptor names zene.exe - see the note on
 * testHostCanLoadPluginModules()), so every slot in this file skips there
 * rather than failing at the load. Linux/macOS run every assertion below
 * unchanged.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "PluginPortsHarness.h"
#include "plugins/Sid/SidPlaybackScheduler.h"

#include "AudioPlugin.h"
#include "BasicFilters.h"
#include "EffectControls.h"
#include "Graph.h"
#include "RingBuffer.h"
#include "../../../plugins/MultitapEcho/MultitapEchoControlDialog.h"
#define private public
#include "../../../plugins/MultitapEcho/MultitapEcho.h"
#undef private

#include <cstddef>
#include <atomic>
#include <cmath>
#include <cmath>
#include <algorithm>
#include <cmath>
#include <algorithm>
#include <cmath>
#include <cmath>
#include <array>
#include <barrier>
#include <thread>
#include <cmath>
#include "BasicFilters.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <new>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>
#include <limits>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <new>
#include <cstdint>
#include <type_traits>
#include <map>
#include <vector>
#include <memory>

#include <QByteArray>
#include <QFile>
#ifdef Q_OS_UNIX
#include <dlfcn.h>
#endif
#include <QDomDocument>
#include <QDir>
#include <QDomDocument>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDomDocument>
#include <QLibrary>
#include <memory>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTableView>
#include <QTimer>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QWaitCondition>
#include <QDeadlineTimer>
#include <QtTest>
#include <type_traits>

#include "BasicFilters.h"
#include "../../../plugins/Xpressive/Xpressive.h"
#include "Engine.h"
#include "Ladspa2LMMS.h"
#include "DspEffectLibrary.h"
#include "EffectControls.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Ladspa2LMMS.h"
#include "AudioDevice.h"
#include "PixmapButton.h"
#include "AudioDevice.h"
#include "PluginView.h"
#include "Plugin.h"
#include "plugins/Xpressive/ExprSynth.h"
#include "Hardware.h"
#include "InstrumentView.h"
#include "NotePlayHandle.h"
#define private public
#include "../../../plugins/Lb302/Lb302.h"
#undef private

// Delay builds Lfo.cpp into a MODULE target, so include its implementation
// here to exercise the constructor directly without adding a new test target.
#include "../../../plugins/Delay/Lfo.cpp"
#include "TempoSyncKnobModel.h"
#include "../../../plugins/Xpressive/ExprSynth.h"

namespace
{

std::atomic<std::size_t> allocationSizeToPoison{0};

}

namespace array_allocation_probe
{

struct Report
{
	std::size_t allocations = 0;
	std::size_t outstanding = 0;
	bool overflow = false;
};

constexpr std::size_t MaxTrackedPointers = 16;

thread_local bool g_tracking = false;
thread_local std::size_t g_size = 0;
thread_local std::size_t g_allocations = 0;
thread_local std::size_t g_outstanding = 0;
thread_local bool g_overflow = false;
thread_local void* g_pointers[MaxTrackedPointers]{};

void start(std::size_t size)
{
	g_size = size;
	g_allocations = 0;
	g_outstanding = 0;
	g_overflow = false;
	for (auto& pointer : g_pointers) { pointer = nullptr; }
	g_tracking = true;
}

Report stop()
{
	g_tracking = false;
	const Report report{g_allocations, g_outstanding, g_overflow};
	for (auto& pointer : g_pointers) { pointer = nullptr; }
	g_outstanding = 0;
	g_allocations = 0;
	g_size = 0;
	return report;
}

inline void allocation(void* pointer, std::size_t size)
{
	if (!g_tracking || size != g_size) { return; }
	++g_allocations;
	++g_outstanding;
	for (auto& tracked : g_pointers)
	{
		if (tracked == nullptr)
		{
			tracked = pointer;
			return;
		}
	}
	g_overflow = true;
}

inline void deallocation(void* pointer)
{
	if (pointer == nullptr || g_outstanding == 0) { return; }
	for (auto& tracked : g_pointers)
	{
		if (tracked == pointer)
		{
			tracked = nullptr;
			--g_outstanding;
			return;
		}
	}
}

} // namespace array_allocation_probe

void* operator new(std::size_t size)
{
	partc::RealtimeAllocationWatch::allocation();
	if (void* allocation = std::malloc(size == 0 ? 1 : size))
	{
		auto expectedSize = size;
		if (allocationSizeToPoison.compare_exchange_strong(expectedSize, 0,
			std::memory_order_relaxed))
		{
			std::memset(allocation, 0xa5, size);
		}
		return allocation;
	}
	throw std::bad_alloc{};
}

void* operator new[](std::size_t size)
{
	void* allocation = ::operator new(size);
	array_allocation_probe::allocation(allocation, size);
	return allocation;
}

void operator delete(void* allocation) noexcept
{
	partc::RealtimeAllocationWatch::deallocation();
	std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept
{
	::operator delete(allocation);
}

void operator delete[](void* allocation) noexcept
{
	array_allocation_probe::deallocation(allocation);
	::operator delete(allocation);
}

void operator delete[](void* allocation, std::size_t) noexcept
{
	array_allocation_probe::deallocation(allocation);
	::operator delete(allocation);
}

// ---------------------------------------------------------------------------
// The heap traffic of the rendering thread.
//
// The operator new above does NOT see the growth of a Qt container: Qt
// allocates the storage of a QList through malloc, inside QtCore, and that
// allocation never reaches the replaced operator new. The C allocator is
// replaced here as well, so that the growth of a container on the audio path
// is counted too.
// ---------------------------------------------------------------------------
namespace
{

struct HeapWatch
{
	inline static thread_local bool active = false;
	inline static thread_local std::size_t allocations = 0;
	inline static thread_local std::size_t deallocations = 0;

	static void begin() noexcept
	{
		allocations = 0;
		deallocations = 0;
		active = true;
	}

	static void end() noexcept { active = false; }

	static void allocated() noexcept { if (active) { ++allocations; } }
	static void deallocated() noexcept { if (active) { ++deallocations; } }
};

} // namespace

#ifdef Q_OS_UNIX

namespace
{

using MallocFn = void* (*)(std::size_t);
using CallocFn = void* (*)(std::size_t, std::size_t);
using ReallocFn = void* (*)(void*, std::size_t);
using FreeFn = void (*)(void*);

MallocFn g_malloc = nullptr;
CallocFn g_calloc = nullptr;
ReallocFn g_realloc = nullptr;
FreeFn g_free = nullptr;

// The allocations that happen while the real allocator is looked up are
// served from this static arena, so that the lookup cannot recurse.
constexpr std::size_t ArenaSize = 256 * 1024;
alignas(16) char g_arena[ArenaSize];
std::size_t g_arenaUsed = 0;
bool g_resolving = false;

void* arenaAlloc(std::size_t size)
{
	const std::size_t aligned = (size + 15u) & ~static_cast<std::size_t>(15u);
	if (g_arenaUsed + aligned > ArenaSize) { return nullptr; }
	void* const pointer = g_arena + g_arenaUsed;
	g_arenaUsed += aligned;
	return pointer;
}

bool isArenaPointer(const void* pointer)
{
	return pointer >= g_arena && pointer < g_arena + ArenaSize;
}

void resolveAllocators()
{
	if (g_resolving) { return; }
	g_resolving = true;
	g_malloc = reinterpret_cast<MallocFn>(dlsym(RTLD_NEXT, "malloc"));
	g_calloc = reinterpret_cast<CallocFn>(dlsym(RTLD_NEXT, "calloc"));
	g_realloc = reinterpret_cast<ReallocFn>(dlsym(RTLD_NEXT, "realloc"));
	g_free = reinterpret_cast<FreeFn>(dlsym(RTLD_NEXT, "free"));
	g_resolving = false;
}

} // namespace

extern "C" void* malloc(std::size_t size)
{
	if (g_resolving) { return arenaAlloc(size); }
	if (g_malloc == nullptr) { resolveAllocators(); }
	HeapWatch::allocated();
	return g_malloc(size);
}

extern "C" void* calloc(std::size_t count, std::size_t size)
{
	if (g_resolving) { return arenaAlloc(count * size); }
	if (g_calloc == nullptr) { resolveAllocators(); }
	HeapWatch::allocated();
	return g_calloc(count, size);
}

extern "C" void* realloc(void* pointer, std::size_t size)
{
	if (g_resolving) { return arenaAlloc(size); }
	if (g_realloc == nullptr) { resolveAllocators(); }
	if (isArenaPointer(pointer))
	{
		void* const fresh = g_malloc(size);
		std::memcpy(fresh, pointer, size);
		return fresh;
	}
	HeapWatch::allocated();
	return g_realloc(pointer, size);
}

extern "C" void free(void* pointer)
{
	if (pointer == nullptr || isArenaPointer(pointer)) { return; }
	HeapWatch::deallocated();
	if (g_free == nullptr) { resolveAllocators(); }
	g_free(pointer);
}

#endif // Q_OS_UNIX
// The audio-path lock regression drives the effect's own private processLock()/
// processUnlock(). A non-recursive QMutex cannot be acquired twice by the same
// thread, so a second successful claim is the deterministic evidence that the
// audio path takes no exclusive mutex - no thread, sleep or clock is involved.
#define private public
#include "../../../plugins/LadspaEffect/LadspaEffect.h"
#undef private
#include "LmmsTypes.h"
// Check ring bounds directly before the stale-bound path can reach undefined behavior.
#define private public
#include "../../../plugins/Flanger/MonoDelay.h"
#undef private
#include "../../../plugins/Flanger/MonoDelay.cpp"
#include "PluginView.h"
#include "PluginView.h"
#include "RmsHelper.h"
#include "PluginView.h"
#include "../../../plugins/Sf2Player/PatchPresetIteration.h"
#include "../../../plugins/Xpressive/Xpressive.h"
#include "../../../plugins/Xpressive/ExprSynth.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "../../../plugins/FrequencyShifter/FrequencyShifterEffect.h"
#include "AudioPlugin.h"
#include "../../../plugins/DualFilter/DualFilterControls.h"
#define private public
#include "../../../plugins/DualFilter/DualFilter.h"
#undef private

namespace
{

using MainFn = lmms::Plugin* (*)(lmms::Model*, void*);

/*!
 * Captures the log messages the OWNING thread emits while the capture is
 * alive, so a test can prove that a path logs nothing. The messages are
 * counted and the first one is kept; they are not printed, so a failing run
 * stays readable. The state is thread-local: a message from another thread
 * (the render thread) is neither recorded nor raced on.
 */
class LogCapture
{
public:
	LogCapture() : m_previous(qInstallMessageHandler(&LogCapture::handler))
	{
		s_count = 0;
		s_first.clear();
	}
	~LogCapture() { qInstallMessageHandler(m_previous); }
	LogCapture(const LogCapture&) = delete;
	LogCapture& operator=(const LogCapture&) = delete;

	int count() const { return s_count; }
	QString first() const { return s_first; }

private:
	static void handler(QtMsgType, const QMessageLogContext&, const QString& message)
	{
		++s_count;
		if (s_first.isEmpty()) { s_first = message; }
	}

	inline static thread_local int s_count = 0;
	inline static thread_local QString s_first;
	QtMessageHandler m_previous = nullptr;
};

struct MigratedModule
{
	const char* plugin;
	const char* path;
};

/*! Paths injected by tests/CMakeLists.txt via $<TARGET_FILE:...>. */
auto migratedModules() -> const std::vector<MigratedModule>&
{
	static const std::vector<MigratedModule> modules{
		{"amplifier", PART_C_MIGRATED_amplifier},
		{"bassbooster", PART_C_MIGRATED_bassbooster},
		{"bitcrush", PART_C_MIGRATED_bitcrush},
		{"dualfilter", PART_C_MIGRATED_dualfilter},
		{"waveshaper", PART_C_MIGRATED_waveshaper},
		{"flanger", PART_C_MIGRATED_flanger},
		{"delay", PART_C_MIGRATED_delay},
		{"compressor", PART_C_MIGRATED_compressor},
		{"crossovereq", PART_C_MIGRATED_crossovereq},
		{"dynamicsprocessor", PART_C_MIGRATED_dynamicsprocessor},
		{"lomm", PART_C_MIGRATED_lomm},
		{"multitapecho", PART_C_MIGRATED_multitapecho},
		{"reverbsc", PART_C_MIGRATED_reverbsc},
		{"stereoenhancer", PART_C_MIGRATED_stereoenhancer},
		{"stereomatrix", PART_C_MIGRATED_stereomatrix},
		// Slice 3 (task #589)
		{"dispersion", PART_C_MIGRATED_dispersion},
		{"vectorscope", PART_C_MIGRATED_vectorscope},
		{"analyzer", PART_C_MIGRATED_analyzer},
		{"granularpitchshifter", PART_C_MIGRATED_granularpitchshifter},
		{"eq", PART_C_MIGRATED_eq},
		// Slice 7 (task #589)
		{"frequencyshifter", PART_C_MIGRATED_frequencyshifter},
		{"oscilloscope", PART_C_MIGRATED_oscilloscope},
		{"slewdistortion", PART_C_MIGRATED_slewdistortion},
	};
	return modules;
}

struct MigratedInstrument
{
	const char* plugin;
	const char* path;
};

/*! Instrument paths injected by tests/CMakeLists.txt via $<TARGET_FILE:...>. */
auto migratedInstruments() -> const std::vector<MigratedInstrument>&
{
	static const std::vector<MigratedInstrument> instruments{
		{"freeboy", PART_C_MIGRATED_freeboy},
		{"nes", PART_C_MIGRATED_nes},
		{"sid", PART_C_MIGRATED_sid},
		{"opulenz", PART_C_MIGRATED_opulenz},
		{"sfxr", PART_C_MIGRATED_sfxr},
		{"bitinvader", PART_C_MIGRATED_bitinvader},
		{"watsyn", PART_C_MIGRATED_watsyn},
		{"xpressive", PART_C_MIGRATED_xpressive},
		{"vibedstrings", PART_C_MIGRATED_vibedstrings},
		{"kicker", PART_C_MIGRATED_kicker},
		// Slice 5 (task #589)
		{"tripleoscillator", PART_C_MIGRATED_tripleoscillator},
		{"monstro", PART_C_MIGRATED_monstro},
		{"organic", PART_C_MIGRATED_organic},
		{"audiofileprocessor", PART_C_MIGRATED_audiofileprocessor},
		// Slice 6 (task #589)
		{"lb302", PART_C_MIGRATED_lb302},
		// Slice 8 (task #589): the asset-driven instruments. An entry exists
		// only when its CMake module was added, i.e. when the build has the
		// dependency AND the asset the harness needs to render it.
#ifdef PART_C_MIGRATED_sf2player
		{"sf2player", PART_C_MIGRATED_sf2player},
#endif
#ifdef PART_C_MIGRATED_malletsstk
		{"malletsstk", PART_C_MIGRATED_malletsstk},
#endif
#ifdef PART_C_MIGRATED_gigplayer
		{"gigplayer", PART_C_MIGRATED_gigplayer},
#endif
	};
	return instruments;
}

//! Finds a render by plugin name (the migrated and reference lists are
//! index-aligned, but the negative control looks modules up by name).
auto findRender(const std::vector<partc::PluginRender>& renders, const char* name)
	-> const partc::PluginRender*
{
	for (const auto& r : renders)
	{
		if (r.name == name)
		{
			return &r;
		}
	}
	return nullptr;
}

struct NesNoiseRender
{
	QString error;
	std::vector<float> samples;
};

// Single-streamed instrument whose play() heap-allocates each period, as a plugin
// does when it builds per-period scratch storage. Proves the realtime watch covers play().
class AllocatingSingleStreamInstrument final : public lmms::Instrument
{
public:
	explicit AllocatingSingleStreamInstrument(lmms::InstrumentTrack* track) :
		Instrument(track, nullptr, nullptr, Instrument::Flag::IsSingleStreamed)
	{
	}

	QString nodeName() const override { return QStringLiteral("allocatingsinglestreaminstrument"); }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
	lmms::gui::PluginView* instantiateView(QWidget*) override { return nullptr; }

	void play(lmms::SampleFrame*) override
	{
		m_scratch = std::make_unique<float[]>(ScratchSamples);
	}

private:
	static constexpr std::size_t ScratchSamples = 64;
	std::unique_ptr<float[]> m_scratch;
};

void renderInstrumentPeriods(lmms::Instrument& instrument, lmms::NotePlayHandle& note,
	lmms::f_cnt_t frames, int periods, std::vector<float>* output)
{
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	for (int period = 0; period < periods; ++period)
	{
		std::fill(buffer.begin(), buffer.end(), lmms::SampleFrame{0.0f, 0.0f});
		instrument.playNote(&note, std::span<lmms::SampleFrame>{buffer});
		if (output == nullptr) { continue; }
		for (const auto& sample : buffer)
		{
			output->push_back(sample.left());
			output->push_back(sample.right());
		}
	}
}

NesNoiseRender renderNesNoiseTail(bool noteFrequencyMode, bool quantized,
	bool toggleNoteFrequencyMode, bool toggleQuantization)
{
	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_nes)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	if (!library.load()) { return {library.errorString(), {}}; }
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	if (entry == nullptr) { return {library.errorString(), {}}; }

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	if (!instrument) { return {QStringLiteral("NES instrument creation failed"), {}}; }

	QDomDocument settingsDocument;
	QDomElement settingsRoot = settingsDocument.createElement(QStringLiteral("instrument"));
	QDomElement settings = instrument->saveState(settingsDocument, settingsRoot);
	const auto applySettings = [&](bool useNoteFrequency, bool useQuantization)
	{
		settings.setAttribute(QStringLiteral("on1"), 0);
		settings.setAttribute(QStringLiteral("on2"), 0);
		settings.setAttribute(QStringLiteral("on3"), 0);
		settings.setAttribute(QStringLiteral("on4"), 1);
		settings.setAttribute(QStringLiteral("vol4"), 15);
		settings.setAttribute(QStringLiteral("envon4"), 0);
		settings.setAttribute(QStringLiteral("nmode4"), 0);
		settings.setAttribute(QStringLiteral("nfrqmode4"), useNoteFrequency ? 1 : 0);
		settings.setAttribute(QStringLiteral("nfreq4"), 0);
		settings.setAttribute(QStringLiteral("nq4"), useQuantization ? 1 : 0);
		settings.setAttribute(QStringLiteral("nswp4"), 0);
		settings.setAttribute(QStringLiteral("vol"), 100);
		settings.setAttribute(QStringLiteral("vibr"), 0);
		instrument->restoreState(settings);
	};
	applySettings(noteFrequencyMode, quantized);

	constexpr int PrefixPeriods = 8;
	constexpr int TailPeriods = 24;
	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const int totalPeriods = PrefixPeriods + TailPeriods;
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * totalPeriods)},
		lmms::TimePos{0}, 60};
	lmms::NotePlayHandle noteHandle{track.get(), 0, frames * totalPeriods, note, nullptr,
		-1, lmms::NotePlayHandle::Origin::MidiClip};
	renderInstrumentPeriods(*instrument, noteHandle, frames, PrefixPeriods, nullptr);
	if (toggleNoteFrequencyMode || toggleQuantization)
	{
		applySettings(noteFrequencyMode || toggleNoteFrequencyMode, quantized || toggleQuantization);
	}

	NesNoiseRender result;
	result.samples.reserve(static_cast<std::size_t>(TailPeriods) * frames * 2);
	renderInstrumentPeriods(*instrument, noteHandle, frames, TailPeriods, &result.samples);
	if (noteHandle.m_pluginData != nullptr)
	{
		instrument->deleteNotePluginData(&noteHandle);
		noteHandle.m_pluginData = nullptr;
	}
	return result;
}

/*!
 * Windows: a test host cannot load a plugin MODULE library at runtime.
 *
 * Every plugin module links the zene executable, so an MSVC module's import
 * descriptor names zene.exe; the Windows loader then fails with
 * ERROR_MOD_NOT_FOUND (126) because the test host is not zene.exe. CI
 * (msvc-x64, QT_FORCE_STDERR_LOGGING=1) shows the loader error verbatim:
 *   QWARN : ... Cannot load library ...\plugins\tripleoscillator.dll:
 *           The specified module could not be found.
 * On Linux/macOS the module's undefined lmms symbols are bound from the
 * loading process's exported symbol table (the test target sets
 * ENABLE_EXPORTS), so the same load succeeds there. The product loads these
 * modules inside zene.exe, where the import resolves by construction: this
 * is a test-host limitation, not a product defect.
 */
constexpr auto testHostCanLoadPluginModules() -> bool
{
#ifdef Q_OS_WIN
	return false;
#else
	return true;
#endif
}

//! What every Windows skip in this suite says: the proven mechanism, named once.
constexpr const char* PluginModuleHostSkipMessage =
	"a Windows test host cannot load the migrated plugin modules: plugin modules link the "
	"zene executable, so on Windows their import descriptor names zene.exe and a test host "
	"cannot satisfy it; the product loads them inside zene.exe where that resolves by "
	"construction (CI msvc-x64: QLibrary::load -> ERROR_MOD_NOT_FOUND, 126)";

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
lmms::WaveSample* constructWaveSampleInPlace(unsigned char* storage)
{
	return ::new (storage) lmms::WaveSample{1};
}

/* Construct from the graphModel path without exposing the caller's seeded bytes. */
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
lmms::WaveSample* constructWaveSampleInPlace(unsigned char* storage, const lmms::graphModel* graph)
{
	return ::new (storage) lmms::WaveSample{graph};
}

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
bool sameObjectRepresentation(const unsigned char* actual, const unsigned char* expected,
	std::size_t size)
{
	for (std::size_t i = 0; i < size; ++i)
	{
		if (actual[i] != expected[i])
		{
			return false;
		}
	}
	return true;
}

struct MonstroRenderResult
{
	std::vector<float> samples;
	QString error;
};

auto renderMonstroLfoCase(bool modulationEnabled) -> MonstroRenderResult
{
	QLibrary lib{QString::fromUtf8(PART_C_MIGRATED_monstro)};
	lib.setLoadHints(QLibrary::PreventUnloadHint);
	if (!lib.load()) { return {{}, lib.errorString()}; }

	auto entry = reinterpret_cast<MainFn>(lib.resolve("lmms_plugin_main"));
	if (entry == nullptr) { return {{}, QStringLiteral("Monstro entry point not found")}; }

	partc::prepareInstrumentEnvironment("monstro");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* inst = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	if (inst == nullptr) { return {{}, QStringLiteral("Monstro instrument creation failed")}; }

	QDomDocument doc;
	QDomElement root = doc.createElement("instrument");
	QDomElement saved = inst->saveState(doc, root);
	saved.setAttribute(QStringLiteral("l1wav"), QStringLiteral("4"));
	saved.setAttribute(QStringLiteral("l1att"), QStringLiteral("20"));
	saved.setAttribute(QStringLiteral("l1rat"), QStringLiteral("10000"));
	saved.setAttribute(QStringLiteral("l1phs"), QStringLiteral("0"));
	saved.setAttribute(QStringLiteral("v1l1"), modulationEnabled ? QStringLiteral("1") : QStringLiteral("0"));
	inst->restoreState(saved);

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	auto samples = partc::renderInstrumentBuffers(*inst, frames, lmms::DefaultKey);
	delete inst;
	return {std::move(samples), {}};
}

auto maxAbsDiffInRange(const std::vector<float>& a, const std::vector<float>& b,
						std::size_t begin, std::size_t end) -> float
{
	float worst = 0.0f;
	for (std::size_t i = begin; i < end; ++i)
	{
		worst = std::max(worst, std::fabs(a[i] - b[i]));
	}
	return worst;
}

struct MonstroParityEvidence
{
	float attackDifference;
	float outsideAttackDifference;
	std::size_t firstOutsideDifferenceIndex;
};

auto compareMonstroAttackWindows(const std::vector<float>& migrated,
									const std::vector<float>& reference,
									std::size_t framesPerPeriod,
									std::size_t attackFrames) -> MonstroParityEvidence
{
	const std::size_t samplesPerPeriod = framesPerPeriod * 2;
	const std::size_t attackValues = std::min(attackFrames * 2, samplesPerPeriod);
	MonstroParityEvidence evidence{0.0f, 0.0f, migrated.size()};
	for (std::size_t periodStart = 0; periodStart < migrated.size(); periodStart += samplesPerPeriod)
	{
		const std::size_t periodEnd = periodStart + samplesPerPeriod;
		const std::size_t attackEnd = periodStart + attackValues;
		evidence.attackDifference = std::max(evidence.attackDifference,
			maxAbsDiffInRange(migrated, reference, periodStart, attackEnd));
		evidence.outsideAttackDifference = std::max(evidence.outsideAttackDifference,
			maxAbsDiffInRange(migrated, reference, attackEnd, periodEnd));
		for (std::size_t i = attackEnd; i < periodEnd; ++i)
		{
			if (migrated[i] != reference[i])
			{
				evidence.firstOutsideDifferenceIndex = std::min(evidence.firstOutsideDifferenceIndex, i);
			}
		}
	}
	return evidence;
}

auto createConfiguredMultitapEcho(MainFn entry) -> std::unique_ptr<lmms::Effect>
{
	auto effect = std::unique_ptr<lmms::Effect>{static_cast<lmms::Effect*>(entry(nullptr, nullptr))};
	QDomDocument doc;
	QDomElement root = doc.createElement("effect");
	root.setAttribute("on", 1);
	root.setAttribute("wet", 1);
	root.setAttribute("autoquit", 0);
	QDomElement controls = effect->controls()->saveState(doc, root);
	controls.setAttribute("steps", 32);
	controls.setAttribute("steplength", 1);
	controls.setAttribute("stages", 1);

	std::array<float, 32> amplitudes{};
	std::array<float, 32> lowpass{};
	lowpass.fill(std::log10(1000.0f / 20.0f));
	QString encoded;
	lmms::base64::encode(reinterpret_cast<const char*>(amplitudes.data()),
		static_cast<int>(sizeof(amplitudes)), encoded);
	controls.setAttribute("ampsteps", encoded);
	lmms::base64::encode(reinterpret_cast<const char*>(lowpass.data()),
		static_cast<int>(sizeof(lowpass)), encoded);
	controls.setAttribute("lpsteps", encoded);
	effect->loadSettings(root);
	return effect;
}

struct SampleRateRestore
{
	lmms::AudioEngine* engine;
	lmms::sample_rate_t rate;

	~SampleRateRestore()
	{
		engine->audioDev()->setSampleRateForTesting(rate);
		emit engine->sampleRateChanged();
	}
};

auto functionBody(const QByteArray& source, const QByteArray& signature) -> QByteArray
{
	const auto start = source.indexOf(signature);
	if (start < 0)
	{
		return {};
	}
	const auto openBrace = source.indexOf('{', start);
	if (openBrace < 0)
	{
		return {};
	}
	int depth = 1;
	for (auto index = openBrace + 1; index < source.size(); ++index)
	{
		if (source[index] == '{')
		{
			++depth;
		}
		else if (source[index] == '}' && --depth == 0)
		{
			return source.mid(openBrace + 1, index - openBrace - 1);
		}
	}
	return {};
}

} // namespace

namespace lmms
{

class LadspaInstantiationFailure
{
public:
	explicit LadspaInstantiationFailure(const ladspa_key_t& key)
	{
		auto* manager = Engine::getLADSPAManager();
		m_description = manager->getDescription(key);
		if (m_description == nullptr)
		{
			return;
		}

		const LADSPA_Descriptor* descriptor = manager->getDescriptor(key);
		if (descriptor == nullptr)
		{
			return;
		}

		m_failureDescriptor = *descriptor;
		m_failureDescriptor.instantiate = failInstantiation;
		m_originalFunction = m_description->descriptorFunction;
		s_active = this;
		m_description->descriptorFunction = descriptorForIndex;
	}

	~LadspaInstantiationFailure()
	{
		if (m_description != nullptr && s_active == this)
		{
			m_description->descriptorFunction = m_originalFunction;
			s_active = nullptr;
		}
	}

	auto isValid() const -> bool
	{
		return m_description != nullptr && s_active == this;
	}

private:
	static auto failInstantiation(const LADSPA_Descriptor*, unsigned long)
		-> LADSPA_Handle
	{
		return nullptr;
	}

	static auto descriptorForIndex(unsigned long index) -> const LADSPA_Descriptor*
	{
		if (s_active == nullptr || index != s_active->m_description->index)
		{
			return nullptr;
		}
		return &s_active->m_failureDescriptor;
	}

	inline static LadspaInstantiationFailure* s_active = nullptr;
	LadspaManagerDescription* m_description = nullptr;
	LADSPA_Descriptor_Function m_originalFunction = nullptr;
	LADSPA_Descriptor m_failureDescriptor{};
};

} // namespace lmms

namespace lmms
{
	class MultitapEchoEffectTestAccess
	{
	public:
		static float amp(const Effect& effect, int index)
		{
			return static_cast<const MultitapEchoEffect&>(effect).m_amp[index];
		}

		static float lpFreq(const Effect& effect, int index)
		{
			return static_cast<const MultitapEchoEffect&>(effect).m_lpFreq[index];
		}

		static void setAmpGraphSample(Effect& effect, int index, float value)
		{
			static_cast<MultitapEchoEffect&>(effect).m_controls.m_ampGraph.setSampleAt(index, value);
		}

		static void setLpGraphSample(Effect& effect, int index, float value)
		{
			static_cast<MultitapEchoEffect&>(effect).m_controls.m_lpGraph.setSampleAt(index, value);
		}
	};

	class FrequencyShifterEffectTestAccess
	{
		static auto concrete(const Effect& effect) -> const FrequencyShifterEffect&
		{
			return static_cast<const FrequencyShifterEffect&>(effect);
		}

		static auto concrete(Effect& effect) -> FrequencyShifterEffect&
		{
			return static_cast<FrequencyShifterEffect&>(effect);
		}

	public:
		static auto ringBufferSize(const Effect& effect) -> int
		{
			return concrete(effect).m_ringBufSize;
		}

		static auto writeIndex(const Effect& effect) -> int
		{
			return concrete(effect).m_writeIndex;
		}

		static auto trueDelay(const Effect& effect) -> float
		{
			return concrete(effect).m_trueDelay;
		}

		static auto ringBufferSizeForRate(const Effect& effect, sample_rate_t rate) -> int
		{
			const auto& controls = concrete(effect).m_controls;
			const float maxDelay = controls.m_delayLengthLong.maxValue() + controls.m_delayLengthShort.maxValue();
			return maxDelay * 0.001f * rate + 6.f;
		}

		static void setMaximumDelay(Effect& effect)
		{
			auto& controls = concrete(effect).m_controls;
			controls.m_delayLengthLong.setValue(controls.m_delayLengthLong.maxValue());
			controls.m_delayLengthShort.setValue(controls.m_delayLengthShort.maxValue());
		}
	};
}

class PluginPortsMigrationTest : public QObject
{
	Q_OBJECT

public:
	~PluginPortsMigrationTest() override
	{
		for (auto* lib : m_libraries)
		{
			delete lib;
		}
	}

private slots:
	void initTestCase();
	void freeBoyShapeBufferIsReleased();
	void invalidSbiSignatureIsRejected_data();
	void invalidSbiSignatureIsRejected();
	void waveSampleDefaultsInterpolationToDisabled();
	void gigPlayerAcceptsMidiNoteZero();
	void lfoStartsFromInitializedState();
	void delayLfoControlDescribesItsPeriod();
	void doubleFilterClearHistoryClearsSubfilter();
	void dualFilterCutoffCachesStartInitialized();
	void xpressiveLoadedWaveSamplesAreFinite();
	void xpressiveTruncatedWaveSamplesAreRejected();
	void positiveFractionHandlesVeryLargePositiveValues();
	void positiveFractionHandlesVeryLargeNegativeValues();
	void positiveFractionPreservesOrdinaryValues();
	void waveshaperLookupHandlesSamplesPastTheTableEnd();
	void ladspaEffectSurvivesFailedInstantiationsDuringSampleRateChange();
	void ladspaEffectProcessLockTakesNoExclusiveMutex();
	void ladspaEffectRejectsZeroInputPlugin();
	void typedDecodeRejectsIncompleteElement();
	void basicFiltersCannotBeCopied();
	void xpressiveSelectionRejectsUnavailableGraphs();
	void xpressiveExpressionEditsMarkSongModified_data();
	void xpressiveExpressionEditsMarkSongModified();
	void xpressiveSynthIsNotCopyable();
	void granularPitchShifterAdvertisesAllControls();
	void migratedPluginsPreserveBehaviour();
	void malletsVoiceLimitIsEnforcedPerInstrument();
	void malletsVoiceLimitSurvivesDeviceChange();
	void monstroLfoAttackRampsWithinPeriod();
	void gigplayerBidirectionalLoopReflectsAtExclusiveEnd();
	void gigplayerSingleFrameLoopRepeatsFrame();
	void gigplayerNoProgressFallbackKeepsEarlierSamples();
	void gigplayerAudioPathDoesNotAllocate();
	void gigplayerCallbackDoesNotWaitForAFileLoad();
	void gigplayerKeepsEveryOverlappingNote();
	void gigplayerPlaysMoreThan512OverlappingNotes();
	void gigplayerKeepsEveryLayerOfANote();
	void gigplayerSurvivesGlobalVoicePoolExhaustion();
	void dispersionRemainsStableAboveNyquist();
	void dynamicsProcessorRejectsShortWaveShape();
	void comparisonIsSensitive();
	void sidShortSegmentsKeepRegisterWritesOrdered();
	void watsynOverlappingNotesAreIndependentAndAllocationFree();
	void watsynServesEveryAdmittedNoteWithoutAllocating();
	void watsynKeepsTheWaveANoteStartedWith();
	void watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff();
	void watsynAppliesOscillatorParameterChangesToASoundingNote();
	void nonRealtimeLadspaEffectsAreInvalid();
	void monoDelayUpdatesItsDelayAcrossSampleRateChanges();
	void zeroLengthDelayPassesInputThrough();
	void nesNoiseFrequencyControlsRetuneHeldNotes();
	void slice5InstrumentsAreLiveAndDistinct();
	void organicDistortionLeavesPreOffsetFramesUnchanged();
	void slice6PluginsAreLiveAndExact();
	void partialInitializationCleansAcquiredResources();
	void lb302ProducerDoesNotWaitForEarlierClaim();
	void lb302DecayTracksSampleRateChanges();
	void lb302NoteEnqueueFailsafeStaysRealtimeSafe();
	void slice7PluginsAreLiveAndExact();
	void slice8PluginsAreLiveAndExact();
	void gigPlayerKeepsReleaseTriggerAcrossRegions();
	void gigOneFrameReleaseSampleIsAudible();
	void waterfallHistoryIsCopiedUnderProcessorLocks();
	void fastBassBoostFrequencySetterHonoursFloor();
	void xpressiveRandomVectorRejectsOutOfRangeFloatInputs();
	void multitapEchoRetunesAllTapsAfterSampleRateChange();
	void multitapEchoGraphEditsAreDeferredToAudioThread();
	void multitapEchoStagesCascade();
	void nesCoarseDetuneRetunesHeldNote();
	void opulenzUpdatePatchLocksEmulatorForWholeUpdate();
	void opulenzReloadEmulatorReplacesEmulatorUnderMutex();
	void opulenzRendersTheProvidedSpanSize();
	void opulenzZeroFrameCountStopsLoop();
	void opulenzReappliesVelocityScalingToHeldVoice();
	void realtimeWatchCoversSingleStreamedPlay();
	void sf2DialogRejectsAFilteredOutProgram();
	void gigPatchDialogPreviewsSelectedProgram();
	void gigKeyboardDimensionUsesPressedKey();
	void rmsHelperCopiesHaveIndependentBuffers();
	void sf2FailureTestChecksPluginModuleHostBeforeLoading();
	void sf2FailedLoadPreservesCurrentFontAndSource();
	void sf2PatchListKeepsEarlierDuplicateAndLaterUniqueProgram();
	void frequencyShifterSampleRateDropKeepsDelayIndicesInRange();

private:
	std::vector<QLibrary*> m_libraries;
	std::vector<partc::PluginRender> m_migrated;
	std::vector<partc::PluginRender> m_reference;
	std::vector<partc::LadspaRender> m_ladspa;
	QTemporaryDir m_cleanProjectDir;
};

void PluginPortsMigrationTest::xpressiveExpressionEditsMarkSongModified_data()
{
	QTest::addColumn<QString>("expression");
	QTest::addColumn<bool>("startWithExpression");
	QTest::newRow("invalid expression") << QStringLiteral("(") << false;
	QTest::newRow("cleared expression") << QString{} << true;
}

void PluginPortsMigrationTest::xpressiveExpressionEditsMarkSongModified()
{
	QFETCH(QString, expression);
	QFETCH(bool, startWithExpression);
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto& instruments = migratedInstruments();
	const auto xpressive = std::find_if(instruments.begin(), instruments.end(), [](const auto& candidate)
	{
		return QString::fromUtf8(candidate.plugin) == QLatin1String("xpressive");
	});
	QVERIFY(xpressive != instruments.end());

	QLibrary library{QString::fromUtf8(xpressive->path)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);
	QWidget host;
	QWidget tabContainer{&host};
	std::unique_ptr<lmms::gui::PluginView> view{instrument->createView(&tabContainer)};
	QVERIFY(view != nullptr);
	auto* editor = view->findChild<QPlainTextEdit*>();
	QVERIFY(editor != nullptr);

	auto* song = lmms::Engine::getSong();
	if (startWithExpression)
	{
		QDomDocument document;
		auto parent = document.createElement(QStringLiteral("instrument"));
		auto settings = instrument->saveState(document, parent);
		settings.setAttribute(QStringLiteral("W1"), QStringLiteral("t"));
		instrument->restoreState(settings);
		const bool wasBlocked = editor->blockSignals(true);
		editor->setPlainText(QStringLiteral("t"));
		editor->blockSignals(wasBlocked);
	}
	QVERIFY(m_cleanProjectDir.isValid());
	QVERIFY2(song->guiSaveProjectAs(m_cleanProjectDir.filePath(QStringLiteral("clean.mmp"))),
		"saving a clean baseline must reset the song's modified state");
	editor->setPlainText(expression);
	QVERIFY2(song->isModified(), "changing Xpressive expression text must mark the song modified");
}

void PluginPortsMigrationTest::initTestCase()
{
	// Slice 6 (task #589): LadspaManager reads LADSPA_PATH when it is first
	// constructed, so it has to be set before Engine::init(). The reference
	// renderer is spawned as a child process and inherits this environment.
	const auto failurePluginPath = QString::fromUtf8(PART_C_FAILURE_LADSPA_MODULE);
	const auto ladspaPath = QString::fromUtf8(PART_C_LADSPA_DIR) + QDir::listSeparator()
		+ QFileInfo{failurePluginPath}.absolutePath();
	qputenv("LADSPA_PATH", ladspaPath.toLocal8Bit());

	lmms::Engine::init(true);
	lmms::NotePlayHandleManager::init();
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
	lmms::Engine::audioEngine()->audioDev()->stopProcessing();
	lmms::NotePlayHandleManager::init();

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();
	lmms::NotePlayHandleManager::init();
	qInfo("engine: sample rate %d, framesPerPeriod %d, buffers per plugin %d",
		static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate()), static_cast<int>(frames), partc::Buffers);
	QVERIFY(frames > 0);
}

void PluginPortsMigrationTest::freeBoyShapeBufferIsReleased()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_freeboy)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto instrument = std::unique_ptr<lmms::Instrument>{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	std::array<float, 32> shape{};
	for (std::size_t i = 0; i < shape.size(); ++i)
	{
		shape[i] = static_cast<float>(i) / static_cast<float>(shape.size());
	}
	QString encodedShape;
	lmms::base64::encode(reinterpret_cast<const char*>(shape.data()),
		static_cast<int>(sizeof(shape)), encodedShape);
	QDomDocument settingsDocument;
	QDomElement settings = settingsDocument.createElement("instrument");
	settings.setAttribute("sampleShape", encodedShape);

	array_allocation_probe::start(sizeof(shape));
	instrument->restoreState(settings);
	const auto report = array_allocation_probe::stop();

	QVERIFY2(report.allocations >= 1, "the decoded sample shape was not a tracked array allocation");
	QVERIFY2(!report.overflow, "the tracked array allocation table overflowed");
	QCOMPARE(report.outstanding, std::size_t{0});

	QDomDocument savedDocument;
	QDomElement savedParent = savedDocument.createElement("instrument");
	const QDomElement saved = instrument->saveState(savedDocument, savedParent);
	QCOMPARE(saved.attribute("sampleShape"), encodedShape);
}

void PluginPortsMigrationTest::invalidSbiSignatureIsRejected_data()
{
	QTest::addColumn<QByteArray>("signature");
	QTest::addColumn<bool>("expectRenamed");
	// The SBI signature is four bytes: "SBI" followed by 0x1a. A file that
	// only starts with "SBI" is not an SBI patch and must not be decoded.
	QTest::newRow("wrong fourth byte") << QByteArray{"SBIX", 4} << false;
	QTest::newRow("truncated signature") << QByteArray{"SBI", 3} << false;
	// The genuine signature is still accepted: the fix must not reject
	// every file, and the renamed track is what proves the file was read.
	QTest::newRow("genuine signature") << QByteArray{"SBI\x1a", 4} << true;
}

void PluginPortsMigrationTest::invalidSbiSignatureIsRejected()
{
	QFETCH(QByteArray, signature);
	QFETCH(bool, expectRenamed);

	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_opulenz)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	const auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("opulenz");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	// This matches the plugin's stored name, so a decoded file would rename it.
	const QString originalName = QStringLiteral("OpulenZ");
	track->setName(originalName);
	auto inst = std::unique_ptr<lmms::Instrument>{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(inst != nullptr);

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// 52 bytes is the full SBI file; the name field is 32 bytes at offset 4.
	// The signature bytes are copied in one by one so the file keeps its
	// length, whatever the row's signature is.
	QByteArray sbi(52, '\0');
	for (int i = 0; i < signature.size(); ++i) { sbi[i] = signature[i]; }
	sbi.replace(4, 32, QByteArray{"Invalid SBI Patch"}.leftJustified(32, ' '));
	QFile file{tmp.filePath("test-patch.sbi")};
	QVERIFY(file.open(QIODevice::WriteOnly));
	QCOMPARE(file.write(sbi), static_cast<qint64>(sbi.size()));
	file.close();

	inst->loadFile(file.fileName());
	if (expectRenamed)
	{
		QCOMPARE(track->displayName(), QString::fromLatin1(sbi.mid(4, 32)));
	}
	else
	{
		QCOMPARE(track->displayName(), originalName);
	}
}

void PluginPortsMigrationTest::waveSampleDefaultsInterpolationToDisabled()
{
	alignas(lmms::WaveSample) unsigned char storage[sizeof(lmms::WaveSample)]{};
	const bool initialInterpolation = true;
	const bool disabledInterpolation = false;
	lmms::graphModel graph{-1.0f, 1.0f, 1, nullptr, true};
	const auto checkDefault = [&](lmms::WaveSample* wave) {
		const bool isDisabled = sameObjectRepresentation(
			storage + offsetof(lmms::WaveSample, m_interpolate),
			reinterpret_cast<const unsigned char*>(&disabledInterpolation), sizeof(bool));
		wave->~WaveSample();
		return isDisabled;
	};

	std::memcpy(storage + offsetof(lmms::WaveSample, m_interpolate),
		&initialInterpolation, sizeof(initialInterpolation));
	auto* wave = constructWaveSampleInPlace(storage);
	const bool integerConstructorDefault = checkDefault(wave);

	std::memcpy(storage + offsetof(lmms::WaveSample, m_interpolate),
		&initialInterpolation, sizeof(initialInterpolation));
	wave = constructWaveSampleInPlace(storage, &graph);
	const bool graphConstructorDefault = checkDefault(wave);

	QVERIFY2(integerConstructorDefault && graphConstructorDefault,
		"Both WaveSample constructors must default interpolation to disabled");
}

void PluginPortsMigrationTest::gigPlayerAcceptsMidiNoteZero()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer requires libgig and the vendored .gig test instrument");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("gigplayer");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);
	partc::applyInstrumentTestSettings(*instrument, "gigplayer");

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames)}, lmms::TimePos{}, 0};
	auto* noteHandle = lmms::NotePlayHandleManager::acquire(track.get(), 0, frames, note);
	std::vector<lmms::SampleFrame> output(static_cast<std::size_t>(frames));
	instrument->playNote(noteHandle, std::span<lmms::SampleFrame>{output});
	const bool accepted = noteHandle->m_pluginData != nullptr;
	if (accepted)
	{
		instrument->deleteNotePluginData(noteHandle);
		noteHandle->m_pluginData = nullptr;
	}
	lmms::NotePlayHandleManager::release(noteHandle);
	delete instrument;

	QVERIFY2(accepted, "GigPlayer did not register MIDI note 0");
#endif
}

void PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid()
{
	auto* manager = lmms::Engine::getLADSPAManager();
	const lmms::ladspa_key_t key{"decimator_1202.so", "decimator"};
	const auto* description = manager->getDescription(key);
	QVERIFY2(description != nullptr, "SWH Decimator test fixture was not loaded");
	QVERIFY(description->type == lmms::LadspaPluginType::Transfer);
	QVERIFY(description->inputChannels == 1 && description->outputChannels == 1);
	QVERIFY(!manager->isRealTimeCapable(key));

	const auto entry = qMakePair(manager->getName(key), key);
	QVERIFY2(!manager->getValidEffects().contains(entry),
		"non-real-time LADSPA plugin was classified as a valid effect");
	QVERIFY2(manager->getInvalidEffects().contains(entry),
		"non-real-time LADSPA plugin was not classified as invalid");
}

void PluginPortsMigrationTest::lfoStartsFromInitializedState()
{
	const auto alignment = std::align_val_t{alignof(lmms::Lfo)};
	void* storage = ::operator new(sizeof(lmms::Lfo), alignment);
	auto* poisonedStorage = static_cast<volatile unsigned char*>(storage);
	for (std::size_t byte = 0; byte < sizeof(lmms::Lfo); ++byte)
	{
		poisonedStorage[byte] = 0xFF;
	}
	auto* lfo = new (storage) lmms::Lfo{44100};
	lfo->setFrequency(2.0);
	const float firstOutput = lfo->tick();
	lfo->~Lfo();
	::operator delete(storage, alignment);

	QVERIFY(std::isfinite(firstOutput));
	QCOMPARE(firstOutput, 0.0f);
}

void PluginPortsMigrationTest::fastBassBoostFrequencySetterHonoursFloor()
{
	lmms::DspEffectLibrary::FastBassBoost updated{80.0f, 1.0f, 2.8f};
	lmms::DspEffectLibrary::FastBassBoost floor{10.0f, 1.0f, 2.8f};
	updated.setFrequency(-1.0f);

	for (const float input : {0.25f, -0.5f, 0.125f})
	{
		const float actual = updated.nextSample(input);
		const float expected = floor.nextSample(input);
		QVERIFY(std::isfinite(actual));
		QCOMPARE(actual, expected);
	}
}

void PluginPortsMigrationTest::delayLfoControlDescribesItsPeriod()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_delay)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	std::unique_ptr<lmms::Effect> effect{static_cast<lmms::Effect*>(entry(nullptr, nullptr))};
	QVERIFY(effect != nullptr);

	const auto models = effect->controls()->findChildren<lmms::TempoSyncKnobModel*>();
	const auto lfoModel = std::find_if(models.cbegin(), models.cend(), [](const auto* model)
	{
		return model->value() == 2.f;
	});
	QVERIFY(lfoModel != models.cend());
	QCOMPARE((*lfoModel)->displayName(), QStringLiteral("LFO period"));

	QFile dialogSource{QCoreApplication::applicationDirPath()
		+ QStringLiteral("/../../plugins/Delay/DelayControlsDialog.cpp")};
	QVERIFY2(dialogSource.open(QIODevice::ReadOnly), qPrintable(dialogSource.errorString()));
	const auto dialogCode = dialogSource.readAll();
	QVERIFY(dialogCode.contains("tr(\"TIME\")"));
	QVERIFY(dialogCode.contains("setHintText(tr(\"LFO period:\"), \" s\")"));
}

void PluginPortsMigrationTest::doubleFilterClearHistoryClearsSubfilter()
{
	lmms::BasicFilters<1> filter{static_cast<lmms::sample_rate_t>(48000)};
	filter.setFilterType(lmms::BasicFilters<1>::FilterType::DoubleLowPass);
	filter.calcFilterCoeffs(1000.0f, 0.7f);
	QVERIFY(filter.update(1.0f, 0) != 0.0f);

	filter.clearHistory();
	QCOMPARE(filter.update(0.0f, 0), 0.0f);
}

void PluginPortsMigrationTest::dualFilterCutoffCachesStartInitialized()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_dualfilter)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	const auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	allocationSizeToPoison.store(sizeof(lmms::DualFilterEffect), std::memory_order_relaxed);
	auto* rawEffect = static_cast<lmms::DualFilterEffect*>(entry(nullptr, nullptr));
	std::unique_ptr<lmms::DualFilterEffect> effect{rawEffect};
	QVERIFY(effect != nullptr);
	QCOMPARE(allocationSizeToPoison.load(std::memory_order_relaxed), std::size_t{0});

	const float zero = 0.0f;
	const auto* zeroBytes = reinterpret_cast<const unsigned char*>(&zero);
	QVERIFY(sameObjectRepresentation(
		reinterpret_cast<const unsigned char*>(&effect->m_currentCut1), zeroBytes, sizeof(zero)));
	QVERIFY(sameObjectRepresentation(
		reinterpret_cast<const unsigned char*>(&effect->m_currentRes1), zeroBytes, sizeof(zero)));
	QVERIFY(sameObjectRepresentation(
		reinterpret_cast<const unsigned char*>(&effect->m_currentCut2), zeroBytes, sizeof(zero)));
	QVERIFY(sameObjectRepresentation(
		reinterpret_cast<const unsigned char*>(&effect->m_currentRes2), zeroBytes, sizeof(zero)));
}

void PluginPortsMigrationTest::xpressiveLoadedWaveSamplesAreFinite()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_xpressive)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	constexpr std::size_t SampleCount = 4096;
	auto encodeSamples = [](const std::vector<float>& samples) {
		const QByteArray bytes{reinterpret_cast<const char*>(samples.data()),
			static_cast<int>(samples.size() * sizeof(float))};
		return QString::fromLatin1(bytes.toBase64());
	};
	const std::vector<float> nanSamples(SampleCount, std::numeric_limits<float>::quiet_NaN());
	const std::vector<float> positiveInfinitySamples(SampleCount,
		std::numeric_limits<float>::infinity());
	const std::vector<float> negativeInfinitySamples(SampleCount,
		-std::numeric_limits<float>::infinity());
	QDomDocument doc;
	QDomElement settings = doc.createElement("instrument");
	settings.setAttribute("W1sample", encodeSamples(nanSamples));
	settings.setAttribute("W2sample", encodeSamples(positiveInfinitySamples));
	settings.setAttribute("W3sample", encodeSamples(negativeInfinitySamples));
	instrument->restoreState(settings);

	auto* xpressive = reinterpret_cast<lmms::Xpressive*>(instrument.get());
	const auto waveIsFinite = [](const lmms::WaveSample& wave) {
		return std::all_of(wave.m_samples, wave.m_samples + wave.m_length,
			[](float sample) { return std::isfinite(sample); });
	};
	QVERIFY2(waveIsFinite(xpressive->W1()), "W1 waveform retained a saved NaN sample");
	QVERIFY2(waveIsFinite(xpressive->W2()), "W2 waveform retained a saved infinite sample");
	QVERIFY2(waveIsFinite(xpressive->W3()), "W3 waveform retained a saved infinite sample");
}

void PluginPortsMigrationTest::xpressiveTruncatedWaveSamplesAreRejected()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_xpressive)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	QDomDocument document;
	QDomElement root = document.createElement("instrument");
	QDomElement settings = instrument->saveState(document, root);

	// One float is 4 bytes; a graph holds GRAPH_LENGTH floats, so this payload is truncated.
	const float truncatedSample = 0.75f;
	const QByteArray truncatedBytes{reinterpret_cast<const char*>(&truncatedSample),
		static_cast<int>(sizeof(truncatedSample))};
	const QString truncatedData = QString::fromLatin1(truncatedBytes.toBase64());

	const auto savedSample = [&instrument](const QString& attribute) {
		QDomDocument resultDocument;
		QDomElement resultRoot = resultDocument.createElement("instrument");
		return instrument->saveState(resultDocument, resultRoot).attribute(attribute);
	};

	const QStringList sampleAttributes{"W1sample", "W2sample", "W3sample"};
	for (const auto& attribute : sampleAttributes)
	{
		QVERIFY(!settings.attribute(attribute).isEmpty());

		auto truncatedSettings = settings.cloneNode(true).toElement();
		truncatedSettings.setAttribute(attribute, truncatedData);
		instrument->restoreState(truncatedSettings);
		QCOMPARE(savedSample(attribute), settings.attribute(attribute));

		auto missingSettings = settings.cloneNode(true).toElement();
		missingSettings.removeAttribute(attribute);
		instrument->restoreState(missingSettings);
		QCOMPARE(savedSample(attribute), settings.attribute(attribute));

		instrument->restoreState(settings);
	}
}

void PluginPortsMigrationTest::xpressiveRandomVectorRejectsOutOfRangeFloatInputs()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_xpressive)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("xpressive");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	QDomDocument document;
	QDomElement root = document.createElement("instrument");
	QDomElement settings = instrument->saveState(document, root);
	const auto renderExpression = [&](const QString& expression)
	{
		settings.setAttribute("O1", expression);
		settings.setAttribute("O2", "0");
		instrument->restoreState(settings);
		return partc::renderInstrumentInFreshThread(*instrument,
			lmms::Engine::audioEngine()->framesPerPeriod(), lmms::DefaultKey);
	};

	const auto outOfRangeIndex = renderExpression("randsv(65536*65536,0)");
	const auto zeroExpression = renderExpression("0");
	const bool indexFallsBackToZero = !outOfRangeIndex.empty()
		&& std::all_of(outOfRangeIndex.begin(), outOfRangeIndex.end(),
			[](const float sample) { return sample == 0.0f; });
	const bool zeroExpressionIsZero = !zeroExpression.empty()
		&& std::all_of(zeroExpression.begin(), zeroExpression.end(),
			[](const float sample) { return sample == 0.0f; });

	const auto zeroSeed = renderExpression("randsv(0,0)");
	const auto outOfRangeSeed = renderExpression("randsv(0,32768*65536)");
	const bool seedFallsBackToZero = outOfRangeSeed.size() == zeroSeed.size()
		&& std::equal(outOfRangeSeed.begin(), outOfRangeSeed.end(), zeroSeed.begin());
	const auto peak = [](const std::vector<float>& samples)
	{
		float result = 0.0f;
		for (const float sample : samples)
		{
			result = std::max(result, std::fabs(sample));
		}
		return result;
	};
	QVERIFY2(indexFallsBackToZero && zeroExpressionIsZero && seedFallsBackToZero,
		qPrintable(QString{"index fallback: %1 (peak %2); zero expression: %3; seed fallback: %4 "
			"(peaks %5, %6; delta %7)"}
			.arg(indexFallsBackToZero).arg(peak(outOfRangeIndex)).arg(zeroExpressionIsZero)
			.arg(seedFallsBackToZero).arg(peak(zeroSeed)).arg(peak(outOfRangeSeed))
			.arg(partc::maxAbsDiff(zeroSeed, outOfRangeSeed))));
}

void PluginPortsMigrationTest::positiveFractionHandlesVeryLargePositiveValues()
{
	const float result = lmms::positiveFraction(std::numeric_limits<float>::max());
	QVERIFY(std::isfinite(result));
	QVERIFY(result >= 0.0f);
	QVERIFY(result < 1.0f);
}

void PluginPortsMigrationTest::positiveFractionHandlesVeryLargeNegativeValues()
{
	const float result = lmms::positiveFraction(-std::numeric_limits<float>::max());
	QVERIFY(std::isfinite(result));
	QVERIFY(result >= 0.0f);
	QVERIFY(result < 1.0f);
}

void PluginPortsMigrationTest::positiveFractionPreservesOrdinaryValues()
{
	QCOMPARE(lmms::positiveFraction(1.25f), 0.25f);
	QCOMPARE(lmms::positiveFraction(-0.25f), 0.75f);
	QCOMPARE(lmms::positiveFraction(-2.0f), 0.0f);
}

void PluginPortsMigrationTest::waveshaperLookupHandlesSamplesPastTheTableEnd()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_waveshaper)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	std::unique_ptr<lmms::Effect> effect{static_cast<lmms::Effect*>(entry(nullptr, nullptr))};
	QVERIFY(effect != nullptr);

	// Clip off, unity gains, fully wet. The last table entry is scaled down to
	// 2^-20 so that a waveshaped out-of-table sample stays inside the +/-1000
	// range that AudioBus::sanitize() applies to track output. Without that,
	// the bus clamp would hide what the waveshaper itself produced.
	constexpr auto tailGain = 0x1p-20f;
	QDomDocument doc;
	QDomElement root = doc.createElement("effect");
	root.setAttribute("on", 1);
	root.setAttribute("wet", 1);
	root.setAttribute("autoquit", 0);
	QDomElement controls = effect->controls()->saveState(doc, root);
	controls.setAttribute("inputGain", 1);
	controls.setAttribute("outputGain", 1);
	controls.setAttribute("clipInput", 0);
	effect->loadSettings(root);
	const auto graphs = effect->controls()->findChildren<lmms::graphModel*>();
	QCOMPARE(static_cast<int>(graphs.size()), 1);
	graphs[0]->setSampleAt(199, tailGain);

	// Frame 0: |x| * 200 is far beyond the int range (lookup must saturate, not wrap).
	// Frame 1: |x| * 200 is about 4e9, also beyond the int range, with a finite result.
	// Frame 2: |x| * 200 overflows float to +inf; the result must not turn into NaN.
	// Frame 3: an in-table sample (|x| * 200 == 100) keeps the ordinary lookup covered.
	std::vector<lmms::SampleFrame> data{{1e9f, -1e9f}, {2e7f, -2e7f}, {3e38f, -3e38f}, {0.5f, -0.5f}};
	const lmms::f_cnt_t frames = static_cast<lmms::f_cnt_t>(data.size());
	lmms::SampleFrame* busPointers[partc::ChannelPairs] = {data.data()};
	lmms::AudioBus bus{busPointers, partc::ChannelPairs, frames};
	effect->processAudioBuffer(bus);

	QCOMPARE(data[0].left(), 1e9f * tailGain);
	QCOMPARE(data[0].right(), -1e9f * tailGain);
	QCOMPARE(data[1].left(), 2e7f * tailGain);
	QCOMPARE(data[1].right(), -2e7f * tailGain);
	// The bus clamps track output to +/-1000, and the waveshaped value is far above that.
	QCOMPARE(data[2].left(), 1000.0f);
	QCOMPARE(data[2].right(), -1000.0f);
	QCOMPARE(data[3].left(), 0.5f);
	QCOMPARE(data[3].right(), -0.5f);
}

void PluginPortsMigrationTest::ladspaEffectSurvivesFailedInstantiationsDuringSampleRateChange()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_ladspaeffect)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto* descriptor = reinterpret_cast<const lmms::Plugin::Descriptor*>(
		lib->resolve("ladspaeffect_plugin_descriptor"));
	QVERIFY(descriptor != nullptr);

	const auto& spec = partc::ladspaSpecs().front();
	auto key = partc::ladspaKeyFor(descriptor, spec);
	const lmms::ladspa_key_t managerKey{
		key.attributes.value(QStringLiteral("file")),
		key.attributes.value(QStringLiteral("plugin"))};
	{
		lmms::LadspaInstantiationFailure failure{managerKey};
		QVERIFY(failure.isValid());
		auto effect = std::unique_ptr<lmms::LadspaEffect>{
			static_cast<lmms::LadspaEffect*>(entry(nullptr, &key))};
		QVERIFY(effect != nullptr);
		QVERIFY(!effect->isOkay());
		QVERIFY(effect->controls() == nullptr);

		emit lmms::Engine::audioEngine()->sampleRateChanged();
		QVERIFY(!effect->isOkay());
	}

	auto effect = std::unique_ptr<lmms::LadspaEffect>{
		static_cast<lmms::LadspaEffect*>(entry(nullptr, &key))};
	QVERIFY(effect != nullptr);
	QVERIFY(effect->isOkay());
	QVERIFY(effect->controls() != nullptr);
	using CreateView = lmms::gui::EffectControlDialog* (lmms::EffectControls::*)();
	const CreateView createView = &lmms::EffectControls::createView;
	auto editor = std::unique_ptr<lmms::gui::EffectControlDialog>{
		(effect->controls()->*createView)()};
	QVERIFY(editor != nullptr);
	{
		lmms::LadspaInstantiationFailure failure{managerKey};
		QVERIFY(failure.isValid());
		emit lmms::Engine::audioEngine()->sampleRateChanged();
		QVERIFY(effect->controls() == nullptr);
	}
	QVERIFY(!effect->isOkay());
}

void PluginPortsMigrationTest::ladspaEffectProcessLockTakesNoExclusiveMutex()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_ladspaeffect)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto* descriptor = reinterpret_cast<const lmms::Plugin::Descriptor*>(
		lib->resolve("ladspaeffect_plugin_descriptor"));
	QVERIFY(descriptor != nullptr);

	const auto& spec = partc::ladspaSpecs().front();
	auto key = partc::ladspaKeyFor(descriptor, spec);
	auto effect = std::unique_ptr<lmms::LadspaEffect>{
		static_cast<lmms::LadspaEffect*>(entry(nullptr, &key))};
	QVERIFY(effect != nullptr);
	QVERIFY(effect->isOkay());

	// A QMutex is not recursive: if processLock() still owns one, this second
	// claim from the same thread cannot succeed. The fixed process gate claims
	// an in-flight slot instead, so both claims are admitted without a wait.
	const bool first = effect->processLock();
	const bool second = effect->processLock();
	if (first) { effect->processUnlock(); }
	if (second) { effect->processUnlock(); }
	QVERIFY2(first, "the first process claim must be admitted");
	QVERIFY2(second, "the audio path still takes an exclusive mutex: a second "
		"claim from the same thread was refused");
}

void PluginPortsMigrationTest::ladspaEffectRejectsZeroInputPlugin()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	// partial_init_failure.so also exports a Source descriptor (no audio
	// inputs). A project can name it under the LADSPA effect wrapper, where
	// DEFAULT_CHANNELS / inputChannels used to divide by zero.
	const QString fixturePath = QString::fromUtf8(PART_C_FAILURE_LADSPA_MODULE);
	auto* fixtureLib = new QLibrary{fixturePath};
	fixtureLib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(fixtureLib->load(), qPrintable(fixtureLib->errorString()));
	m_libraries.push_back(fixtureLib);

	auto* hostLib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_ladspaeffect)};
	hostLib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(hostLib->load(), qPrintable(hostLib->errorString()));
	m_libraries.push_back(hostLib);

	auto entry = reinterpret_cast<MainFn>(hostLib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto* descriptor = reinterpret_cast<const lmms::Plugin::Descriptor*>(
		hostLib->resolve("ladspaeffect_plugin_descriptor"));
	QVERIFY(descriptor != nullptr);

	lmms::Plugin::Descriptor::SubPluginFeatures::Key key{descriptor,
		QStringLiteral("ladspaeffect:zero_input_source")};
	key.attributes["file"] = QFileInfo{fixturePath}.fileName();
	key.attributes["plugin"] = QStringLiteral("zero_input_source");

	auto effect = std::unique_ptr<lmms::LadspaEffect>{
		static_cast<lmms::LadspaEffect*>(entry(nullptr, &key))};
	QVERIFY(effect != nullptr);
	QVERIFY2(!effect->isOkay(),
		"a zero-input LADSPA source must be rejected as an effect");
	QVERIFY(effect->controls() == nullptr);
}

void PluginPortsMigrationTest::nesNoiseFrequencyControlsRetuneHeldNotes()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto presetNoise = renderNesNoiseTail(false, false, false, false);
	const auto noteNoise = renderNesNoiseTail(false, false, true, false);
	QVERIFY2(presetNoise.error.isEmpty(), qPrintable(presetNoise.error));
	QVERIFY2(noteNoise.error.isEmpty(), qPrintable(noteNoise.error));
	const float modeChange = partc::maxAbsDiff(presetNoise.samples, noteNoise.samples);
	qInfo().noquote() << QString{"NES note-frequency mode toggle max|delta|=%1"}
		.arg(modeChange, 0, 'g', 4);
	QVERIFY2(modeChange > 0.05f, "switching a held note to note-frequency noise did not retune it");

	const auto unquantizedNoise = renderNesNoiseTail(true, false, false, false);
	const auto quantizedNoise = renderNesNoiseTail(true, false, false, true);
	QVERIFY2(unquantizedNoise.error.isEmpty(), qPrintable(unquantizedNoise.error));
	QVERIFY2(quantizedNoise.error.isEmpty(), qPrintable(quantizedNoise.error));
	const float quantizeChange = partc::maxAbsDiff(unquantizedNoise.samples, quantizedNoise.samples);
	qInfo().noquote() << QString{"NES held-note quantization toggle max|delta|=%1"}
		.arg(quantizeChange, 0, 'g', 4);
	QVERIFY2(quantizeChange > 0.05f, "switching quantization on did not retune held-note noise");
}

void PluginPortsMigrationTest::sf2DialogRejectsAFilteredOutProgram()
{
#ifdef PART_C_MIGRATED_sf2player
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_sf2player)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("sf2player");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);
	partc::applyInstrumentTestSettings(*instrument, "sf2player");

	QWidget host;
	auto* view = instrument->createView(&host);
	QVERIFY(view != nullptr);
	bool dialogFound = false;
	bool filterFound = false;
	bool acceptButtonFound = false;
	bool programViewFound = false;
	bool initiallyAcceptable = false;
	bool programRowsFilteredOut = false;
	bool accepted = false;
	QTimer::singleShot(0, qApp, [&] {
		auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
		if (dialog == nullptr)
		{
			return;
		}
		dialogFound = true;
		auto* filter = dialog->findChild<QLineEdit*>(QStringLiteral("m_filterEdit"));
		auto* acceptButton = dialog->findChild<QPushButton*>(QStringLiteral("m_okButton"));
		auto* programView = dialog->findChild<QTableView*>(QStringLiteral("m_progListView"));
		filterFound = filter != nullptr;
		acceptButtonFound = acceptButton != nullptr;
		programViewFound = programView != nullptr;
		initiallyAcceptable = acceptButton != nullptr && acceptButton->isEnabled();
		if (filter == nullptr || acceptButton == nullptr || programView == nullptr)
		{
			dialog->reject();
			return;
		}
		filter->setText(QStringLiteral("qwen-no-matching-sf2-preset-345"));
		QCoreApplication::processEvents();
		programRowsFilteredOut = programView->model()->rowCount() == 0;
		acceptButton->click();
		accepted = dialog->result() == QDialog::Accepted;
		dialog->reject();
	});
	QVERIFY(QMetaObject::invokeMethod(view, "showPatchDialog", Qt::DirectConnection));
	QVERIFY(dialogFound);
	QVERIFY(filterFound);
	QVERIFY(acceptButtonFound);
	QVERIFY(programViewFound);
	QVERIFY(initiallyAcceptable);
	QVERIFY(programRowsFilteredOut);
	QVERIFY2(!accepted, "the dialog accepted a program hidden by its search filter");
	delete view;
	delete instrument;
#else
	QSKIP("SF2Player is not built in this configuration");
#endif
}

void PluginPortsMigrationTest::typedDecodeRejectsIncompleteElement()
{
	std::uint32_t* decoded = nullptr;
	int size = -1;
	lmms::base64::decode(QStringLiteral("AQID"), &decoded, &size);
	const bool rejected = decoded == nullptr && size == 0;
	delete[] decoded;
	QVERIFY2(rejected, "three decoded bytes cannot form a uint32_t element");
}

void PluginPortsMigrationTest::basicFiltersCannotBeCopied()
{
	using Filter = lmms::BasicFilters<2>;
	QVERIFY2(!std::is_copy_constructible_v<Filter>, "BasicFilters must not shallow-copy its owned double-filter subfilter");
	QVERIFY2(!std::is_copy_assignable_v<Filter>, "BasicFilters must not copy-assign its owned double-filter subfilter");
}

void PluginPortsMigrationTest::sf2PatchListKeepsEarlierDuplicateAndLaterUniqueProgram()
{
	struct Preset
	{
		int bank;
		int program;
		QString name;
	};
	using SoundFont = std::vector<Preset>;
	const std::vector<SoundFont> soundFonts{
		{{0, 5, QStringLiteral("first soundfont")}, {1, 90, QStringLiteral("other bank")}},
		{{0, 5, QStringLiteral("duplicate")}, {0, 7, QStringLiteral("later unique")}},
	};
	std::map<int, QString> programs;

	lmms::sf2::forEachUniquePresetForBank(
		static_cast<int>(soundFonts.size()), 0,
		[&soundFonts](int index) { return &soundFonts[static_cast<std::size_t>(index)]; },
		[](const SoundFont* soundFont, auto&& visitPreset) {
			for (const auto& preset : *soundFont)
			{
				visitPreset(preset);
			}
		},
		[](const SoundFont*, const Preset& preset) { return preset.bank; },
		[](const Preset& preset) { return preset.program; },
		[&programs](int program) { return programs.contains(program); },
		[&programs](const SoundFont*, const Preset& preset, int program) {
			programs.emplace(program, preset.name);
		});

	QCOMPARE(programs.size(), std::size_t{2});
	QCOMPARE(programs.at(5), QStringLiteral("first soundfont"));
	QVERIFY(programs.contains(7));
}

void PluginPortsMigrationTest::xpressiveSelectionRejectsUnavailableGraphs()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_xpressive)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("xpressive");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);
	auto* xpressive = static_cast<lmms::Xpressive*>(instrument);

	for (const int selection : {5, 6})
	{
		xpressive->selectedGraph().setValue(selection);
		QCOMPARE(xpressive->selectedGraph().value(), 4);
		QDomDocument document;
		auto settings = document.createElement("xpressive");
		settings.setAttribute("selectedGraph", selection);
		settings.setAttribute("id", QString::number(xpressive->selectedGraph().id()));
		document.appendChild(settings);
		xpressive->selectedGraph().loadSettings(settings, "selectedGraph");
		QCOMPARE(xpressive->selectedGraph().value(), 4);
	}

	delete instrument;
}

void PluginPortsMigrationTest::xpressiveSynthIsNotCopyable()
{
	QVERIFY2(!std::is_copy_constructible_v<lmms::ExprSynth>,
		"Assertion: ExprSynth must not be copy-constructible because it owns its expressions");
	QVERIFY2(!std::is_copy_assignable_v<lmms::ExprSynth>,
		"Assertion: ExprSynth must not be copy-assignable because it owns its expressions");
}

void PluginPortsMigrationTest::granularPitchShifterAdvertisesAllControls()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary lib{QString::fromUtf8(PART_C_MIGRATED_granularpitchshifter)};
	lib.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib.load(), qPrintable(lib.errorString()));

	auto entry = reinterpret_cast<MainFn>(lib.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	std::unique_ptr<lmms::Effect> fx{static_cast<lmms::Effect*>(entry(nullptr, nullptr))};
	QVERIFY(fx != nullptr);
	QVERIFY(fx->controls() != nullptr);
	QCOMPARE(fx->controls()->controlCount(), 15);
}

void PluginPortsMigrationTest::migratedPluginsPreserveBehaviour()
{
	// The QLibrary loads below are exactly what a Windows test host cannot do
	// (see the note on testHostCanLoadPluginModules()): skip at the top so the
	// failure mode is a named skip, not one loader error per module.
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();

	// 1. Render the migrated plugins through the real plugin entry point.
	for (const auto& m : migratedModules())
	{
		auto* lib = new QLibrary{QString::fromUtf8(m.path)};
		lib->setLoadHints(QLibrary::PreventUnloadHint);
		QVERIFY2(lib->load(), qPrintable(QString{"%1: %2"}.arg(m.plugin, lib->errorString())));
		m_libraries.push_back(lib);

		auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
		QVERIFY2(entry != nullptr, m.plugin);

		auto* fx = static_cast<lmms::Effect*>(entry(nullptr, nullptr));
		QVERIFY2(fx != nullptr, m.plugin);

		partc::applyTestSettings(*fx, m.plugin);

		partc::PluginRender render;
		render.name = QString::fromUtf8(m.plugin);
		render.samples = partc::renderInFreshThread(*fx, frames);
		render.checksum = partc::checksum(render.samples);
		m_migrated.push_back(std::move(render));

		delete fx;
	}

	// 1b. Render the migrated instruments through the real plugin entry point.
	//     Instruments are parented to an InstrumentTrack, exactly like the
	//     engine creates them, and settings go through the instrument's own
	//     saveState()/restoreState() round trip.
	for (const auto& m : migratedInstruments())
	{
		auto* lib = new QLibrary{QString::fromUtf8(m.path)};
		lib->setLoadHints(QLibrary::PreventUnloadHint);
		QVERIFY2(lib->load(), qPrintable(QString{"%1: %2"}.arg(m.plugin, lib->errorString())));
		m_libraries.push_back(lib);

		auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
		QVERIFY2(entry != nullptr, m.plugin);

		partc::prepareInstrumentEnvironment(m.plugin);
		auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
		auto* inst = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
		QVERIFY2(inst != nullptr, m.plugin);

		partc::applyInstrumentTestSettings(*inst, m.plugin);

		partc::PluginRender render;
		render.name = QString::fromUtf8(m.plugin);
		// Plugins whose first note period and its cleanup must not touch the heap.
		const bool realtimeChecked = m.plugin == std::string{"freeboy"} || m.plugin == std::string{"tripleoscillator"} || m.plugin == std::string{"malletsstk"}
			|| m.plugin == std::string{"kicker"} || m.plugin == std::string{"gigplayer"};
		partc::RealtimeAllocationCounts realtimeCounts;
		render.samples = partc::renderInstrumentInFreshThread(*inst, frames, lmms::DefaultKey,
			realtimeChecked ? &realtimeCounts : nullptr);
		if (realtimeChecked)
		{
			QVERIFY2(realtimeCounts.allocations == 0 && realtimeCounts.deallocations == 0,
				qPrintable(QString{"%1 render allocations=%2, deallocations=%3"}.arg(render.name)
					.arg(static_cast<qulonglong>(realtimeCounts.allocations))
					.arg(static_cast<qulonglong>(realtimeCounts.deallocations))));
		}
		render.checksum = partc::checksum(render.samples);
		m_migrated.push_back(std::move(render));

		delete inst;
	}

	// 1c. Render the migrated LADSPA-hosted effects (slice 6, task #589).
	//     LadspaEffect needs its sub-plugin Key, so these go through
	//     renderLadspa() instead of the plain entry point above.
	for (const auto& spec : partc::ladspaSpecs())
	{
		auto result = partc::renderLadspa(PART_C_MIGRATED_ladspaeffect, spec, frames);
		QVERIFY2(result.loaded, qPrintable(result.error));
		QVERIFY2(result.effectOkay,
			qPrintable(QString{"%1: LadspaEffect did not instantiate the LADSPA plugin "
				"(LADSPA_PATH=%2)"}.arg(result.name, QString::fromUtf8(PART_C_LADSPA_DIR))));

		partc::PluginRender render;
		render.name = result.name;
		render.samples = result.samples;
		render.checksum = result.checksum;
		m_migrated.push_back(std::move(render));
		m_ladspa.push_back(std::move(result));
	}

	// 1d. Render the migrated LV2 host plugins (slice 8, task #589). Both
	//     resolve their DSP from the Key's "uri" attribute, so they go through
	//     the keyed render helpers instead of the plain entry point.
#ifdef PART_C_MIGRATED_lv2effect
	for (const auto& spec : partc::lv2Specs())
	{
		if (!partc::lv2BundleProvides(spec.uri))
		{
			// Optional fixture (mda-lv2) not installed here: skip it on BOTH sides —
			// the reference renderer asks the same question — so the two render lists
			// stay comparable instead of failing on a missing package.
			continue;
		}
		if (QString::fromUtf8(spec.plugin) == QLatin1String("lv2effect"))
		{
			auto result = partc::renderKeyedEffect(PART_C_MIGRATED_lv2effect, spec, frames);
			QVERIFY2(result.loaded, qPrintable(result.error));
			QVERIFY2(result.okay, qPrintable(QString{"%1: Lv2Effect did not instantiate %2"}
				.arg(result.render.name, QString::fromUtf8(spec.uri))));
			m_migrated.push_back(std::move(result.render));
		}
#ifdef PART_C_MIGRATED_lv2instrument
		else
		{
			auto result = partc::renderKeyedInstrument(PART_C_MIGRATED_lv2instrument, spec, frames,
				lmms::DefaultKey);
			QVERIFY2(result.loaded, qPrintable(result.error));
			QVERIFY2(result.okay, qPrintable(QString{"%1: Lv2Instrument did not instantiate %2"}
				.arg(result.render.name, QString::fromUtf8(spec.uri))));
			m_migrated.push_back(std::move(result.render));
		}
#endif
	}
#endif

	// 2. Render the pre-migration reference sources in a separate process.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString referencePath = tmp.filePath("reference-renders.bin");

	QProcess reference;
	reference.start(QString::fromUtf8(PART_C_REFERENCE_EXE), {referencePath});
	QVERIFY(reference.waitForStarted(30000));
	QVERIFY(reference.waitForFinished(300000));
	const QByteArray referenceOut = reference.readAllStandardOutput();
	const QByteArray referenceErr = reference.readAllStandardError();
	if (!referenceOut.isEmpty())
	{
		qInfo("reference renderer stdout:\n%s", referenceOut.constData());
	}
	if (!referenceErr.isEmpty())
	{
		qWarning("reference renderer stderr:\n%s", referenceErr.constData());
	}
	QCOMPARE(reference.exitStatus(), QProcess::NormalExit);
	QCOMPARE(reference.exitCode(), 0);

	m_reference = partc::readRenders(referencePath);
	QCOMPARE(m_reference.size(), m_migrated.size());

	// 3. Compare sample-exactly and report the evidence per plugin.
	for (std::size_t i = 0; i < m_migrated.size(); ++i)
	{
		const auto& migrated = m_migrated[i];
		const auto& referenceRender = m_reference[i];

		QCOMPARE(referenceRender.name, migrated.name);
		QVERIFY2(migrated.samples.size() == referenceRender.samples.size(),
			qPrintable(migrated.name));

		const float worst = partc::maxAbsDiff(migrated.samples, referenceRender.samples);

		qInfo().noquote()
			<< QString{"%1: %2 samples | first samples migrated=[%3, %4] reference=[%5, %6] "
					   "| sha256 migrated=%7 reference=%8 | max|delta|=%9"}
				   .arg(migrated.name)
				   .arg(migrated.samples.size())
				   .arg(migrated.samples[0], 0, 'g', 9)
				   .arg(migrated.samples[1], 0, 'g', 9)
				   .arg(referenceRender.samples[0], 0, 'g', 9)
				   .arg(referenceRender.samples[1], 0, 'g', 9)
				   .arg(migrated.checksum, referenceRender.checksum)
				   .arg(worst, 0, 'g', 3);

		if (migrated.name == QLatin1String("multitapecho"))
		{
			QVERIFY2(worst > 1e-6f,
				"the fixed two-stage render must differ from the single-stage old reference");
			continue;
		}

		if (migrated.name == QLatin1String("monstro"))
		{
			// The test settings give LFO 1 a 0.1 ms attack and LFO 2 a 0.2 ms
			// attack; both contribute to oscillator volume modulation.
			const auto attackFrames = static_cast<std::size_t>(
				std::ceil(0.2f * lmms::Engine::audioEngine()->outputSampleRate() / 1000.0f));
			const auto evidence = compareMonstroAttackWindows(migrated.samples,
				referenceRender.samples,
				static_cast<std::size_t>(lmms::Engine::audioEngine()->framesPerPeriod()), attackFrames);
			QVERIFY(attackFrames > 0);
			QVERIFY2(evidence.attackDifference > 1e-5f,
				"Monstro's corrected LFO attack did not change samples within its attack window");
			QVERIFY2(evidence.outsideAttackDifference == 0.0f,
				qPrintable(QString{"Monstro differs outside attack windows: attackFrames=%1 firstMismatch=%2 max|delta|=%3"}
					.arg(attackFrames)
					.arg(evidence.firstOutsideDifferenceIndex)
					.arg(evidence.outsideAttackDifference, 0, 'g', 3)));
		}
		else if (migrated.name == QLatin1String("gigplayer"))
		{
			// The frozen reference has the old half-amplitude 16-bit conversion.
			QVERIFY(migrated.samples.size() == referenceRender.samples.size());
			float scaledWorst = 0.0f;
			for (std::size_t sample = 0; sample < migrated.samples.size(); ++sample)
			{
				scaledWorst = std::max(scaledWorst, std::fabs(
					migrated.samples[sample] - 2.0f * referenceRender.samples[sample]));
			}
			QVERIFY2(scaledWorst <= 1e-6f,
				qPrintable(QString{"gigplayer render is not twice the pre-fix reference "
							   "(scaled max|delta|=%1)"}
						   .arg(scaledWorst, 0, 'g', 3)));
		}
		else
		{
			QVERIFY2(migrated.checksum == referenceRender.checksum,
				qPrintable(QString{"%1: migrated render differs from the pre-migration reference "
							   "(max|delta|=%2)"}
							   .arg(migrated.name)
							   .arg(worst, 0, 'g', 3)));
		}
	}

	// 4. Migrated but not provable in this build: report each gap explicitly
	//    so it can never be mistaken for a pass.
	for (const auto& gap : partc::unprovenGaps())
	{
		qWarning().noquote() << QString{"UNPROVEN %1: %2"}
			.arg(QString::fromUtf8(gap.plugin), QString::fromUtf8(gap.reason));
	}
}

/*! One Mallets instrument voices at most this many notes at the same time -
	the product limit of the instrument (plugins/Stk/Mallets/Mallets.cpp,
	MALLETS_VOICE_LIMIT). The limit belongs to the instrument, so two Mallets
	instruments voice voiceLimit notes each.
*/
constexpr int malletsVoiceLimit = 128;

void PluginPortsMigrationTest::malletsVoiceLimitIsEnforcedPerInstrument()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_malletsstk)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();
	QVERIFY(frames > 0);

	constexpr int instances = 2;
	// More notes than one instrument may voice at the same time.
	constexpr int notesPerInstance = malletsVoiceLimit + 50;

	std::vector<std::unique_ptr<lmms::InstrumentTrack>> tracks;
	std::vector<lmms::Instrument*> instruments;
	tracks.reserve(instances);
	instruments.reserve(instances);
	partc::prepareInstrumentEnvironment("malletsstk");

	for (int i = 0; i < instances; ++i)
	{
		auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
		auto* inst = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
		QVERIFY(inst != nullptr);
		partc::applyInstrumentTestSettings(*inst, "malletsstk");
		tracks.push_back(std::move(track));
		instruments.push_back(inst);
	}

	struct NoteHolder
	{
		lmms::NotePlayHandle* handle = nullptr;
		lmms::Instrument* instrument = nullptr;
	};

	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames)}, lmms::TimePos{0}, lmms::DefaultKey};
	std::vector<NoteHolder> notes;
	notes.reserve(instances * notesPerInstance);
	for (int i = 0; i < instances; ++i)
	{
		for (int j = 0; j < notesPerInstance; ++j)
		{
			auto handle = lmms::NotePlayHandleManager::acquire(
				instruments[i]->instrumentTrack(), 0, frames, note, nullptr, -1,
				lmms::NotePlayHandle::Origin::MidiClip);
			notes.push_back({handle, instruments[i]});
		}
	}

	std::vector<lmms::SampleFrame> data(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{data};

	//! Renders one period of a note into a cleared buffer and reports whether
	//! the note produced any audio at all. A note that owns a prepared voice
	//! is audible; a note that waits for a voice is not.
	auto renderNote = [&](const NoteHolder& nh) {
		std::fill(data.begin(), data.end(), lmms::SampleFrame{0.0f, 0.0f});
		nh.instrument->playNote(nh.handle, span);
		for (const auto& frame : data)
		{
			if (frame.left() != 0.0f || frame.right() != 0.0f) { return true; }
		}
		return false;
	};

	partc::RealtimeAllocationCounts counts;
	std::vector<char> audible(notes.size(), 0);
	std::vector<char> stillAudible(notes.size(), 0);

	// Period 1: every note is started. The notes that fit into the voice
	// limit of their instrument have to be audible: they own a prepared voice,
	// not a silence marker.
	partc::RealtimeAllocationWatch::begin();
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		audible[i] = renderNote(notes[i]) ? 1 : 0;
	}
	const auto periodCounts = partc::RealtimeAllocationWatch::end();
	counts.allocations += periodCounts.allocations;
	counts.deallocations += periodCounts.deallocations;

	for (int inst = 0; inst < instances; ++inst)
	{
		const int base = inst * notesPerInstance;
		for (int j = 0; j < malletsVoiceLimit; ++j)
		{
			QVERIFY2(audible[base + j] != 0,
				qPrintable(QString{"Mallets note %1 of instrument %2 was silenced instead of "
					"taking a prepared voice"}
					.arg(j).arg(inst)));
		}
	}

	// Period 2: an instrument voices at most malletsVoiceLimit notes at a
	// time. The notes beyond the limit wait for a voice, so they stay silent
	// as long as the voices are busy.
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		stillAudible[i] = renderNote(notes[i]) ? 1 : 0;
	}

	for (int inst = 0; inst < instances; ++inst)
	{
		const int base = inst * notesPerInstance;
		int voiced = 0;
		for (int j = 0; j < notesPerInstance; ++j)
		{
			voiced += stillAudible[base + j];
		}
		QVERIFY2(voiced == malletsVoiceLimit,
			qPrintable(QString{"Mallets instrument %1 voiced %2 notes at once, the product "
				"limit of one instrument is %3"}
				.arg(inst).arg(voiced).arg(malletsVoiceLimit)));

		// The notes beyond the limit wait for a voice: they are not dropped,
		// but they are silent while every voice of the instrument is busy.
		for (int j = malletsVoiceLimit; j < notesPerInstance; ++j)
		{
			QVERIFY2(stillAudible[base + j] == 0,
				qPrintable(QString{"Mallets instrument %1 voiced its note %2 although it "
					"already had %3 notes running"}
					.arg(inst).arg(j).arg(malletsVoiceLimit)));
		}
	}

	partc::RealtimeAllocationWatch::begin();
	for (auto& nh : notes)
	{
		if (nh.handle->m_pluginData != nullptr)
		{
			nh.instrument->deleteNotePluginData(nh.handle);
		}
	}
	const auto cleanupCounts = partc::RealtimeAllocationWatch::end();
	counts.allocations += cleanupCounts.allocations;
	counts.deallocations += cleanupCounts.deallocations;

	QVERIFY2(counts.allocations == 0 && counts.deallocations == 0,
		qPrintable(QString{"render allocations=%1, deallocations=%2"}
			.arg(static_cast<qulonglong>(counts.allocations))
			.arg(static_cast<qulonglong>(counts.deallocations))));

	for (auto& nh : notes)
	{
		if (nh.handle != nullptr)
		{
			lmms::NotePlayHandleManager::release(nh.handle);
		}
	}

	for (auto* inst : instruments)
	{
		delete inst;
	}
}

/*! A device change retires the pool of the old sample rate but keeps its
	sustained notes rendering through it. The product limit belongs to the
	instrument and therefore has to cover the current pool and every retired
	pool together: starting more notes after the change must not voice more
	than malletsVoiceLimit notes of the one instrument.
*/
void PluginPortsMigrationTest::malletsVoiceLimitSurvivesDeviceChange()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_malletsstk)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto* engine = lmms::Engine::audioEngine();
	auto* device = engine->audioDev();
	QVERIFY(device != nullptr);
	const auto originalRate = device->sampleRate();
	struct RestoreRate
	{
		lmms::AudioEngine* engine;
		lmms::AudioDevice* device;
		lmms::sample_rate_t rate;
		~RestoreRate()
		{
			device->setSampleRateForTesting(rate);
			emit engine->sampleRateChanged();
		}
	} restoreRate{engine, device, originalRate};

	const lmms::f_cnt_t frames = engine->framesPerPeriod();
	QVERIFY(frames > 0);

	partc::prepareInstrumentEnvironment("malletsstk");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);
	partc::applyInstrumentTestSettings(*instrument, "malletsstk");

	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames)},
		lmms::TimePos{0}, lmms::DefaultKey};
	std::vector<lmms::NotePlayHandle*> notes;

	auto acquireNote = [&]() {
		notes.push_back(lmms::NotePlayHandleManager::acquire(
			instrument->instrumentTrack(), 0, frames, note, nullptr, -1,
			lmms::NotePlayHandle::Origin::MidiClip));
	};

	std::vector<lmms::SampleFrame> data(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{data};

	//! Renders one period of a note into a cleared buffer and reports whether
	//! the note produced audio. A note that owns a prepared voice is audible;
	//! a note that waits for a voice is silent.
	auto renderNote = [&](lmms::NotePlayHandle* handle) {
		std::fill(data.begin(), data.end(), lmms::SampleFrame{0.0f, 0.0f});
		instrument->playNote(handle, span);
		for (const auto& frame : data)
		{
			if (frame.left() != 0.0f || frame.right() != 0.0f) { return true; }
		}
		return false;
	};

	partc::RealtimeAllocationCounts counts;

	// Saturating the instrument's voice limit with sustained notes.
	for (int i = 0; i < malletsVoiceLimit; ++i) { acquireNote(); }

	std::vector<char> initialAudible(notes.size(), 0);
	partc::RealtimeAllocationWatch::begin();
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		initialAudible[i] = renderNote(notes[i]) ? 1 : 0;
	}
	{
		const auto periodCounts = partc::RealtimeAllocationWatch::end();
		counts.allocations += periodCounts.allocations;
		counts.deallocations += periodCounts.deallocations;
	}
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		QVERIFY2(initialAudible[i] != 0,
			qPrintable(QString{"Mallets note %1 was silenced before the device change"}
				.arg(static_cast<int>(i))));
	}
	QVERIFY2(notes.size() < static_cast<std::size_t>(2 * malletsVoiceLimit),
		"the test needs more notes than the instrument may voice at once");

	// Device change: the instrument builds a fresh pool for the new sample
	// rate and retires the one the sustained notes keep using.
	const lmms::sample_rate_t changedRate =
		originalRate == 44100 ? 32000 : 44100;
	device->setSampleRateForTesting(changedRate);
	emit engine->sampleRateChanged();
	QCOMPARE(engine->outputSampleRate(), changedRate);

	// Start more notes of the same instrument after the change.
	constexpr int extraNotes = 50;
	for (int i = 0; i < extraNotes; ++i) { acquireNote(); }

	std::vector<char> afterChange(notes.size(), 0);
	partc::RealtimeAllocationWatch::begin();
	for (std::size_t i = 0; i < notes.size(); ++i)
	{
		afterChange[i] = renderNote(notes[i]) ? 1 : 0;
	}
	{
		const auto periodCounts = partc::RealtimeAllocationWatch::end();
		counts.allocations += periodCounts.allocations;
		counts.deallocations += periodCounts.deallocations;
	}

	int voiced = 0;
	for (const char audible : afterChange) { voiced += audible; }
	QVERIFY2(voiced <= malletsVoiceLimit,
		qPrintable(QString{"Mallets instrument voiced %1 notes after the device change, "
			"the product limit of one instrument is %2"}
			.arg(voiced).arg(malletsVoiceLimit)));
	QVERIFY2(voiced == malletsVoiceLimit,
		qPrintable(QString{"Mallets instrument voiced %1 notes after the device change, "
			"expected the %2 sustained notes to keep their voices"}
			.arg(voiced).arg(malletsVoiceLimit)));

	// The sustained notes keep the voices of the retired pool; the new notes
	// wait for the shared budget instead of claiming a second instrument's
	// worth of voices from the fresh pool.
	for (int i = 0; i < malletsVoiceLimit; ++i)
	{
		QVERIFY2(afterChange[static_cast<std::size_t>(i)] != 0,
			qPrintable(QString{"Mallets sustained note %1 lost its voice across the "
				"device change"}.arg(i)));
	}
	for (std::size_t i = malletsVoiceLimit; i < notes.size(); ++i)
	{
		QVERIFY2(afterChange[i] == 0,
			qPrintable(QString{"Mallets note %1 was voiced over the instrument's limit "
				"by the fresh pool after the device change"}
				.arg(static_cast<int>(i) - malletsVoiceLimit)));
	}

	// Ending some sustained notes frees budget for the waiting notes.
	constexpr int releasedNotes = 10;
	for (int i = 0; i < releasedNotes; ++i)
	{
		QVERIFY(notes[static_cast<std::size_t>(i)]->m_pluginData != nullptr);
	}
	partc::RealtimeAllocationWatch::begin();
	for (int i = 0; i < releasedNotes; ++i)
	{
		instrument->deleteNotePluginData(notes[static_cast<std::size_t>(i)]);
	}
	{
		const auto cleanupCounts = partc::RealtimeAllocationWatch::end();
		counts.allocations += cleanupCounts.allocations;
		counts.deallocations += cleanupCounts.deallocations;
	}

	int newlyVoiced = 0;
	for (std::size_t i = malletsVoiceLimit; i < notes.size(); ++i)
	{
		newlyVoiced += renderNote(notes[i]) ? 1 : 0;
	}
	QVERIFY2(newlyVoiced == releasedNotes,
		qPrintable(QString{"Mallets voiced %1 waiting notes after %2 voices were "
			"released"}.arg(newlyVoiced).arg(releasedNotes)));

	partc::RealtimeAllocationWatch::begin();
	for (auto* handle : notes)
	{
		if (handle->m_pluginData != nullptr)
		{
			instrument->deleteNotePluginData(handle);
		}
	}
	{
		const auto cleanupCounts = partc::RealtimeAllocationWatch::end();
		counts.allocations += cleanupCounts.allocations;
		counts.deallocations += cleanupCounts.deallocations;
	}

	QVERIFY2(counts.allocations == 0 && counts.deallocations == 0,
		qPrintable(QString{"render allocations=%1, deallocations=%2"}
			.arg(static_cast<qulonglong>(counts.allocations))
			.arg(static_cast<qulonglong>(counts.deallocations))));

	for (auto* handle : notes)
	{
		lmms::NotePlayHandleManager::release(handle);
	}
	delete instrument;
}

void PluginPortsMigrationTest::monstroLfoAttackRampsWithinPeriod()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto withoutModulation = renderMonstroLfoCase(false);
	QVERIFY2(withoutModulation.error.isEmpty(), qPrintable(withoutModulation.error));
	const auto withAttack = renderMonstroLfoCase(true);
	QVERIFY2(withAttack.error.isEmpty(), qPrintable(withAttack.error));

	const auto firstPeriodSampleCount = static_cast<std::size_t>(
		lmms::Engine::audioEngine()->framesPerPeriod()) * 2;
	QVERIFY(withoutModulation.samples.size() >= firstPeriodSampleCount);
	QVERIFY(withAttack.samples.size() >= firstPeriodSampleCount);

	const std::vector<float> baselineFirstPeriod{withoutModulation.samples.begin(),
		withoutModulation.samples.begin() + static_cast<std::ptrdiff_t>(firstPeriodSampleCount)};
	const std::vector<float> attackedFirstPeriod{withAttack.samples.begin(),
		withAttack.samples.begin() + static_cast<std::ptrdiff_t>(firstPeriodSampleCount)};
	float baselinePeak = 0.0f;
	for (const float sample : baselineFirstPeriod)
	{
		baselinePeak = std::max(baselinePeak, std::fabs(sample));
	}
	QVERIFY2(baselinePeak > 1e-4f, "Monstro baseline rendered silence");

	const float firstPeriodDifference = partc::maxAbsDiff(attackedFirstPeriod, baselineFirstPeriod);
	QVERIFY2(firstPeriodDifference > 1e-5f,
		qPrintable(QString{"Monstro LFO attack did not ramp within the first period (max|delta|=%1)"}
			.arg(firstPeriodDifference, 0, 'g', 3)));
}

void PluginPortsMigrationTest::gigplayerBidirectionalLoopReflectsAtExclusiveEnd()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	const QString fixture = QDir{QString{QT_TESTCASE_SOURCEDIR}}
		.filePath(QStringLiteral("data/gigproof/pingpong.gig"));
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("instrument"));
	QDomElement saved = instrument->saveState(doc, root);
	saved.setAttribute(QStringLiteral("src"), fixture);
	saved.setAttribute(QStringLiteral("patch"), QStringLiteral("0"));
	saved.setAttribute(QStringLiteral("bank"), QStringLiteral("0"));
	instrument->restoreState(saved);

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const auto rendered = partc::renderInstrumentInFreshThread(*instrument, frames, lmms::DefaultKey);
	delete instrument;

	constexpr int LoopEnd = 128;
	constexpr int FramesAfterEnd = 8;
	constexpr double FixtureRate = 44100.0;
	const double outputRate = lmms::Engine::audioEngine()->outputSampleRate();
	const auto afterEnd = static_cast<std::size_t>(std::lround(
		(LoopEnd + FramesAfterEnd) * outputRate / FixtureRate));
	const auto atEnd = static_cast<std::size_t>(std::lround((LoopEnd + 2) * outputRate / FixtureRate));
	const auto firstPass = static_cast<std::size_t>(std::lround(
		(LoopEnd - 2 - FramesAfterEnd) * outputRate / FixtureRate));
	QVERIFY(rendered.size() > 2 * std::max(afterEnd, firstPass));

	const float endSample = rendered[2 * atEnd];
	const float reflected = rendered[2 * afterEnd];
	const float firstPassSample = rendered[2 * firstPass];
	QVERIFY2(endSample > 1e-4f,
		qPrintable(QString{"exclusive loop end should reflect to end - 2, got %1"}
			.arg(endSample, 0, 'g', 6)));
	QVERIFY(std::fabs(firstPassSample) > 1e-4f);
	QVERIFY2(std::fabs(reflected - firstPassSample) < 0.02f,
		qPrintable(QString{"bidirectional loop sample %1 should reflect from exclusive end; first pass %2"}
			.arg(reflected, 0, 'g', 6).arg(firstPassSample, 0, 'g', 6)));
#endif
}

// ---------------------------------------------------------------------------
// The GIG player's audio path (cluster new-p9-149, new-p9-150, new-p15-71).
// ---------------------------------------------------------------------------
namespace
{

//! Renders one new note per period and counts the heap traffic of the render.
auto renderGigNotesCountingHeap(lmms::Instrument& instrument, int key, int notes)
	-> std::pair<std::size_t, std::size_t>
{
	using namespace lmms;

	const auto frames = Engine::audioEngine()->framesPerPeriod();
	InstrumentTrack* track = instrument.instrumentTrack();

	const Note note{TimePos{static_cast<tick_t>(frames * notes)}, TimePos{0}, key};
	std::vector<NotePlayHandle*> handles;
	for (int i = 0; i < notes; ++i)
	{
		handles.push_back(NotePlayHandleManager::acquire(track, 0, frames * notes, note));
	}

	if (instrument.isSingleStreamed())
	{
		instrument.handleMidiEvent(
			MidiEvent{MidiNoteOn, 0, static_cast<std::int16_t>(key), 100}, TimePos{0}, 0);
	}

	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));

	for (int i = 0; i < notes; ++i)
	{
		std::fill(data.begin(), data.end(), SampleFrame{0.0f, 0.0f});
		const std::span<SampleFrame> span{data};

		// The first note warms libgig's sample cache up; the measurement
		// starts with the second note, so that the cache is not counted.
		const bool measure = (i > 0);
		if (measure) { HeapWatch::begin(); }
		instrument.playNote(handles[static_cast<std::size_t>(i)], span);
		if (instrument.isSingleStreamed()) { instrument.play(span); }
		if (measure) { HeapWatch::end(); }
	}

	for (auto* handle : handles)
	{
		NotePlayHandleManager::release(handle);
	}

	return {HeapWatch::allocations, HeapWatch::deallocations};
}


//! Starts the given number of notes in one period and renders them.
auto renderGigNotes(lmms::Instrument& instrument, int key, int notes) -> float
{
	using namespace lmms;

	constexpr int Periods = 64;
	const auto frames = Engine::audioEngine()->framesPerPeriod();
	InstrumentTrack* track = instrument.instrumentTrack();

	const Note note{TimePos{static_cast<tick_t>(frames * Periods)}, TimePos{0}, key};
	std::vector<NotePlayHandle*> handles;
	for (int i = 0; i < notes; ++i)
	{
		handles.push_back(NotePlayHandleManager::acquire(track, 0, frames * Periods, note));
	}

	instrument.handleMidiEvent(
		MidiEvent{MidiNoteOn, 0, static_cast<std::int16_t>(key), 100}, TimePos{0}, 0);

	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));
	float peak = 0.0f;

	for (int period = 0; period < Periods; ++period)
	{
		std::fill(data.begin(), data.end(), SampleFrame{0.0f, 0.0f});
		const std::span<SampleFrame> span{data};

		for (auto* handle : handles) { instrument.playNote(handle, span); }
		instrument.play(span);

		for (f_cnt_t frame = 0; frame < frames; ++frame)
		{
			peak = std::max(peak, std::fabs(data[frame].left()));
			peak = std::max(peak, std::fabs(data[frame].right()));
		}
	}

	for (auto* handle : handles) { NotePlayHandleManager::release(handle); }

	return peak;
}


//! Renders the given number of notes and reports the peak of the render.
//!
//! The render runs in a worker thread with a watchdog: an index that wraps
//! inside the audio callback keeps the callback from ever coming back, and
//! that has to show up as a failed case and not as a hanging test.
auto renderGigNotesWithWatchdog(lmms::Instrument& instrument, int key, int notes,
		float& peak) -> bool
{
	QMutex mutex;
	QWaitCondition condition;
	bool done = false;

	std::thread worker{[&] {
		const float rendered = renderGigNotes(instrument, key, notes);
		QMutexLocker locker(&mutex);
		peak = rendered;
		done = true;
		condition.wakeAll();
	}};

	bool finished = false;
	{
		QMutexLocker locker(&mutex);
		QDeadlineTimer deadline{ 30000 };
		while (!done && !deadline.hasExpired())
		{
			condition.wait(&mutex, 100);
		}
		finished = done;
	}

	if (!finished)
	{
		// The render does not come back. The worker cannot be joined, so it is
		// detached: the case fails and the test binary ends.
		worker.detach();
		return false;
	}

	worker.join();
	return true;
}


//! The synchronisation between the test and the thread that opens a GIG file.
struct GigLoadSync
{
	QMutex mutex;
	QWaitCondition condition;
	bool loaderPaused = false;
	bool loaderReleased = false;
	bool renderDone = false;
	float renderPeak = 0.0f;
};


//! The hook that pauses the file preparation until the test lets it continue.
void pauseGigFileLoad(void* context)
{
	auto* sync = static_cast<GigLoadSync*>(context);
	QMutexLocker locker(&sync->mutex);
	sync->loaderPaused = true;
	sync->condition.wakeAll();

	// The deadline only breaks a deadlock: a callback that waits for the
	// loader would keep the loader waiting for ever.
	QDeadlineTimer deadline{ 10000 };
	while (!sync->loaderReleased && !deadline.hasExpired())
	{
		sync->condition.wait(&sync->mutex, 100);
	}
}


} // namespace


//! The audio path of the GIG player must not touch the heap: every note is
//! started and rendered, and neither the note storage nor the sample storage
//! may grow while the audio thread is inside them.
void PluginPortsMigrationTest::gigplayerAudioPathDoesNotAllocate()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
#ifdef Q_OS_UNIX
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("gigplayer");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);
	partc::applyInstrumentTestSettings(*instrument, "gigplayer");

	constexpr int Notes = 8;
	const auto counts = renderGigNotesCountingHeap(*instrument, lmms::DefaultKey, Notes);
	delete instrument;

	QVERIFY2(counts.first == 0,
		qPrintable(QString{"the audio path allocated %1 times while %2 notes were played"}
			.arg(static_cast<qulonglong>(counts.first)).arg(Notes)));
	QVERIFY2(counts.second == 0,
		qPrintable(QString{"the audio path freed %1 times while %2 notes were played"}
			.arg(static_cast<qulonglong>(counts.second)).arg(Notes)));
#else
	QSKIP("The allocator hook of this test needs a POSIX allocator");
#endif
#endif
}


//! The audio callback must not wait for a GIG file that is being prepared.
//!
//! The thread that opens the file stops inside the file preparation, at the
//! point where the old code held its mutex. The audio thread renders in the
//! meantime and has to finish: a callback that waits for the loader would
//! never come back while the preparation is paused.
void PluginPortsMigrationTest::gigplayerCallbackDoesNotWaitForAFileLoad()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("gigplayer");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);
	partc::applyInstrumentTestSettings(*instrument, "gigplayer");

	const QString other = QDir{QString{QT_TESTCASE_SOURCEDIR}}
		.filePath(QStringLiteral("data/gigproof/pingpong.gig"));
	QVERIFY(QFile::exists(other));

	using HookSetter = void (*)(void (*)(void*), void*);
	const auto setHook = reinterpret_cast<HookSetter>(
		lib->resolve("lmms_gigplayer_set_file_load_hook"));
	QVERIFY2(setHook != nullptr, "the GIG player does not export its file load hook");

	GigLoadSync sync;
	setHook(pauseGigFileLoad, &sync);

	// The thread that opens the other GIG file. It stops inside the file
	// preparation, with the hook waiting.
	std::thread loader{[&] {
		QMetaObject::invokeMethod(instrument, "openFile", Qt::DirectConnection,
			Q_ARG(QString, other), Q_ARG(bool, false));
	}};

	{
		QMutexLocker locker(&sync.mutex);
		QDeadlineTimer deadline{ 10000 };
		while (!sync.loaderPaused && !deadline.hasExpired())
		{
			sync.condition.wait(&sync.mutex, 100);
		}
		QVERIFY2(sync.loaderPaused, "the file preparation was never reached");
	}

	// The audio thread renders while the file is prepared.
	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> data(static_cast<std::size_t>(frames));
	std::thread renderer{[&] {
		const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * 64)},
			lmms::TimePos{}, lmms::DefaultKey};
		auto* handle = lmms::NotePlayHandleManager::acquire(track.get(), 0, frames * 64, note);

		std::fill(data.begin(), data.end(), lmms::SampleFrame{0.0f, 0.0f});
		const std::span<lmms::SampleFrame> span{data};
		instrument->handleMidiEvent(
			lmms::MidiEvent{lmms::MidiNoteOn, 0, lmms::DefaultKey, 100}, lmms::TimePos{}, 0);
		instrument->playNote(handle, span);
		instrument->play(span);

		float peak = 0.0f;
		for (const auto& frame : data)
		{
			peak = std::max(peak, std::fabs(frame.left()));
			peak = std::max(peak, std::fabs(frame.right()));
		}

		{
			QMutexLocker locker(&sync.mutex);
			sync.renderPeak = peak;
			sync.renderDone = true;
			sync.condition.wakeAll();
		}
		lmms::NotePlayHandleManager::release(handle);
	}};

	bool renderFinished = false;
	float renderPeak = 0.0f;
	{
		QMutexLocker locker(&sync.mutex);
		QDeadlineTimer deadline{ 10000 };
		while (!sync.renderDone && !deadline.hasExpired())
		{
			sync.condition.wait(&sync.mutex, 100);
		}
		renderFinished = sync.renderDone;
		renderPeak = sync.renderPeak;
	}

	// Let the loader continue, whatever the callback did.
	{
		QMutexLocker locker(&sync.mutex);
		sync.loaderReleased = true;
		sync.condition.wakeAll();
	}
	loader.join();
	renderer.join();
	setHook(nullptr, nullptr);

	QVERIFY2(renderFinished,
		"the audio callback waited for the GIG file that was being loaded");
	QVERIFY2(renderPeak > 1e-4f,
		"the audio callback rendered silence while the file was being loaded");

	delete instrument;
#endif
}


//! Overlapping notes must all be played: the note pool is large enough for
//! the notes a project can hold at the same time, and a note that does not
//! fit is not accepted at all, so that it never stays half registered.
void PluginPortsMigrationTest::gigplayerKeepsEveryOverlappingNote()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	const auto renderWithNotes = [&](int notes, float& peak) -> bool {
		partc::prepareInstrumentEnvironment("gigplayer");
		auto* track = new lmms::InstrumentTrack(lmms::Engine::getSong());
		auto* instrument = static_cast<lmms::Instrument*>(entry(track, nullptr));
		if (instrument == nullptr) { return false; }
		partc::applyInstrumentTestSettings(*instrument, "gigplayer");

		const bool finished = renderGigNotesWithWatchdog(*instrument, lmms::DefaultKey, notes, peak);
		if (finished)
		{
			delete instrument;
			delete track;
		}
		// A render that does not come back keeps the instrument and its track
		// alive: the worker is still inside them.
		return finished;
	};

	float single = 0.0f;
	float many = 0.0f;
	float more = 0.0f;
	float last = 0.0f;
	float oneMore = 0.0f;

	QVERIFY2(renderWithNotes(1, single), "the render of one note did not finish");
	QVERIFY2(renderWithNotes(128, many), "the render of 128 notes did not finish");
	QVERIFY2(renderWithNotes(129, more), "the render of 129 notes did not finish");
	// 256 notes are the point where an 8 bit index into the note order wraps:
	// the render has to come back and the 256th note has to be audible.
	QVERIFY2(renderWithNotes(255, last), "the render of 255 notes did not finish");
	QVERIFY2(renderWithNotes(256, oneMore), "the render of 256 notes did not finish");

	QVERIFY2(single > 1e-4f, "a single note rendered silence");
	QVERIFY2(more > many,
		qPrintable(QString{"the 129th overlapping note was dropped: %1 notes reached %2, "
			"129 notes reached %3"}
			.arg(128).arg(many, 0, 'g', 6).arg(more, 0, 'g', 6)));
	QVERIFY2(more - many > 0.5f * single,
		qPrintable(QString{"the 129th overlapping note was not audible: %1 notes reached %2, "
			"129 notes reached %3"}
			.arg(128).arg(many, 0, 'g', 6).arg(more, 0, 'g', 6)));
	QVERIFY2(oneMore > last,
		qPrintable(QString{"the 256th overlapping note was dropped: %1 notes reached %2, "
			"256 notes reached %3"}
			.arg(255).arg(last, 0, 'g', 6).arg(oneMore, 0, 'g', 6)));
	QVERIFY2(oneMore - last > 0.5f * single,
		qPrintable(QString{"the 256th overlapping note was not audible: %1 notes reached %2, "
			"256 notes reached %3"}
			.arg(255).arg(last, 0, 'g', 6).arg(oneMore, 0, 'g', 6)));
#endif
}


//! More than 512 overlapping notes must all play.
//!
//! The note pool used to be a fixed array of 512 slots, and a note that found
//! the pool full was refused outright. A project can hold more notes than that,
//! so the pool now grows in segments: a note that finds the pool full asks the
//! control thread for another segment and starts on a later period.
//!
//! The measurement is the energy of the render (the sum of |left| + |right|
//! over every frame), which adds up over the notes whatever their phase: a
//! note that starts one period later than the others contributes its energy
//! from that period on instead of cancelling against them. The energy
//! therefore grows with the number of notes that actually play, and it
//! flattens out as soon as the pool stops accepting notes.
void PluginPortsMigrationTest::gigplayerPlaysMoreThan512OverlappingNotes()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	// The growth service runs from a timer, which a test has no event loop for.
	// The test drives it between periods, where the control thread would. A
	// build without the service simply never grows the pool, and the level
	// assertions below are what decide the case.
	using GrowthService = void (*)(lmms::Instrument*);
	const auto serviceGrowth = reinterpret_cast<GrowthService>(
		lib->resolve("lmms_gigplayer_service_pool_growth"));
	const auto grow = [&](lmms::Instrument* instrument) {
		if (serviceGrowth != nullptr) { serviceGrowth(instrument); }
	};

	// The DC fixture holds one constant-level sample, so the sum of any number
	// of voices is exactly proportional to the number of voices that play, in
	// every period and whatever period each note started in.
	const QString dcFixture = QDir{QString{QT_TESTCASE_SOURCEDIR}}
		.filePath(QStringLiteral("data/gigproof/dc.gig"));
	QVERIFY2(QFile::exists(dcFixture),
		qPrintable(QString{"missing DC fixture %1: regenerate it with make_gig --dc"}
			.arg(dcFixture)));

	const auto renderEnergy = [&](int notes, double& energy) -> bool {
		partc::prepareInstrumentEnvironment("gigplayer");
		auto* track = new lmms::InstrumentTrack(lmms::Engine::getSong());
		auto* instrument = static_cast<lmms::Instrument*>(entry(track, nullptr));
		if (instrument == nullptr) { return false; }

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("instrument"));
		QDomElement saved = instrument->saveState(doc, root);
		saved.setAttribute(QStringLiteral("src"), dcFixture);
		saved.setAttribute(QStringLiteral("patch"), QStringLiteral("0"));
		saved.setAttribute(QStringLiteral("bank"), QStringLiteral("0"));
		instrument->restoreState(saved);

		constexpr int Periods = 48;
		const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
		lmms::InstrumentTrack* instrumentTrack = instrument->instrumentTrack();
		const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * Periods)},
			lmms::TimePos{0}, lmms::DefaultKey};

		std::vector<lmms::NotePlayHandle*> handles;
		handles.reserve(static_cast<std::size_t>(notes));
		for (int i = 0; i < notes; ++i)
		{
			handles.push_back(lmms::NotePlayHandleManager::acquire(instrumentTrack, 0,
				frames * Periods, note));
		}

		std::vector<lmms::SampleFrame> data(static_cast<std::size_t>(frames));
		double rendered = 0.0;
		for (int period = 0; period < Periods; ++period)
		{
			std::fill(data.begin(), data.end(), lmms::SampleFrame{0.0f, 0.0f});
			const std::span<lmms::SampleFrame> span{data};

			// A note the pool refused has no plugin data yet, so it is offered
			// again on the next period, after the pool has grown.
			for (auto* handle : handles) { instrument->playNote(handle, span); }
			instrument->play(span);
			grow(instrument);

			for (const auto& frame : data)
			{
				rendered += std::fabs(frame.left()) + std::fabs(frame.right());
			}
		}

		for (auto* handle : handles) { lmms::NotePlayHandleManager::release(handle); }
		delete instrument;
		delete track;
		energy = rendered;
		return true;
	};

	double one = 0.0;
	double quarter = 0.0;
	double atCap = 0.0;
	double pastCap = 0.0;

	QVERIFY2(renderEnergy(1, one), "the render of one note did not finish");
	QVERIFY2(renderEnergy(256, quarter), "the render of 256 notes did not finish");
	// 512 notes are exactly one segment of the pool: the last count the old
	// fixed array could hold.
	QVERIFY2(renderEnergy(512, atCap), "the render of 512 notes did not finish");
	// 600 notes are past that segment. The old array held 512 and refused the
	// rest, so the level of 600 notes was the level of 512 notes exactly.
	QVERIFY2(renderEnergy(600, pastCap), "the render of 600 notes did not finish");

	QVERIFY2(one > 1e-4, "a single note rendered silence");
	QVERIFY2(atCap > 1.9 * quarter,
		qPrintable(QString{"the render does not grow with the note count: 256 notes summed %1, "
			"512 notes summed %2"}.arg(quarter, 0, 'g', 8).arg(atCap, 0, 'g', 8)));
	// 600 notes are 1.17 times the level of 512; a pool that refused the notes
	// past 512 adds exactly nothing.
	QVERIFY2(pastCap > 1.1 * atCap,
		qPrintable(QString{"the notes past 512 were dropped: 512 notes summed %1, "
			"600 notes summed %2"}.arg(atCap, 0, 'g', 8).arg(pastCap, 0, 'g', 8)));
#endif
}


//! Every matching layer of one note must play: the per-note sample cap is gone.
//!
//! The voice pool used to give a note at most eight voices and silently skip
//! the matching regions after that. The layered fixture has twelve regions
//! that all match the same key, so one note has twelve matching samples, all
//! starting together and therefore in phase. The energy of that note's render
//! is twelve times the energy of a single-layer note; with the old cap it was
//! eight times, so a bound between the two tells them apart.
void PluginPortsMigrationTest::gigplayerKeepsEveryLayerOfANote()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	const auto renderEnergyOf = [&](const QString& fixture, double& energy) -> bool {
		if (!QFileInfo{fixture}.exists()) { return false; }

		partc::prepareInstrumentEnvironment("gigplayer");
		auto* track = new lmms::InstrumentTrack(lmms::Engine::getSong());
		auto* instrument = static_cast<lmms::Instrument*>(entry(track, nullptr));
		if (instrument == nullptr) { return false; }

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("instrument"));
		QDomElement saved = instrument->saveState(doc, root);
		saved.setAttribute(QStringLiteral("src"), fixture);
		saved.setAttribute(QStringLiteral("patch"), QStringLiteral("0"));
		saved.setAttribute(QStringLiteral("bank"), QStringLiteral("0"));
		instrument->restoreState(saved);

		constexpr int Periods = 16;
		const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
		lmms::InstrumentTrack* instrumentTrack = instrument->instrumentTrack();
		const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * Periods)},
			lmms::TimePos{0}, lmms::DefaultKey};
		lmms::NotePlayHandle* handle = lmms::NotePlayHandleManager::acquire(
			instrumentTrack, 0, frames * Periods, note);

		std::vector<lmms::SampleFrame> data(static_cast<std::size_t>(frames));
		double rendered = 0.0;
		for (int period = 0; period < Periods; ++period)
		{
			std::fill(data.begin(), data.end(), lmms::SampleFrame{0.0f, 0.0f});
			const std::span<lmms::SampleFrame> span{data};
			instrument->playNote(handle, span);
			instrument->play(span);

			for (const auto& frame : data)
			{
				rendered += std::fabs(frame.left()) + std::fabs(frame.right());
			}
		}

		lmms::NotePlayHandleManager::release(handle);
		delete instrument;
		delete track;
		energy = rendered;
		return true;
	};

	// Both fixtures are constant-level, so the energy of a note is exactly the
	// number of its playing layers times the energy of one layer.
	const QString oneLayer = QDir{QString{QT_TESTCASE_SOURCEDIR}}
		.filePath(QStringLiteral("data/gigproof/dc.gig"));
	const QString layered = QDir{QString{QT_TESTCASE_SOURCEDIR}}
		.filePath(QStringLiteral("data/gigproof/layered12.gig"));

	double one = 0.0;
	double twelve = 0.0;
	QVERIFY2(renderEnergyOf(oneLayer, one), "the single-layer fixture did not render");
	QVERIFY2(renderEnergyOf(layered, twelve), "the layered fixture did not render");
	QVERIFY2(one > 1e-4, "the single-layer note rendered silence");

	QVERIFY2(twelve > 9.0 * one,
		qPrintable(QString{"the note kept only a fraction of its matching layers: one layer "
			"summed %1, twelve matching regions summed %2; the eight-voice cap is back"}
			.arg(one, 0, 'g', 8).arg(twelve, 0, 'g', 8)));
#endif
}


//! A note must survive a global voice-pool exhaustion and then play.
//!
//! The starting pool holds 512 voices. Forty-two notes of the twelve-layer
//! fixture take 504 of them, leaving eight: the next note cannot get its twelve
//! voices, rolls the pass back and asks for another segment. Once the pool has
//! grown, all twenty notes that follow play their twelve layers. With the old
//! behaviour the pool had no room at all, so those notes would contribute
//! nothing and the energy would stay at the level of forty-two notes.
void PluginPortsMigrationTest::gigplayerSurvivesGlobalVoicePoolExhaustion()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	// The growth service is the driver, not the claim: a build without it never
	// grows the pool, and the level assertion below decides the case.
	using GrowthService = void (*)(lmms::Instrument*);
	const auto serviceGrowth = reinterpret_cast<GrowthService>(
		lib->resolve("lmms_gigplayer_service_pool_growth"));
	const auto grow = [&](lmms::Instrument* instrument) {
		if (serviceGrowth != nullptr) { serviceGrowth(instrument); }
	};

	// Every note takes exactly one voice, so what runs out is the global voice
	// pool and not a per-note layer cap. The fixture is constant-level, so the
	// energy is exactly proportional to the number of notes that played.
	const QString single = QDir{QString{QT_TESTCASE_SOURCEDIR}}
		.filePath(QStringLiteral("data/gigproof/dc.gig"));
	QVERIFY2(QFile::exists(single),
		qPrintable(QString{"missing DC fixture %1: regenerate it with make_gig --dc"}
			.arg(single)));

	const auto renderNotes = [&](int fillers, int late, double& energy) -> bool {
		partc::prepareInstrumentEnvironment("gigplayer");
		auto* track = new lmms::InstrumentTrack(lmms::Engine::getSong());
		auto* instrument = static_cast<lmms::Instrument*>(entry(track, nullptr));
		if (instrument == nullptr) { return false; }

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("instrument"));
		QDomElement saved = instrument->saveState(doc, root);
		saved.setAttribute(QStringLiteral("src"), single);
		saved.setAttribute(QStringLiteral("patch"), QStringLiteral("0"));
		saved.setAttribute(QStringLiteral("bank"), QStringLiteral("0"));
		instrument->restoreState(saved);

		constexpr int Periods = 32;
		const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
		lmms::InstrumentTrack* instrumentTrack = instrument->instrumentTrack();
		const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * Periods)},
			lmms::TimePos{0}, lmms::DefaultKey};

		std::vector<lmms::NotePlayHandle*> handles;
		handles.reserve(static_cast<std::size_t>(fillers + late));
		for (int i = 0; i < fillers + late; ++i)
		{
			handles.push_back(lmms::NotePlayHandleManager::acquire(instrumentTrack, 0,
				frames * Periods, note));
		}

		std::vector<lmms::SampleFrame> data(static_cast<std::size_t>(frames));
		double rendered = 0.0;
		for (int period = 0; period < Periods; ++period)
		{
			std::fill(data.begin(), data.end(), lmms::SampleFrame{0.0f, 0.0f});
			const std::span<lmms::SampleFrame> span{data};

			// The fillers come first, so they take the voices of the starting
			// pool before the late notes (the last handles) are offered.
			for (auto* handle : handles) { instrument->playNote(handle, span); }
			instrument->play(span);
			grow(instrument);

			for (const auto& frame : data)
			{
				rendered += std::fabs(frame.left()) + std::fabs(frame.right());
			}
		}

		for (auto* handle : handles) { lmms::NotePlayHandleManager::release(handle); }
		delete instrument;
		delete track;
		energy = rendered;
		return true;
	};

	// The starting voice pool is 8 segments of 64 voices: one voice per filler
	// note takes every one of them, so the notes that follow find it empty.
	constexpr int Fillers = 512;
	constexpr int Late = 64;

	double fillersOnly = 0.0;
	double withLate = 0.0;
	QVERIFY2(renderNotes(Fillers, 0, fillersOnly), "the filler render did not finish");
	QVERIFY2(renderNotes(Fillers, Late, withLate), "the exhaustion render did not finish");

	QVERIFY2(fillersOnly > 1e-4, "the filler notes rendered silence");
	// 576 notes are 1.125 times the level of 512. A pool that dropped the notes
	// that found it empty adds exactly nothing.
	QVERIFY2(withLate > 1.06 * fillersOnly,
		qPrintable(QString{"the notes that found the voice pool empty never played: 512 notes "
			"summed %1, 512 notes plus 64 after the pool grew summed %2"}
			.arg(fillersOnly, 0, 'g', 8).arg(withLate, 0, 'g', 8)));
#endif
}


void PluginPortsMigrationTest::gigplayerSingleFrameLoopRepeatsFrame()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	const QString fixture = QDir{QString{QT_TESTCASE_SOURCEDIR}}
		.filePath(QStringLiteral("data/gigproof/single-frame-loop.gig"));
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("instrument"));
	QDomElement saved = instrument->saveState(doc, root);
	saved.setAttribute(QStringLiteral("src"), fixture);
	saved.setAttribute(QStringLiteral("patch"), QStringLiteral("0"));
	saved.setAttribute(QStringLiteral("bank"), QStringLiteral("0"));
	instrument->restoreState(saved);

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const auto rendered = partc::renderInstrumentInFreshThread(*instrument, frames, lmms::DefaultKey);
	delete instrument;

	constexpr int LoopEnd = 65;
	constexpr int FirstRepeat = LoopEnd + 5;
	constexpr int LaterRepeat = LoopEnd + 25;
	constexpr double FixtureRate = 44100.0;
	const double outputRate = lmms::Engine::audioEngine()->outputSampleRate();
	const auto first = static_cast<std::size_t>(std::lround(FirstRepeat * outputRate / FixtureRate));
	const auto later = static_cast<std::size_t>(std::lround(LaterRepeat * outputRate / FixtureRate));
	QVERIFY(rendered.size() > 2 * later);

	const float firstRepeat = rendered[2 * first];
	const float laterRepeat = rendered[2 * later];
	QVERIFY(std::fabs(firstRepeat) > 1e-4f);
	QVERIFY2(std::fabs(laterRepeat - firstRepeat) < 0.001f,
		qPrintable(QString{"one-frame loop should repeat frame 64, got %1 then %2"}
			.arg(laterRepeat, 0, 'g', 6).arg(firstRepeat, 0, 'g', 6)));
#endif
}

// The GIG player mixes EVERY sample of EVERY note into ONE shared output
// buffer per period. A sample whose resampler made no progress contributes
// nothing, and it must not touch the frames the other samples have already
// mixed into that buffer.
void PluginPortsMigrationTest::gigplayerNoProgressFallbackKeepsEarlierSamples()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	// The mixing step of the render path, exported by the module so that the
	// no-progress case can be driven with an injected resampler result.
	using MixResampledBlock = lmms::f_cnt_t (*)(lmms::SampleFrame*, lmms::f_cnt_t, lmms::f_cnt_t,
		const lmms::SampleFrame*, lmms::f_cnt_t, lmms::f_cnt_t);
	auto mix = reinterpret_cast<MixResampledBlock>(lib->resolve("gigplayerMixResampledBlock"));
	QVERIFY2(mix != nullptr, "the GigPlayer module does not export gigplayerMixResampledBlock");

	constexpr lmms::f_cnt_t frames = 64;
	std::array<lmms::SampleFrame, frames> shared{};

	// Another sample of the same note has already mixed its audio into the
	// shared buffer of this period.
	shared[0] = lmms::SampleFrame{0.5f, -0.25f};
	shared[1] = lmms::SampleFrame{0.25f, 0.125f};

	// A sample whose resampler made no progress: it consumed no input and
	// produced no output.
	const std::array<lmms::SampleFrame, 4> notProduced{};
	const auto mixedNothing = mix(shared.data(), 0, frames, notProduced.data(), 0, 0);
	QCOMPARE(mixedNothing, lmms::f_cnt_t{0});

	QVERIFY2(shared[0].left() == 0.5f && shared[0].right() == -0.25f,
		qPrintable(QString{"the no-progress fallback erased the audio of the earlier samples: %1, %2"}
			.arg(shared[0].left(), 0, 'g', 6).arg(shared[0].right(), 0, 'g', 6)));
	QVERIFY2(shared[1].left() == 0.25f && shared[1].right() == 0.125f,
		qPrintable(QString{"the no-progress fallback erased the audio of the earlier samples: %1, %2"}
			.arg(shared[1].left(), 0, 'g', 6).arg(shared[1].right(), 0, 'g', 6)));

	// The frames this sample did not fill stay silent.
	for (lmms::f_cnt_t i = 2; i < frames; ++i)
	{
		QVERIFY(shared[i].left() == 0.0f && shared[i].right() == 0.0f);
	}

	// A sample that DID produce frames adds them to the shared buffer; it
	// never overwrites what the other samples mixed in.
	const std::array<lmms::SampleFrame, 4> produced{
		lmms::SampleFrame{0.25f, 0.125f}, lmms::SampleFrame{0.5f, 0.25f},
		lmms::SampleFrame{0.125f, 0.5f}, lmms::SampleFrame{0.25f, 0.25f}};
	const auto mixedFrames = mix(shared.data(), 0, frames, produced.data(), 8, produced.size());
	QCOMPARE(mixedFrames, lmms::f_cnt_t{produced.size()});
	QCOMPARE(shared[0].left(), 0.75f);
	QCOMPARE(shared[0].right(), -0.125f);
	QCOMPARE(shared[1].left(), 0.75f);
	QCOMPARE(shared[1].right(), 0.375f);
	QCOMPARE(shared[2].left(), 0.125f);
	QCOMPARE(shared[2].right(), 0.5f);
#endif
}


void PluginPortsMigrationTest::dispersionRemainsStableAboveNyquist()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* device = lmms::Engine::audioEngine()->audioDev();
	QVERIFY(device != nullptr);
	const auto originalRate = device->sampleRate();
	struct RestoreSampleRate
	{
		lmms::AudioDevice* device;
		lmms::sample_rate_t rate;
		~RestoreSampleRate() { device->setSampleRateForTesting(rate); }
	} restoreRate{device, originalRate};

	device->setSampleRateForTesting(32000);
	QCOMPARE(device->sampleRate(), 32000);

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_dispersion)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto effect = std::unique_ptr<lmms::Effect>{static_cast<lmms::Effect*>(entry(nullptr, nullptr))};
	QVERIFY(effect != nullptr);

	QDomDocument doc;
	QDomElement root = doc.createElement("effect");
	root.setAttribute("on", 1);
	root.setAttribute("wet", 1);
	root.setAttribute("autoquit", 0);
	QDomElement controls = effect->controls()->saveState(doc, root);
	controls.setAttribute("amount", "1");
	QDomElement frequency = controls.firstChildElement("freq");
	QVERIFY(!frequency.isNull());
	frequency.setAttribute("value", "20000");
	QDomElement resonance = controls.firstChildElement("reso");
	QVERIFY(!resonance.isNull());
	resonance.setAttribute("value", "0.707");
	controls.setAttribute("feedback", "0");
	controls.setAttribute("dc", "0");
	effect->loadSettings(root);
	QDomDocument actualDoc;
	QDomElement actualRoot = actualDoc.createElement("effect");
	const QDomElement actualControls = effect->controls()->saveState(actualDoc, actualRoot);
	QCOMPARE(actualControls.attribute("amount").toInt(), 1);
	QCOMPARE(actualControls.firstChildElement("freq").attribute("value").toFloat(), 20000.0f);

	constexpr lmms::f_cnt_t frames = 128;
	std::array<lmms::SampleFrame, frames> buffer{};
	lmms::SampleFrame* busPointers[] = {buffer.data()};
	float peak = 0.0f;
	for (int block = 0; block < 32; ++block)
	{
		buffer.fill(lmms::SampleFrame{0.0f, 0.0f});
		if (block == 0) { buffer[0] = lmms::SampleFrame{1.0f, 1.0f}; }
		lmms::AudioBus bus{busPointers, 1, frames};
		effect->processAudioBuffer(bus);
		for (const auto& sample : buffer)
		{
			QVERIFY(std::isfinite(sample.left()) && std::isfinite(sample.right()));
			peak = std::max(peak, std::max(std::abs(sample.left()), std::abs(sample.right())));
		}
	}
	qInfo("Dispersion high-frequency render peak=%g", peak);
	QVERIFY(peak < 1000.0f);
}

void PluginPortsMigrationTest::dynamicsProcessorRejectsShortWaveShape()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto module = std::find_if(migratedModules().begin(), migratedModules().end(),
		[](const MigratedModule& candidate) { return std::string{candidate.plugin} == "dynamicsprocessor"; });
	QVERIFY(module != migratedModules().end());

	QLibrary library{QString::fromUtf8(module->path)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	std::unique_ptr<lmms::Effect> effect{
		static_cast<lmms::Effect*>(entry(nullptr, nullptr))};
	QVERIFY(effect != nullptr);

	QDomDocument inputDoc;
	QDomElement inputRoot = inputDoc.createElement("effect");
	auto controls = effect->controls()->saveState(inputDoc, inputRoot);
	const std::array<float, 1> shortShape{0.9f};
	QString encodedShortShape;
	lmms::base64::encode(reinterpret_cast<const char*>(shortShape.data()),
		static_cast<int>(sizeof(shortShape)), encodedShortShape);
	controls.setAttribute(QStringLiteral("waveShape"), encodedShortShape);
	effect->loadSettings(inputRoot);

	std::array<float, 200> defaultShape{};
	for (int i = 0; i < static_cast<int>(defaultShape.size()); ++i)
	{
		defaultShape[i] = (static_cast<float>(i) + 1.0f) / 200.0f;
	}
	QString expectedWaveShape;
	lmms::base64::encode(reinterpret_cast<const char*>(defaultShape.data()),
		static_cast<int>(sizeof(defaultShape)), expectedWaveShape);

	QDomDocument outputDoc;
	QDomElement outputRoot = outputDoc.createElement("effect");
	const auto savedControls = effect->controls()->saveState(outputDoc, outputRoot);
	QCOMPARE(savedControls.attribute(QStringLiteral("waveShape")), expectedWaveShape);
}

/*!
 * Negative control: the comparison above must be able to fail. Comparing two
 * *different* plugins' renders has to produce different checksums; otherwise
 * the harness would be comparing silence with silence and prove nothing.
 */
void PluginPortsMigrationTest::comparisonIsSensitive()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QVERIFY(m_migrated.size() >= 2);
	QVERIFY(m_reference.size() == m_migrated.size());

	const auto& a = m_migrated[0];
	const auto& b = m_reference[1];
	const float worst = partc::maxAbsDiff(a.samples, b.samples);

	qInfo().noquote()
		<< QString{"negative control: %1 (migrated) vs %2 (reference) max|delta|=%3"}
			   .arg(a.name, b.name)
			   .arg(worst, 0, 'g', 3);

	QVERIFY2(a.checksum != b.checksum, "distinct plugins rendered identically");
	QVERIFY2(worst > 1e-6f, "comparison is not sensitive to real differences");
}

/*!
 * Bounded fake SID clock for the short-segment regression: it emits one sample
 * after 22 cycles and models a one-cycle pending register write that a later
 * write would overwrite if the scheduler spent more cycles than it was given.
 */
struct SidClockProbe
{
	int cyclesToSample = 22;
	int clockedCycles = 0;
	int lastWriteAddress = -1;
	int overwrittenPendingWrites = 0;
	bool writePending = false;

	void write(unsigned char address, unsigned char)
	{
		if (writePending) { ++overwrittenPendingWrites; }
		lastWriteAddress = address;
		writePending = true;
	}

	int clock(int& cycles, short* buffer, int samples)
	{
		if (cycles <= 0 || samples <= 0) { return 0; }

		const int used = std::min(cycles, cyclesToSample);
		if (used > 0) { writePending = false; }
		cycles -= used;
		cyclesToSample -= used;
		clockedCycles += used;
		if (cyclesToSample > 0) { return 0; }

		buffer[0] = 0;
		cyclesToSample = 22;
		return 1;
	}
};

void PluginPortsMigrationTest::sidShortSegmentsKeepRegisterWritesOrdered()
{
	lmms::sidplayback::RegisterWriteProgress progress;
	SidClockProbe sid;
	std::array<unsigned char, lmms::sidplayback::NumSidRegisters> registers{};
	std::array<short, 1> output{};

	const int samples = lmms::sidplayback::fillBuffer(registers.data(), &sid,
		lmms::sidplayback::RegisterWriteOrder, 26, output.data(), 1, progress);

	QCOMPARE(samples, 1);
	QVERIFY(sid.clockedCycles <= 26);
	QCOMPARE(sid.overwrittenPendingWrites, 0);
	QVERIFY(!sid.writePending);
	QVERIFY(sid.lastWriteAddress != lmms::sidplayback::RegisterWriteOrder.back());
	QCOMPARE(progress.registerIndex, std::size_t{2});
	QVERIFY(progress.cyclesRemaining > 0);

	const int cyclesBeforeNextSegment = sid.clockedCycles;
	const int nextSamples = lmms::sidplayback::fillBuffer(registers.data(), &sid,
		lmms::sidplayback::RegisterWriteOrder, 26, output.data(), 1, progress);
	QCOMPARE(nextSamples, 1);
	QVERIFY(sid.clockedCycles - cyclesBeforeNextSegment <= 26);
	QCOMPARE(sid.overwrittenPendingWrites, 0);
	QVERIFY(progress.registerIndex > 2);
}

/*!
 * Cluster new-p9-521 (port): Watsyn must not allocate on the note path, and
 * two notes that sound at the same time must not share render buffers.
 *
 * The allocation count comes from the replacement operator new this file
 * already defines (it feeds partc::RealtimeAllocationWatch), so the test
 * binary keeps exactly ONE replacement allocation function: the shared
 * zene_allocation_probe cannot be linked here, it would define a second
 * operator new and the link would fail with a duplicate symbol.
 *
 * The reference pass renders every note on its own; the concurrent pass starts
 * the same notes together on worker threads. Each worker compares its samples
 * with its own reference and counts the allocations its own thread made.
 */
void PluginPortsMigrationTest::watsynOverlappingNotesAreIndependentAndAllocationFree()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto& instruments = migratedInstruments();
	const auto module = std::find_if(instruments.begin(), instruments.end(),
		[](const auto& candidate) { return std::string{candidate.plugin} == "watsyn"; });
	QVERIFY(module != instruments.end());

	auto* lib = new QLibrary{QString::fromUtf8(module->path)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	const auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	lmms::NotePlayHandleManager::init();
	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	constexpr std::size_t concurrentNotes = 8;
	constexpr int periods = 16;
	const std::array<int, concurrentNotes> keys{36, 40, 43, 47, 50, 55, 59, 64};
	const auto acquireNote = [&](int key) {
		const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * periods)},
			lmms::TimePos{0}, key};
		return lmms::NotePlayHandleManager::acquire(track.get(), 0, frames * periods, note, nullptr, -1,
			lmms::NotePlayHandle::Origin::MidiClip);
	};
	const auto releaseNote = [instrument](lmms::NotePlayHandle* handle) {
		if (handle->m_pluginData != nullptr)
		{
			instrument->deleteNotePluginData(handle);
			handle->m_pluginData = nullptr;
		}
		lmms::NotePlayHandleManager::release(handle);
	};
	const auto spanOf = [frames](std::vector<lmms::SampleFrame>& buffer, int period) {
		return std::span<lmms::SampleFrame>{buffer.data() + period * frames, static_cast<std::size_t>(frames)};
	};

	// Reference: every note rendered on its own, one after the other.
	std::array<std::vector<lmms::SampleFrame>, concurrentNotes> expected;
	for (std::size_t i = 0; i < concurrentNotes; ++i)
	{
		expected[i].resize(static_cast<std::size_t>(frames) * periods);
		auto* handle = acquireNote(keys[i]);
		for (int period = 0; period < periods; ++period)
		{
			instrument->playNote(handle, spanOf(expected[i], period));
		}
		releaseNote(handle);
	}

	// Concurrent: the same notes, started together on worker threads.
	std::array<lmms::NotePlayHandle*, concurrentNotes> handles{};
	std::array<std::vector<lmms::SampleFrame>, concurrentNotes> actual;
	std::array<std::size_t, concurrentNotes> allocations{};
	for (std::size_t i = 0; i < concurrentNotes; ++i)
	{
		actual[i].resize(static_cast<std::size_t>(frames) * periods);
		handles[i] = acquireNote(keys[i]);
	}

	std::barrier start{static_cast<std::ptrdiff_t>(concurrentNotes + 1)};
	std::array<std::thread, concurrentNotes> workers;
	for (std::size_t i = 0; i < concurrentNotes; ++i)
	{
		workers[i] = std::thread{[&, i] {
			start.arrive_and_wait();
			partc::RealtimeAllocationWatch::begin();
			for (int period = 0; period < periods; ++period)
			{
				instrument->playNote(handles[i], spanOf(actual[i], period));
			}
			const auto counts = partc::RealtimeAllocationWatch::end();
			allocations[i] = counts.allocations;
		}};
	}
	start.arrive_and_wait();
	for (auto& worker : workers)
	{
		worker.join();
	}

	float maxDelta = 0.0f;
	for (std::size_t i = 0; i < concurrentNotes; ++i)
	{
		for (std::size_t frame = 0; frame < actual[i].size(); ++frame)
		{
			maxDelta = std::max(maxDelta, std::abs(expected[i][frame].left() - actual[i][frame].left()));
			maxDelta = std::max(maxDelta, std::abs(expected[i][frame].right() - actual[i][frame].right()));
		}
		releaseNote(handles[i]);
	}
	delete instrument;

	const auto allocationFree = std::all_of(allocations.begin(), allocations.end(),
		[](std::size_t count) { return count == 0; });
	qInfo("Watsyn overlap max|delta|=%g worker note allocations=%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu",
		static_cast<double>(maxDelta), allocations[0], allocations[1], allocations[2], allocations[3],
		allocations[4], allocations[5], allocations[6], allocations[7]);
	QVERIFY2(maxDelta <= 1e-6f, "concurrent Watsyn notes contaminated each other's output");
	QVERIFY2(allocationFree, "Watsyn note start allocated on a rendering worker");
}

/*!
 * Cluster new-p9-521 (port, round 2): Watsyn must serve EVERY note the note
 * manager admits. The first fix kept the per-note state in a pool of 256
 * preallocated voices and silenced every note beyond the 256th, while the note
 * manager grows its cache as needed and has no such limit - a supported
 * workload lost a period or a whole short note.
 *
 * The test plays more notes than that pool had (320), all of them sounding at
 * the same time, and checks that every one of them renders its own audio and
 * that no rendering worker allocates.
 */
void PluginPortsMigrationTest::watsynServesEveryAdmittedNoteWithoutAllocating()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto& instruments = migratedInstruments();
	const auto module = std::find_if(instruments.begin(), instruments.end(),
		[](const auto& candidate) { return std::string{candidate.plugin} == "watsyn"; });
	QVERIFY(module != instruments.end());

	auto* lib = new QLibrary{QString::fromUtf8(module->path)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	const auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	lmms::NotePlayHandleManager::init();
	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	// More notes than the voice pool the first fix used.
	constexpr std::size_t noteCount = 320;
	constexpr std::size_t workerCount = 8;
	const auto keyFor = [](std::size_t index) { return 24 + static_cast<int>(index % 80); };
	const auto acquireNote = [&](int key) {
		const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * 4)},
			lmms::TimePos{0}, key};
		return lmms::NotePlayHandleManager::acquire(track.get(), 0, frames * 4, note, nullptr, -1,
			lmms::NotePlayHandle::Origin::MidiClip);
	};
	const auto releaseNote = [instrument](lmms::NotePlayHandle* handle) {
		if (handle->m_pluginData != nullptr)
		{
			instrument->deleteNotePluginData(handle);
			handle->m_pluginData = nullptr;
		}
		lmms::NotePlayHandleManager::release(handle);
	};

	// Reference: every note rendered on its own.
	std::vector<std::vector<lmms::SampleFrame>> expected(noteCount);
	for (std::size_t i = 0; i < noteCount; ++i)
	{
		expected[i].resize(static_cast<std::size_t>(frames));
		auto* handle = acquireNote(keyFor(i));
		instrument->playNote(handle, std::span<lmms::SampleFrame>{expected[i]});
		releaseNote(handle);
	}

	// All notes at once: every note is sounding while the others render.
	std::vector<lmms::NotePlayHandle*> handles(noteCount, nullptr);
	std::vector<std::vector<lmms::SampleFrame>> actual(noteCount);
	for (std::size_t i = 0; i < noteCount; ++i)
	{
		actual[i].resize(static_cast<std::size_t>(frames));
		handles[i] = acquireNote(keyFor(i));
	}

	std::vector<std::size_t> allocations(workerCount, 0);
	std::vector<std::thread> workers;
	for (std::size_t w = 0; w < workerCount; ++w)
	{
		workers.emplace_back([&, w] {
			partc::RealtimeAllocationWatch::begin();
			for (std::size_t i = w; i < noteCount; i += workerCount)
			{
				instrument->playNote(handles[i], std::span<lmms::SampleFrame>{actual[i]});
			}
			const auto counts = partc::RealtimeAllocationWatch::end();
			allocations[w] = counts.allocations;
		});
	}
	for (auto& worker : workers)
	{
		worker.join();
	}

	float maxDelta = 0.0f;
	int silentNotes = 0;
	for (std::size_t i = 0; i < noteCount; ++i)
	{
		float peak = 0.0f;
		for (std::size_t frame = 0; frame < actual[i].size(); ++frame)
		{
			maxDelta = std::max(maxDelta, std::abs(expected[i][frame].left() - actual[i][frame].left()));
			maxDelta = std::max(maxDelta, std::abs(expected[i][frame].right() - actual[i][frame].right()));
			peak = std::max(peak, std::abs(actual[i][frame].left()));
			peak = std::max(peak, std::abs(actual[i][frame].right()));
		}
		if (peak <= 0.0f) { ++silentNotes; }
		releaseNote(handles[i]);
	}
	delete instrument;

	const auto allocationFree = std::all_of(allocations.begin(), allocations.end(),
		[](std::size_t count) { return count == 0; });
	qInfo("Watsyn %zu simultaneous notes: max|delta|=%g silent notes=%d worker allocations=%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu",
		noteCount, static_cast<double>(maxDelta), silentNotes, allocations[0], allocations[1],
		allocations[2], allocations[3], allocations[4], allocations[5], allocations[6], allocations[7]);
	QVERIFY2(silentNotes == 0, "Watsyn silenced a note it could have played");
	QVERIFY2(maxDelta <= 1e-6f, "simultaneous Watsyn notes contaminated each other's output");
	QVERIFY2(allocationFree, "Watsyn note start allocated on a rendering worker");
}

/*!
 * Cluster new-p9-521 (port, round 2): a Watsyn note keeps the wavetable it
 * started with.
 *
 * The upstream code copied the wavetables into the note's own object ("copy
 * wavegraphs to the synth object to prevent race conditions"). The snapshot
 * the fix shares between the notes must keep that promise: a note that is
 * already sounding renders the wave it started with, while the next note
 * renders the wave that was just edited.
 */
void PluginPortsMigrationTest::watsynKeepsTheWaveANoteStartedWith()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto& instruments = migratedInstruments();
	const auto module = std::find_if(instruments.begin(), instruments.end(),
		[](const auto& candidate) { return std::string{candidate.plugin} == "watsyn"; });
	QVERIFY(module != instruments.end());

	auto* lib = new QLibrary{QString::fromUtf8(module->path)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	const auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	lmms::NotePlayHandleManager::init();
	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const auto acquireNote = [&](int key) {
		const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * 4)},
			lmms::TimePos{0}, key};
		return lmms::NotePlayHandleManager::acquire(track.get(), 0, frames * 4, note, nullptr, -1,
			lmms::NotePlayHandle::Origin::MidiClip);
	};
	const auto releaseNote = [instrument](lmms::NotePlayHandle* handle) {
		if (handle->m_pluginData != nullptr)
		{
			instrument->deleteNotePluginData(handle);
			handle->m_pluginData = nullptr;
		}
		lmms::NotePlayHandleManager::release(handle);
	};
	const auto peakOf = [](const std::vector<lmms::SampleFrame>& buffer) {
		float peak = 0.0f;
		for (const auto& frame : buffer)
		{
			peak = std::max(peak, std::abs(frame.left()));
			peak = std::max(peak, std::abs(frame.right()));
		}
		return peak;
	};

	// The wave the instrument starts with, and the same wave with every
	// sample set to zero: an oscillator that reads it renders silence.
	QDomDocument doc;
	QDomElement root = doc.createElement("instrument");
	const QDomElement saved = instrument->saveState(doc, root);
	const auto silentWave = [](const QString& encoded) {
		int size = 0;
		char* data = nullptr;
		lmms::base64::decode(encoded, &data, &size);
		std::vector<float> zeros(static_cast<std::size_t>(size) / sizeof(float), 0.0f);
		QString out;
		lmms::base64::encode(reinterpret_cast<const char*>(zeros.data()), size, out);
		delete[] data;
		return out;
	};
	QDomElement silent = saved.cloneNode(true).toElement();
	for (const auto* name : {"a1_wave", "a2_wave", "b1_wave", "b2_wave"})
	{
		silent.setAttribute(name, silentWave(saved.attribute(name)));
	}

	// A note that is already sounding when the wave changes.
	auto* sounding = acquireNote(60);
	std::vector<lmms::SampleFrame> first(static_cast<std::size_t>(frames));
	instrument->playNote(sounding, std::span<lmms::SampleFrame>{first});

	instrument->restoreState(silent);

	std::vector<lmms::SampleFrame> second(static_cast<std::size_t>(frames));
	instrument->playNote(sounding, std::span<lmms::SampleFrame>{second});
	releaseNote(sounding);

	// A note that starts after the change renders the new wave.
	auto* fresh = acquireNote(60);
	std::vector<lmms::SampleFrame> third(static_cast<std::size_t>(frames));
	instrument->playNote(fresh, std::span<lmms::SampleFrame>{third});
	releaseNote(fresh);

	delete instrument;

	const float firstPeak = peakOf(first);
	const float secondPeak = peakOf(second);
	const float thirdPeak = peakOf(third);
	qInfo("Watsyn wave peaks: first period=%g second period=%g note after the edit=%g",
		static_cast<double>(firstPeak), static_cast<double>(secondPeak), static_cast<double>(thirdPeak));
	QVERIFY2(firstPeak > 0.0f, "the Watsyn note rendered no sound at all");
	QVERIFY2(secondPeak > 0.0f, "a sounding Watsyn note lost the wave it started with");
	QVERIFY2(thirdPeak <= 0.0f, "a new Watsyn note did not render the edited wave");
}

namespace
{

//! Reads one file of the Watsyn plugin's own source tree.
QString readWatsynSource( const QString& _name )
{
	QFile file{ QDir{ QStringLiteral( WATSYN_PLUGIN_SOURCE_DIR ) }.filePath( _name ) };
	if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) { return {}; }
	return QString::fromUtf8( file.readAll() );
}

//! The body of the function whose definition starts with _signature.
QString functionBodyStartingAt( const QString& _source, const QString& _signature )
{
	const auto start = _source.indexOf( _signature );
	if ( start < 0 ) { return {}; }
	const auto end = _source.indexOf( QStringLiteral( "\n}\n" ), start );
	if ( end < 0 ) { return {}; }
	return _source.mid( start, end - start );
}

}


/*!
 * Cluster new-p17-202 (and new-p9-524): Watsyn's oscillator parameters must
 * not travel from the editing thread to the audio thread through a plain
 * cache.
 *
 * The upstream code kept the gains and the frequencies in plain float arrays
 * that the editing thread wrote and the audio thread read, with nothing
 * between them: a data race, and the audio thread could read a value that was
 * being written. The parameters now travel through a published set that the
 * editing thread fills and the audio thread takes once per rendered period.
 *
 * The regression reads the plugin's own sources, so it says the same thing
 * on the unfixed code (where the plain caches are still there) and on the
 * fixed code. The hand-off's own behaviour - a reader that is taken while a
 * publication is in progress - is proved by
 * tests/src/plugins/WatsynParamHandoffTest.cpp.
 */
void PluginPortsMigrationTest::watsynPublishesItsOscillatorParametersThroughARealTimeSafeHandoff()
{
	const auto header = readWatsynSource( QStringLiteral( "Watsyn.h" ) );
	const auto source = readWatsynSource( QStringLiteral( "Watsyn.cpp" ) );
	QVERIFY2( !header.isEmpty() && !source.isEmpty(), "the Watsyn sources were not found" );

	// The plain caches the editing thread wrote and the audio thread read.
	const QStringList caches{ "m_lvol", "m_rvol", "m_lfreq", "m_rfreq" };
	for ( const auto& cache : caches )
	{
		QVERIFY2( !header.contains( cache ),
			qPrintable( QStringLiteral( "Watsyn.h still declares the plain parameter cache %1" ).arg( cache ) ) );
	}

	// The published set the instrument fills and the audio thread takes.
	QVERIFY2( header.contains( QStringLiteral( "WatsynParamHandoff" ) ),
		"Watsyn does not hand its oscillator parameters over through a real-time-safe set" );
	QVERIFY2( header.contains( QStringLiteral( "m_paramHandoff" ) ),
		"Watsyn does not keep a published set of oscillator parameters" );
	QVERIFY2( source.contains( QStringLiteral( "publishParams" ) ),
		"Watsyn never publishes its oscillator parameters" );

	const auto renderPath = functionBodyStartingAt( source, QStringLiteral( "void WatsynVoice::renderOutput(" ) );
	QVERIFY2( !renderPath.isEmpty(), "the render path of Watsyn was not found" );
	for ( const auto& cache : caches )
	{
		QVERIFY2( !renderPath.contains( cache ),
			qPrintable( QStringLiteral( "the Watsyn render path still reads the plain parameter cache %1" ).arg( cache ) ) );
	}
	QVERIFY2( renderPath.contains( QStringLiteral( "m_paramHandoff" ) ),
		"the Watsyn render path does not take the published set of oscillator parameters" );

	// A note takes the published set, never a reading of the models: a
	// reading of the models could combine values from different points of a
	// multi-parameter update. The parameters are computed once, by the
	// editing thread, and published.
	QVERIFY2( !renderPath.contains( QStringLiteral( "computeParams" ) ),
		"the Watsyn render path recomputes the oscillator parameters from the models" );
	const auto voiceConstruction = functionBodyStartingAt( source, QStringLiteral( "WatsynVoice::WatsynVoice(" ) );
	QVERIFY2( !voiceConstruction.isEmpty(), "the Watsyn voice was not found" );
	QVERIFY2( !voiceConstruction.contains( QStringLiteral( "computeParams" ) ),
		"a Watsyn note seeds its parameters from a reading of the models" );
	QCOMPARE( source.count( QStringLiteral( "computeParams" ) ), 2 );

	// The reclamation protocol's ordering. The published pointer's load and
	// the exchange that publishes it have to be in the same total order as
	// the claimer counter (seq_cst): a load that is only an acquire may read
	// a STALE pointer, and a stale pointer is a set the editing thread has
	// already retired and may be filling again.
	const auto handoff = readWatsynSource( QStringLiteral( "WatsynParamHandoff.h" ) );
	QVERIFY2( !handoff.isEmpty(), "the Watsyn parameter hand-off was not found" );
	const auto lineOf = [&handoff]( const QString& _needle ) {
		for ( const auto& line : handoff.split( '\n' ) )
		{
			const auto trimmed = line.trimmed();
			// The protocol is written out in the comment above the code, so
			// only the code itself is looked at here.
			if ( trimmed.startsWith( '*' ) || trimmed.startsWith( "//" ) ) { continue; }
			if ( line.contains( _needle ) ) { return line; }
		}
		return QString();
	};
	const auto loadLine = lineOf( QStringLiteral( "m_published.load( std::memory_order" ) );
	QVERIFY2( !loadLine.isEmpty(), "the hand-off does not read the published pointer" );
	QVERIFY2( loadLine.contains( QStringLiteral( "seq_cst" ) ),
		"the hand-off reads the published pointer with a weaker order than the claimer counter: "
		"a reader could observe a set the editing thread has already retired" );
	const auto exchangeLine = lineOf( QStringLiteral( "m_published.exchange( fresh" ) );
	QVERIFY2( !exchangeLine.isEmpty(), "the hand-off does not publish the pointer" );
	QVERIFY2( exchangeLine.contains( QStringLiteral( "seq_cst" ) ),
		"the hand-off publishes the pointer with a weaker order than the claimer counter" );

	// The claim window, in the code: a reader announces itself BEFORE it
	// observes the published pointer, and leaves the window only AFTER it has
	// pinned the set it observed. The editing thread's recycle() refuses to
	// recycle while the claimer count is non-zero, so a set a reader could
	// still copy cannot be put back into the free list. The interleaving
	// itself is exercised by
	// WatsynParamHandoffTest::anIndependentReaderPausedInTheClaimWindowKeepsItsSetAlive().
	const auto claimIn = handoff.indexOf( QStringLiteral( "m_claimers.fetch_add( 1," ) );
	const auto observe = handoff.indexOf( QStringLiteral( "Set* set = m_published.load(" ) );
	const auto pin = handoff.indexOf( QStringLiteral( "set->pins.fetch_add( 1," ) );
	const auto claimOut = handoff.indexOf( QStringLiteral( "m_claimers.fetch_sub( 1," ) );
	QVERIFY2( claimIn >= 0 && observe >= 0 && pin >= 0 && claimOut >= 0,
		"the hand-off's claim window is not the one this regression describes" );
	QVERIFY2( claimIn < observe,
		"a reader observes the published pointer before it announces its claim: the editing "
		"thread could recycle the observed set under it" );
	QVERIFY2( observe < pin && pin < claimOut,
		"a reader leaves the claim window before it has pinned the set it observed" );
	QVERIFY2( handoff.contains( QStringLiteral( "m_claimers.load( std::memory_order_seq_cst ) != 0" ) ),
		"the editing thread does not refuse to recycle while a reader is inside the claim window" );

	// The seam is test-only. It is the one thing added to the audio-thread
	// read for the regression, and the shipped plugin must not contain it:
	// the type, the setter, the branch and the members are all behind
	// LMMS_TESTING. The build proves the hook is inert in production (the
	// plugin module has no such symbol); this says it cannot creep back.
	bool sawTestGuard = false;
	bool everySeamLineGuarded = true;
	int testGuardDepth = 0;
	for ( const auto& line : handoff.split( '\n' ) )
	{
		const auto trimmed = line.trimmed();
		if ( trimmed.startsWith( QStringLiteral( "#ifdef LMMS_TESTING" ) ) ) { ++testGuardDepth; sawTestGuard = true; }
		else if ( trimmed.startsWith( QStringLiteral( "#endif" ) ) && testGuardDepth > 0 ) { --testGuardDepth; }
		if ( line.contains( QStringLiteral( "m_claimHook" ) ) && testGuardDepth == 0 ) { everySeamLineGuarded = false; }
	}
	QVERIFY2( sawTestGuard, "the hand-off does not compile its test seam out of the shipped plugin" );
	QVERIFY2( everySeamLineGuarded,
		"a claim-window seam member is outside #ifdef LMMS_TESTING, so the shipped plugin carries it" );
}


/*!
 * The oscillator parameters still reach a note that is already sounding: a
 * volume of zero silences it, and a volume that is set back makes it sound
 * again. The hand-off must not cost the instrument its parameters.
 */
void PluginPortsMigrationTest::watsynAppliesOscillatorParameterChangesToASoundingNote()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto& instruments = migratedInstruments();
	const auto module = std::find_if(instruments.begin(), instruments.end(),
		[](const auto& candidate) { return std::string{candidate.plugin} == "watsyn"; });
	QVERIFY(module != instruments.end());

	auto* lib = new QLibrary{QString::fromUtf8(module->path)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);

	const auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	lmms::NotePlayHandleManager::init();
	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * 4)}, lmms::TimePos{0}, 60};
	auto* handle = lmms::NotePlayHandleManager::acquire(track.get(), 0, frames * 4, note, nullptr, -1,
		lmms::NotePlayHandle::Origin::MidiClip);
	QVERIFY(handle != nullptr);

	const auto peakOf = [](const std::vector<lmms::SampleFrame>& buffer) {
		float peak = 0.0f;
		for (const auto& frame : buffer)
		{
			peak = std::max(peak, std::abs(frame.left()));
			peak = std::max(peak, std::abs(frame.right()));
		}
		return peak;
	};

	QDomDocument doc;
	QDomElement root = doc.createElement("instrument");
	const QDomElement saved = instrument->saveState(doc, root);
	const auto withVolumes = [&](const char* volume) {
		QDomElement state = saved.cloneNode(true).toElement();
		for (const auto* name : {"a1_vol", "a2_vol", "b1_vol", "b2_vol"})
		{
			state.setAttribute(name, volume);
		}
		instrument->restoreState(state);
	};

	const auto render = [&](std::vector<lmms::SampleFrame>& buffer) {
		instrument->playNote(handle, std::span<lmms::SampleFrame>{buffer});
	};

	std::vector<lmms::SampleFrame> loud(static_cast<std::size_t>(frames));
	render(loud);
	QVERIFY2(peakOf(loud) > 0.0f, "the Watsyn note rendered no sound at all");

	withVolumes("0");
	std::vector<lmms::SampleFrame> silent(static_cast<std::size_t>(frames));
	render(silent);
	QCOMPARE(peakOf(silent), 0.0f);

	withVolumes("100");
	std::vector<lmms::SampleFrame> loudAgain(static_cast<std::size_t>(frames));
	render(loudAgain);
	QVERIFY2(peakOf(loudAgain) > 0.0f, "the Watsyn note did not come back after its volume was restored");

	if (handle->m_pluginData != nullptr)
	{
		instrument->deleteNotePluginData(handle);
		handle->m_pluginData = nullptr;
	}
	lmms::NotePlayHandleManager::release(handle);
	delete instrument;
}


void PluginPortsMigrationTest::monoDelayUpdatesItsDelayAcrossSampleRateChanges()
{
	lmms::MonoDelay delay{2, 48000};
	delay.setLength(24000.f);

	lmms::sample_t sample = 0.f;
	for (int frame = 0; frame < 17; ++frame)
	{
		delay.tick(&sample);
	}

	delay.setSampleRate(44100);
	QCOMPARE(delay.m_maxLength, 88200);
	QCOMPARE(delay.m_length, 22050.f);
	QCOMPARE(delay.m_writeIndex, 0);
}

void PluginPortsMigrationTest::zeroLengthDelayPassesInputThrough()
{
	lmms::MonoDelay delay{1, 4};
	delay.setLength(0.0f);

	// Run for one ring's worth of ticks, then check that zero delay returns the
	// current sample instead of data left in a previously used slot.
	lmms::sample_t sample = 0.0f;
	for (int frame = 0; frame < 4; ++frame)
	{
		sample = static_cast<lmms::sample_t>(frame + 1);
		delay.tick(&sample);
	}
	sample = 17.0f;
	delay.tick(&sample);
	QCOMPARE(sample, 17.0f);
}

/*!
 * Slice 5 (task #589) negative control: the four newly migrated instruments
 * must each render audible, pairwise-distinct output, so the sample-exact
 * comparison above is demonstrably comparing real DSP and not silence.
 */
void PluginPortsMigrationTest::slice5InstrumentsAreLiveAndDistinct()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	static const std::array<const char*, 4> slice5{
		"tripleoscillator", "monstro", "organic", "audiofileprocessor"};

	for (const char* name : slice5)
	{
		const auto* migrated = findRender(m_migrated, name);
		QVERIFY2(migrated != nullptr, name);

		float peak = 0.0f;
		for (const float s : migrated->samples)
		{
			peak = std::max(peak, std::fabs(s));
		}
		qInfo().noquote() << QString{"slice 5 negative control: %1 peak=%2 (audible)"}
			.arg(name).arg(peak, 0, 'g', 4);
		QVERIFY2(peak > 1e-4f, qPrintable(QString{"%1 rendered silence"}.arg(name)));
	}

	// Pairwise distinctness: every pair of the four new instruments must
	// differ, so the comparator is live on the new modules too.
	for (std::size_t i = 0; i < slice5.size(); ++i)
	{
		for (std::size_t j = i + 1; j < slice5.size(); ++j)
		{
			const auto* a = findRender(m_migrated, slice5[i]);
			const auto* b = findRender(m_reference, slice5[j]);
			QVERIFY(a != nullptr && b != nullptr);
			const float worst = partc::maxAbsDiff(a->samples, b->samples);
			qInfo().noquote()
				<< QString{"slice 5 negative control: %1 (migrated) vs %2 (reference) max|delta|=%3"}
					   .arg(QString::fromUtf8(slice5[i]), QString::fromUtf8(slice5[j]))
					   .arg(worst, 0, 'g', 3);
			QVERIFY2(a->checksum != b->checksum,
				qPrintable(QString{"%1 and %2 rendered identically"}
					.arg(QString::fromUtf8(slice5[i]), QString::fromUtf8(slice5[j]))));
			QVERIFY2(worst > 1e-6f,
				qPrintable(QString{"%1 vs %2: comparison not sensitive"}
					.arg(QString::fromUtf8(slice5[i]), QString::fromUtf8(slice5[j]))));
		}
	}
}

void PluginPortsMigrationTest::organicDistortionLeavesPreOffsetFramesUnchanged()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{PART_C_MIGRATED_organic};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("organic");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	QDomDocument doc;
	QDomElement root = doc.createElement("instrument");
	QDomElement state = instrument->saveState(doc, root);
	state.setAttribute(QStringLiteral("foldback"), QStringLiteral("0.5"));
	instrument->restoreState(state);

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const lmms::f_cnt_t offset = frames / 2;
	QVERIFY(offset > 0);
	constexpr float previousSample = 0.75f;
	std::vector<lmms::SampleFrame> output(static_cast<std::size_t>(frames),
		lmms::SampleFrame{previousSample, previousSample});
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * 2)},
		lmms::TimePos{0}, lmms::DefaultKey};

	lmms::NotePlayHandleManager::init();
	auto* handle = lmms::NotePlayHandleManager::acquire(track.get(), offset, frames * 2, note);
	instrument->playNote(handle, std::span<lmms::SampleFrame>{output});
	instrument->deleteNotePluginData(handle);
	handle->m_pluginData = nullptr;
	lmms::NotePlayHandleManager::release(handle);
	lmms::NotePlayHandleManager::free();

	for (lmms::f_cnt_t frame = 0; frame < offset; ++frame)
	{
		QCOMPARE(output[static_cast<std::size_t>(frame)].left(), previousSample);
		QCOMPARE(output[static_cast<std::size_t>(frame)].right(), previousSample);
	}
}

/*!
 * Slice 6 (task #589) negative controls and state round trip.
 *
 * The sample-exact comparison in migratedPluginsPreserveBehaviour() proves
 * "identical to the base commit"; this slot proves the comparison is not
 * vacuous for the slice-6 modules: Lb302 and both LADSPA-hosted effects must
 * render audible output, the LADSPA effects must actually change the signal
 * (a bypassed effect would compare equal trivially), the two distinct LADSPA
 * plugins must not render identically, and every control-port value applied
 * through loadSettings() must survive the saveState() round trip.
 */
void PluginPortsMigrationTest::slice6PluginsAreLiveAndExact()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	// Lb302: rendered through the same instrument entry point as every other
	// instrument, so a non-silent peak proves the note path ran.
	const auto* lb302 = findRender(m_migrated, "lb302");
	QVERIFY2(lb302 != nullptr, "lb302 render missing");
	float lb302Peak = 0.0f;
	for (const float s : lb302->samples)
	{
		lb302Peak = std::max(lb302Peak, std::fabs(s));
	}
	qInfo().noquote() << QString{"slice 6 negative control: lb302 peak=%1 (audible)"}
		.arg(lb302Peak, 0, 'g', 4);
	QVERIFY2(lb302Peak > 1e-4f, "lb302 rendered silence");

	// LADSPA-hosted effects: audible, wet and round-tripped.
	const auto dry = partc::dryInput(lmms::Engine::audioEngine()->framesPerPeriod());
	const auto& specs = partc::ladspaSpecs();
	QCOMPARE(m_ladspa.size(), specs.size());
	for (std::size_t i = 0; i < specs.size(); ++i)
	{
		const auto& spec = specs[i];
		const QString name = QString::fromUtf8(spec.name);
		const auto* render = findRender(m_migrated, spec.name);
		QVERIFY2(render != nullptr, spec.name);
		QCOMPARE(render->samples.size(), dry.size());

		float peak = 0.0f;
		for (const float s : render->samples)
		{
			peak = std::max(peak, std::fabs(s));
		}
		const float wet = partc::maxAbsDiff(render->samples, dry);
		qInfo().noquote() << QString{"slice 6 negative control: %1 peak=%2 max|wet-dry|=%3"}
			.arg(name).arg(peak, 0, 'g', 4).arg(wet, 0, 'g', 4);
		QVERIFY2(peak > 1e-4f, qPrintable(name + " rendered silence"));
		QVERIFY2(wet > 0.05f,
			qPrintable(name + " did not change the signal (bypassed?)"));

		// State round trip: each port value applied by loadSettings() must be
		// visible again in the serialised state.
		const auto& ladspa = m_ladspa[i];
		QCOMPARE(static_cast<int>(ladspa.savedPorts.size()),
			static_cast<int>(spec.ports.size()));
		for (int p = 0; p < static_cast<int>(spec.ports.size()); ++p)
		{
			const QStringList parts = ladspa.savedPorts[p].split(QLatin1Char('='));
			QCOMPARE(parts.size(), 2);
			const double saved = parts[1].toDouble();
			const double expected = QString::fromUtf8(spec.ports[p].value).toDouble();
			qInfo().noquote() << QString{"slice 6 state round trip: %1 %2 expected=%3"}
				.arg(name, ladspa.savedPorts[p]).arg(expected, 0, 'g', 9);
			QVERIFY2(std::fabs(saved - expected) < 1e-3,
				qPrintable(QString{"%1: %2 did not round-trip"}.arg(name, ladspa.savedPorts[p])));
		}
	}

	// Two distinct LADSPA plugins through the migrated module must differ, so
	// the comparator is live on the new modules too.
	const auto* amp = findRender(m_migrated, "ladspaeffect:amp");
	const auto* djEq = findRender(m_reference, "ladspaeffect:dj_eq");
	QVERIFY(amp != nullptr && djEq != nullptr);
	const float worst = partc::maxAbsDiff(amp->samples, djEq->samples);
	qInfo().noquote()
		<< QString{"slice 6 negative control: ladspaeffect:amp (migrated) vs ladspaeffect:dj_eq (reference) max|delta|=%1"}
			   .arg(worst, 0, 'g', 3);
	QVERIFY2(amp->checksum != djEq->checksum,
		"distinct LADSPA plugins rendered identically");
	QVERIFY2(worst > 1e-6f, "LADSPA comparison not sensitive");
}


void PluginPortsMigrationTest::partialInitializationCleansAcquiredResources()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary ladspaLibrary{QString::fromUtf8(PART_C_FAILURE_LADSPA_MODULE)};
	QVERIFY2(ladspaLibrary.load(), qPrintable(ladspaLibrary.errorString()));
	auto reset = reinterpret_cast<void (*)()>(
		ladspaLibrary.resolve("partial_init_failure_reset"));
	auto instantiations = reinterpret_cast<unsigned long (*)()>(
		ladspaLibrary.resolve("partial_init_failure_instantiations"));
	auto cleanups = reinterpret_cast<unsigned long (*)()>(
		ladspaLibrary.resolve("partial_init_failure_cleanups"));
	QVERIFY(reset != nullptr);
	QVERIFY(instantiations != nullptr);
	QVERIFY(cleanups != nullptr);
	reset();

	QLibrary effectLibrary{QString::fromUtf8(PART_C_MIGRATED_ladspaeffect)};
	effectLibrary.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(effectLibrary.load(), qPrintable(effectLibrary.errorString()));
	auto entry = reinterpret_cast<MainFn>(effectLibrary.resolve("lmms_plugin_main"));
	auto* descriptor = reinterpret_cast<const lmms::Plugin::Descriptor*>(
		effectLibrary.resolve("ladspaeffect_plugin_descriptor"));
	QVERIFY(entry != nullptr);
	QVERIFY(descriptor != nullptr);

	lmms::Plugin::Descriptor::SubPluginFeatures::Key key{
		descriptor, QStringLiteral("ladspaeffect:partial_init_failure")};
	key.attributes[QStringLiteral("file")] =
		QFileInfo{QString::fromUtf8(PART_C_FAILURE_LADSPA_MODULE)}.fileName();
	key.attributes[QStringLiteral("plugin")] = QStringLiteral("partial_init_failure");
	std::unique_ptr<lmms::Effect> effect{
		static_cast<lmms::Effect*>(entry(nullptr, &key))};
	QVERIFY(effect != nullptr);
	QCOMPARE(instantiations(), 2UL);
	QVERIFY(!effect->isOkay());
	effect.reset();
	QCOMPARE(cleanups(), 1UL);
}

void PluginPortsMigrationTest::lb302ProducerDoesNotWaitForEarlierClaim()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	lmms::NotePlayHandleManager::init();
	auto* engine = lmms::Engine::audioEngine();
	QVERIFY(engine != nullptr);

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_lb302)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	const auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("lb302");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);
	auto* synth = static_cast<lmms::Lb302Synth*>(instrument.get());

	const auto frames = engine->framesPerPeriod();
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames)}, lmms::TimePos{0}, lmms::DefaultKey};
	lmms::NotePlayHandle handle{track.get(), 0, frames, note, nullptr, -1,
		lmms::NotePlayHandle::Origin::MidiClip};

	// Model producer 0 paused immediately after claiming sequence 0.
	synth->m_notesWriteClaimed.store(1, std::memory_order_relaxed);
	std::atomic<bool> returned{false};
	std::thread producer{[&]
	{
		synth->playNoteImpl(&handle, std::span<lmms::SampleFrame>{});
		returned.store(true, std::memory_order_release);
	}};

	const auto claimDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
	while (synth->m_notesWriteClaimed.load(std::memory_order_acquire) < 2
		&& std::chrono::steady_clock::now() < claimDeadline)
	{
		std::this_thread::yield();
	}
	const bool claimed = synth->m_notesWriteClaimed.load(std::memory_order_acquire) == 2;
	const auto returnDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
	while (!returned.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < returnDeadline)
	{
		std::this_thread::yield();
	}
	const bool returnedBeforeGapRelease = returned.load(std::memory_order_acquire);
	if (!returnedBeforeGapRelease)
	{
		// Release the legacy contiguous-commit wait so the red test can join cleanly.
		const auto releaseLegacyGap = []<typename Synth>(Synth& state)
		{
			if constexpr (requires { state.m_notesWriteCommitted.store(std::size_t{}, std::memory_order_release); })
			{
				state.m_notesWriteCommitted.store(1, std::memory_order_release);
			}
		};
		releaseLegacyGap(*synth);
	}
	producer.join();

	QVERIFY2(claimed, "second producer did not claim the next note slot");
	QVERIFY2(returnedBeforeGapRelease,
		"note producer waited for an earlier claimed slot to commit");
}

void PluginPortsMigrationTest::lb302DecayTracksSampleRateChanges()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	lmms::NotePlayHandleManager::init();
	auto* engine = lmms::Engine::audioEngine();
	QVERIFY(engine != nullptr);
	QVERIFY(engine->audioDev() != nullptr);

	struct RestoreSampleRate
	{
		lmms::AudioEngine* engine;
		lmms::sample_rate_t sampleRate;
		~RestoreSampleRate()
		{
			engine->audioDev()->setSampleRateForTesting(sampleRate);
			emit engine->sampleRateChanged();
		}
	} restore{engine, engine->outputSampleRate()};

	engine->audioDev()->setSampleRateForTesting(48000);
	emit engine->sampleRateChanged();

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_lb302)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	const auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("lb302");
	auto actualTrack = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto expectedTrack = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> actual{
		static_cast<lmms::Instrument*>(entry(actualTrack.get(), nullptr))};
	std::unique_ptr<lmms::Instrument> expected{
		static_cast<lmms::Instrument*>(entry(expectedTrack.get(), nullptr))};
	QVERIFY(actual != nullptr);
	QVERIFY(expected != nullptr);
	partc::applyInstrumentTestSettings(*actual, "lb302");
	partc::applyInstrumentTestSettings(*expected, "lb302");

	const auto frames = engine->framesPerPeriod();
	const lmms::Note actualNote{lmms::TimePos{static_cast<lmms::tick_t>(frames * partc::Buffers)},
		lmms::TimePos{0}, lmms::DefaultKey};
	const lmms::Note expectedNote{lmms::TimePos{static_cast<lmms::tick_t>(frames * partc::Buffers)},
		lmms::TimePos{0}, lmms::DefaultKey};
	lmms::NotePlayHandle actualHandle{actualTrack.get(), 0, frames * partc::Buffers,
		actualNote, nullptr, -1, lmms::NotePlayHandle::Origin::MidiClip};
	lmms::NotePlayHandle expectedHandle{expectedTrack.get(), 0, frames * partc::Buffers,
		expectedNote, nullptr, -1, lmms::NotePlayHandle::Origin::MidiClip};
	actual->handleMidiEvent(lmms::MidiEvent{lmms::MidiNoteOn, 0,
		static_cast<std::int16_t>(lmms::DefaultKey), 100}, lmms::TimePos{0}, 0);
	expected->handleMidiEvent(lmms::MidiEvent{lmms::MidiNoteOn, 0,
		static_cast<std::int16_t>(lmms::DefaultKey), 100}, lmms::TimePos{0}, 0);

	std::vector<lmms::SampleFrame> actualData(frames);
	std::vector<lmms::SampleFrame> expectedData(frames);
	std::vector<float> actualSamples;
	std::vector<float> expectedSamples;
	const auto sampleCount = static_cast<std::size_t>(partc::Buffers) * frames * 2;
	actualSamples.reserve(sampleCount);
	expectedSamples.reserve(sampleCount);
	auto renderBlock = [](lmms::Instrument& instrument, lmms::NotePlayHandle& handle,
		std::vector<lmms::SampleFrame>& data, std::vector<float>& output)
	{
		std::fill(data.begin(), data.end(), lmms::SampleFrame{0.0f, 0.0f});
		const std::span<lmms::SampleFrame> buffer{data};
		instrument.playNote(&handle, buffer);
		if (instrument.isSingleStreamed()) { instrument.play(buffer); }
		for (const auto& frame : data)
		{
			output.push_back(frame.left());
			output.push_back(frame.right());
		}
	};

	for (int block = 0; block < partc::Buffers; ++block)
	{
		renderBlock(*actual, actualHandle, actualData, actualSamples);
		renderBlock(*expected, expectedHandle, expectedData, expectedSamples);
		if (block == partc::Buffers / 2 - 1)
		{
			QVERIFY(actualHandle.m_pluginData != nullptr);
			QVERIFY(expectedHandle.m_pluginData != nullptr);
			engine->audioDev()->setSampleRateForTesting(24000);
			emit engine->sampleRateChanged();
			QVERIFY(QMetaObject::invokeMethod(expected.get(), "decayChanged", Qt::DirectConnection));
		}
	}
	actual->deleteNotePluginData(&actualHandle);
	expected->deleteNotePluginData(&expectedHandle);
	actualHandle.m_pluginData = nullptr;
	expectedHandle.m_pluginData = nullptr;
	const auto difference = partc::maxAbsDiff(actualSamples, expectedSamples);
	QVERIFY2(difference == 0.0f,
		qPrintable(QString{"LB302 did not apply the new sample-rate decay coefficient (max delta %1)"}
			.arg(difference, 0, 'g', 9)));
}

/*!
 * Cluster new-p9-254 and new-p14-134: the LB302 note path must stay
 * realtime-safe.
 *
 * `Lb302Synth::playNoteImpl` is the per-note callback of the instrument and
 * runs on the audio render thread. When its bounded enqueue retries run out
 * it used to call `qDebug()`: logging formats a message, which allocates and
 * takes a lock - both forbidden on that path. The failsafe now counts the
 * drop in a lock-free counter, and a control-thread timer reports it.
 *
 * The test drives the failsafe deterministically, without any timing: the
 * note ring buffer of the instrument is filled with more enqueues than any
 * capacity it can declare, so the enqueues past the capacity find the buffer
 * full, burn their retry budget and take the failsafe. The test then proves
 * that the note path neither logged nor allocated, that the drop was
 * recorded, and that the report happens on the control thread.
 */
void PluginPortsMigrationTest::lb302NoteEnqueueFailsafeStaysRealtimeSafe()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	lmms::NotePlayHandleManager::init();
	auto* engine = lmms::Engine::audioEngine();
	QVERIFY(engine != nullptr);

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_lb302)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	const auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("lb302");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	const auto frames = engine->framesPerPeriod();
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * partc::Buffers)},
		lmms::TimePos{0}, lmms::DefaultKey};
	lmms::NotePlayHandle handle{track.get(), 0, frames * partc::Buffers,
		note, nullptr, -1, lmms::NotePlayHandle::Origin::MidiClip};
	std::vector<lmms::SampleFrame> data(frames);
	const std::span<lmms::SampleFrame> buffer{data};

	// The ring buffer holds at most a couple of hundred notes, so this many
	// enqueues fill it and then keep taking the failsafe.
	constexpr int enqueueAttempts = 512;

	int logged = 0;
	QString firstMessage;
	partc::RealtimeAllocationCounts counts;
	{
		LogCapture capture;
		partc::RealtimeAllocationWatch::begin();
		for (int attempt = 0; attempt < enqueueAttempts; ++attempt)
		{
			instrument->playNote(&handle, buffer);
		}
		counts = partc::RealtimeAllocationWatch::end();
		logged = capture.count();
		firstMessage = capture.first();
	}

	QVERIFY2(logged == 0,
		qPrintable(QString{"the LB302 note path logged %1 message(s) from the audio-thread "
			"callback, first: %2"}.arg(logged).arg(firstMessage)));
	QVERIFY2(counts.allocations == 0 && counts.deallocations == 0,
		qPrintable(QString{"the LB302 note path allocated %1 time(s) and freed %2 time(s)"}
			.arg(static_cast<qulonglong>(counts.allocations))
			.arg(static_cast<qulonglong>(counts.deallocations))));

	// The failsafe recorded the drop in a lock-free counter ...
	quint64 dropped = 0;
	QVERIFY2(QMetaObject::invokeMethod(instrument.get(), "droppedNoteCount",
		Qt::DirectConnection, Q_RETURN_ARG(quint64, dropped)),
		"the LB302 instrument does not expose the dropped-note counter");
	QVERIFY2(dropped > 0, "the LB302 enqueue failsafe did not record a dropped note");

	// ... and the control thread reports it.
	int reported = 0;
	QString report;
	{
		LogCapture capture;
		QVERIFY2(QMetaObject::invokeMethod(instrument.get(), "reportDroppedNotes",
			Qt::DirectConnection),
			"the LB302 instrument does not expose the dropped-note report");
		reported = capture.count();
		report = capture.first();
	}
	QVERIFY2(reported == 1,
		qPrintable(QString{"the dropped-note report logged %1 message(s)"}.arg(reported)));
	QVERIFY2(report.contains(QString::number(dropped)),
		qPrintable(QString{"the dropped-note report does not name the count: %1"}.arg(report)));

	quint64 afterReport = 1;
	QVERIFY(QMetaObject::invokeMethod(instrument.get(), "droppedNoteCount",
		Qt::DirectConnection, Q_RETURN_ARG(quint64, afterReport)));
	QVERIFY2(afterReport == 0, "the dropped-note report did not drain the counter");

	instrument->deleteNotePluginData(&handle);
	handle.m_pluginData = nullptr;
}

/*!
 * Slice 7 (task #589) negative controls for the last in-tree legacy-API
 * plugins: every one of them must render audio, the two signal shapers must
 * actually change the signal (a bypassed effect would compare equal to its
 * reference trivially), and Oscilloscope - a documented pass-through - must
 * hand the input back bit-for-bit. Two distinct new plugins must not render
 * identically, so the comparator is live on the new modules as well.
 */
void PluginPortsMigrationTest::slice7PluginsAreLiveAndExact()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto dry = partc::dryInput(lmms::Engine::audioEngine()->framesPerPeriod());
	for (const char* name : {"frequencyshifter", "oscilloscope", "slewdistortion"})
	{
		const auto* render = findRender(m_migrated, name);
		QVERIFY2(render != nullptr, name);
		QCOMPARE(render->samples.size(), dry.size());

		float peak = 0.0f;
		for (const float s : render->samples)
		{
			peak = std::max(peak, std::fabs(s));
		}
		const float wet = partc::maxAbsDiff(render->samples, dry);
		qInfo().noquote() << QString{"slice 7 negative control: %1 peak=%2 max|wet-dry|=%3"}
			.arg(name).arg(peak, 0, 'g', 4).arg(wet, 0, 'g', 4);
		QVERIFY2(peak > 1e-4f, qPrintable(QString{"%1 rendered silence"}.arg(name)));
	}

	// The two shapers must not be bypassed.
	for (const char* name : {"frequencyshifter", "slewdistortion"})
	{
		const auto* render = findRender(m_migrated, name);
		QVERIFY2(render != nullptr, name);
		const float wet = partc::maxAbsDiff(render->samples, dry);
		QVERIFY2(wet > 0.05f,
			qPrintable(QString{"%1 did not change the signal (bypassed?)"}.arg(name)));
	}

	// Oscilloscope only copies the signal into its GUI ring buffer, so its
	// render must equal the dry input exactly.
	const auto* scope = findRender(m_migrated, "oscilloscope");
	QVERIFY2(scope != nullptr, "oscilloscope render missing");
	QCOMPARE(scope->checksum, partc::checksum(dry));

	// Two distinct new plugins must differ from each other.
	const auto* shifter = findRender(m_migrated, "frequencyshifter");
	const auto* slew = findRender(m_reference, "slewdistortion");
	QVERIFY(shifter != nullptr && slew != nullptr);
	const float worst = partc::maxAbsDiff(shifter->samples, slew->samples);
	qInfo().noquote()
		<< QString{"slice 7 negative control: frequencyshifter (migrated) vs slewdistortion (reference) max|delta|=%1"}
			   .arg(worst, 0, 'g', 3);
	QVERIFY2(shifter->checksum != slew->checksum, "distinct plugins rendered identically");
	QVERIFY2(worst > 1e-6f, "slice 7 comparison not sensitive");
}

/*!
 * Slice 8 (task #589) negative control: every newly proven slice-8 render
 * must be audible and pairwise distinct. Without this a host that failed to
 * load its asset (an unloaded soundfont, a missing rawwave directory, an LV2
 * plugin that never instantiated) would render silence on both sides and the
 * sample-exact comparison would pass trivially.
 */
void PluginPortsMigrationTest::slice8PluginsAreLiveAndExact()
{
	// Consumes the module renders from migratedPluginsPreserveBehaviour(),
	// which a Windows test host cannot produce (see testHostCanLoadPluginModules()).
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	std::vector<QString> names;
#ifdef PART_C_MIGRATED_sf2player
	names.push_back(QStringLiteral("sf2player"));
#endif
#ifdef PART_C_MIGRATED_malletsstk
	names.push_back(QStringLiteral("malletsstk"));
#endif
#ifdef PART_C_MIGRATED_gigplayer
	names.push_back(QStringLiteral("gigplayer"));
#endif
#ifdef PART_C_MIGRATED_lv2effect
	for (const auto& spec : partc::lv2Specs())
	{
		if (QString::fromUtf8(spec.plugin) == QLatin1String("lv2effect") && partc::lv2BundleProvides(spec.uri))
		{
			names.push_back(QString::fromUtf8(spec.name));
		}
	}
#endif
#ifdef PART_C_MIGRATED_lv2instrument
	for (const auto& spec : partc::lv2Specs())
	{
		if (QString::fromUtf8(spec.plugin) == QLatin1String("lv2instrument") && partc::lv2BundleProvides(spec.uri))
		{
			names.push_back(QString::fromUtf8(spec.name));
		}
	}
#endif
	// The slice-8 plugins need optional third-party deps (libgig, STK, lilv,
	// fluidsynth). A CI runner without them builds none of these, so an empty
	// list is a skip, not a failure (it failed the whole ctest suite on CI).
	if (names.empty())
	{
		QSKIP("no slice-8 plugin is built into this configuration");
	}

	for (const auto& name : names)
	{
		const auto* render = findRender(m_migrated, name.toUtf8().constData());
		QVERIFY2(render != nullptr, qPrintable(name));

		float peak = 0.0f;
		for (const float s : render->samples)
		{
			peak = std::max(peak, std::fabs(s));
		}
		qInfo().noquote() << QString{"slice 8 negative control: %1 peak=%2"}
			.arg(name).arg(peak, 0, 'g', 4);
		QVERIFY2(peak > 1e-4f, qPrintable(QString{"%1 rendered silence"}.arg(name)));
		if (name == QLatin1String("gigplayer"))
		{
			QVERIFY2(peak > 0.35f, "16-bit GIG sample peak was not normalized to full scale");
		}
	}

	for (std::size_t i = 0; i < names.size(); ++i)
	{
		for (std::size_t j = i + 1; j < names.size(); ++j)
		{
			const auto* a = findRender(m_migrated, names[i].toUtf8().constData());
			const auto* b = findRender(m_reference, names[j].toUtf8().constData());
			QVERIFY(a != nullptr && b != nullptr);

			const float worst = partc::maxAbsDiff(a->samples, b->samples);
			qInfo().noquote()
				<< QString{"slice 8 negative control: %1 (migrated) vs %2 (reference) max|delta|=%3"}
					   .arg(names[i], names[j])
					   .arg(worst, 0, 'g', 3);
			QVERIFY2(a->checksum != b->checksum,
				qPrintable(QString{"%1 and %2 rendered identically"}.arg(names[i], names[j])));
			QVERIFY2(worst > 1e-6f, "slice 8 comparison not sensitive");
		}
	}
}

void PluginPortsMigrationTest::multitapEchoRetunesAllTapsAfterSampleRateChange()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_multitapecho)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto* engine = lmms::Engine::audioEngine();
	const auto originalRate = engine->outputSampleRate();
	SampleRateRestore restore{engine, originalRate};
	auto outdated = createConfiguredMultitapEcho(entry);
	const auto changedRate = originalRate == 48000 ? 44100 : 48000;
	engine->audioDev()->setSampleRateForTesting(changedRate);
	emit engine->sampleRateChanged();
	auto current = createConfiguredMultitapEcho(entry);

	const auto frames = engine->framesPerPeriod();
	const auto outdatedSamples = partc::renderInFreshThread(*outdated, frames);
	const auto currentSamples = partc::renderInFreshThread(*current, frames);
	QVERIFY2(outdatedSamples == currentSamples,
		"tap filters differ after a sample-rate change with 32 steps");
}

void PluginPortsMigrationTest::multitapEchoGraphEditsAreDeferredToAudioThread()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_multitapecho)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	const auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto effect = createConfiguredMultitapEcho(entry);
	constexpr int tap = 31;
	const float ampBeforeEdit = lmms::MultitapEchoEffectTestAccess::amp(*effect, tap);
	const float lpFreqBeforeEdit = lmms::MultitapEchoEffectTestAccess::lpFreq(*effect, tap);
	const float graphLpValue = std::log10(2000.0f / 20.0f);

	lmms::MultitapEchoEffectTestAccess::setAmpGraphSample(*effect, tap, -6.0f);
	lmms::MultitapEchoEffectTestAccess::setLpGraphSample(*effect, tap, graphLpValue);
	QCOMPARE(lmms::MultitapEchoEffectTestAccess::amp(*effect, tap), ampBeforeEdit);
	QCOMPARE(lmms::MultitapEchoEffectTestAccess::lpFreq(*effect, tap), lpFreqBeforeEdit);

	std::array<lmms::SampleFrame, 64> buffer{};
	lmms::SampleFrame* busPointers[] = {buffer.data()};
	lmms::AudioBus bus{busPointers, 1, static_cast<lmms::f_cnt_t>(buffer.size())};
	effect->processAudioBuffer(bus);
	QCOMPARE(lmms::MultitapEchoEffectTestAccess::amp(*effect, tap), lmms::dbfsToAmp(-6.0f));
	QVERIFY(std::abs(lmms::MultitapEchoEffectTestAccess::lpFreq(*effect, tap) - 2000.0f) < 0.01f);
}

void PluginPortsMigrationTest::multitapEchoStagesCascade()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary* lib = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_multitapecho)};
	lib->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(lib->load(), qPrintable(lib->errorString()));
	m_libraries.push_back(lib);
	auto entry = reinterpret_cast<MainFn>(lib->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	for (const bool swapInputs : {false, true})
	{
		std::vector<std::vector<float>> renders;
		for (const int stages : {1, 4})
		{
			auto* fx = static_cast<lmms::Effect*>(entry(nullptr, nullptr));
			QVERIFY(fx != nullptr);
			QDomDocument doc;
			QDomElement root = doc.createElement("effect");
			root.setAttribute("on", 1);
			root.setAttribute("wet", QString::number(partc::WetLevel, 'g', 9));
			root.setAttribute("autoquit", 0);
			QDomElement controls = fx->controls()->saveState(doc, root);
			for (const auto& setting : partc::overridesFor("multitapecho"))
			{
				controls.setAttribute(QString::fromUtf8(setting.name), QString::fromUtf8(setting.value));
			}
			controls.setAttribute(QStringLiteral("stages"), stages);
			controls.setAttribute(QStringLiteral("swapinputs"), swapInputs ? 1 : 0);
			fx->loadSettings(root);
			renders.push_back(partc::renderInFreshThread(*fx,
				lmms::Engine::audioEngine()->framesPerPeriod()));
			delete fx;
		}

		const float worst = partc::maxAbsDiff(renders[0], renders[1]);
		QVERIFY2(worst > 1e-6f,
			qPrintable(QString{"four filter stages match one stage (swap inputs=%1, max|delta|=%2)"}
				.arg(swapInputs ? "true" : "false").arg(worst, 0, 'g', 3)));
	}
}

void PluginPortsMigrationTest::nesCoarseDetuneRetunesHeldNote()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_nes)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	const auto setNesSettings = [](lmms::Instrument& inst, const QString& coarseDetune)
	{
		QDomDocument doc;
		QDomElement root = doc.createElement("instrument");
		QDomElement state = inst.saveState(doc, root);
		state.setAttribute("crs1", coarseDetune);
		state.setAttribute("on2", "0");
		state.setAttribute("on3", "0");
		state.setAttribute("envon1", "0");
		state.setAttribute("sweep1", "0");
		state.setAttribute("vibr", "0");
		inst.restoreState(state);
	};
	setNesSettings(*instrument, QStringLiteral("0"));

	const lmms::f_cnt_t frames = lmms::Engine::audioEngine()->framesPerPeriod();
	constexpr int BeforeDetuneChange = 8;
	constexpr int WarmupAfterDetune = 8;
	constexpr int MeasureAfterDetune = 8;
	constexpr int TotalPeriods = BeforeDetuneChange + WarmupAfterDetune + MeasureAfterDetune;
	const lmms::Note note{
		lmms::TimePos{static_cast<lmms::tick_t>(frames * TotalPeriods)},
		lmms::TimePos{0}, lmms::DefaultKey};
	lmms::NotePlayHandle noteHandle{
		track.get(), 0, frames * TotalPeriods, note, nullptr, -1,
		lmms::NotePlayHandle::Origin::MidiClip};
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	std::vector<float> beforeDetune;
	std::vector<float> afterDetune;
	beforeDetune.reserve(static_cast<std::size_t>(BeforeDetuneChange * frames));
	afterDetune.reserve(static_cast<std::size_t>(MeasureAfterDetune * frames));

	for (int period = 0; period < TotalPeriods; ++period)
	{
		if (period == BeforeDetuneChange)
		{
			setNesSettings(*instrument, QStringLiteral("12"));
		}
		std::fill(buffer.begin(), buffer.end(), lmms::SampleFrame{0.0f, 0.0f});
		instrument->playNote(&noteHandle, std::span<lmms::SampleFrame>{buffer});
		if (period < BeforeDetuneChange || period >= BeforeDetuneChange + WarmupAfterDetune)
		{
			for (lmms::f_cnt_t frame = 0; frame < frames; ++frame)
			{
				auto& samples = period < BeforeDetuneChange ? beforeDetune : afterDetune;
				samples.push_back(buffer[static_cast<std::size_t>(frame)].left());
			}
		}
	}
	if (noteHandle.m_pluginData != nullptr)
	{
		instrument->deleteNotePluginData(&noteHandle);
		noteHandle.m_pluginData = nullptr;
	}
	delete instrument;

	const auto estimatePeriod = [](const std::vector<float>& samples, int minPeriod, int maxPeriod)
	{
		float mean = 0.0f;
		for (const float sample : samples) { mean += sample; }
		mean /= static_cast<float>(samples.size());
		double bestCorrelation = -std::numeric_limits<double>::infinity();
		int bestPeriod = minPeriod;
		for (int lag = minPeriod; lag <= maxPeriod; ++lag)
		{
			double correlation = 0.0;
			for (std::size_t index = 0; index + static_cast<std::size_t>(lag) < samples.size(); ++index)
			{
				correlation += (samples[index] - mean) * (samples[index + static_cast<std::size_t>(lag)] - mean);
			}
			if (correlation > bestCorrelation)
			{
				bestCorrelation = correlation;
				bestPeriod = lag;
			}
		}
		return bestPeriod;
	};
	const int initialPeriod = static_cast<int>(lmms::Engine::audioEngine()->outputSampleRate()
		/ noteHandle.frequency());
	const int beforePeriod = estimatePeriod(beforeDetune, initialPeriod - 2, initialPeriod + 2);
	const int afterPeriod = estimatePeriod(afterDetune, initialPeriod / 2 - 2, initialPeriod + 2);
	qInfo("NES held-note oscillator periods: before=%d after=%d", beforePeriod, afterPeriod);
	QVERIFY2(std::abs(afterPeriod * 2 - beforePeriod) <= 3,
		"changing NES channel 1 coarse detune did not halve the held note's oscillator period");
}

void PluginPortsMigrationTest::opulenzUpdatePatchLocksEmulatorForWholeUpdate()
{
	const auto sourcePath = QDir{QCoreApplication::applicationDirPath()}
		.filePath(QStringLiteral("../../plugins/OpulenZ/OpulenZ.cpp"));
	QFile source{sourcePath};
	QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(sourcePath));
	const auto contents = source.readAll();
	const auto updateBody = functionBody(contents, "void OpulenzInstrument::updatePatch()");
	QVERIFY2(!updateBody.isEmpty(), "OpulenzInstrument::updatePatch() source was not found");

	const auto lockPosition = updateBody.indexOf("QMutexLocker locker(&emulatorMutex);");
	QVERIFY2(lockPosition >= 0, "updatePatch must hold emulatorMutex with one scoped lock");
	QVERIFY(updateBody.indexOf("theEmulator->write(0xBD,") > lockPosition);
	QVERIFY(updateBody.indexOf("setVoiceVelocity(") > lockPosition);
	QVERIFY(updateBody.indexOf("loadPatchLocked(inst.data())") > lockPosition);
	QVERIFY(!updateBody.contains("loadPatch(inst.data())"));

	const auto loaderBody = functionBody(contents,
		"void OpulenzInstrument::loadPatchLocked(const unsigned char inst[14])");
	QVERIFY2(!loaderBody.isEmpty(), "locked patch writer helper was not found");
	QVERIFY(!loaderBody.contains("emulatorMutex.lock()"));
	QVERIFY(!loaderBody.contains("QMutexLocker"));
}

void PluginPortsMigrationTest::opulenzReloadEmulatorReplacesEmulatorUnderMutex()
{
	const auto sourcePath = QDir{QCoreApplication::applicationDirPath()}
		.filePath(QStringLiteral("../../plugins/OpulenZ/OpulenZ.cpp"));
	QFile source{sourcePath};
	QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(sourcePath));
	const auto contents = source.readAll();
	const auto reloadBody = functionBody(contents, "void OpulenzInstrument::reloadEmulator()");
	QVERIFY2(!reloadBody.isEmpty(), "OpulenzInstrument::reloadEmulator() source was not found");

	// The audio callbacks read theEmulator, voiceNote and voiceLRU only under emulatorMutex, so the
	// old emulator must be deleted and the voice state reset inside the same critical section.
	const auto lock = reloadBody.indexOf("emulatorMutex.lock();");
	const auto deletion = reloadBody.indexOf("delete theEmulator;");
	const auto replacement = reloadBody.indexOf("theEmulator = new CTemuopl");
	const auto noteReset = reloadBody.indexOf("voiceNote[i] = OPL2_VOICE_FREE;");
	const auto lruReset = reloadBody.indexOf("voiceLRU[i] = i;");
	const auto patchUpdate = reloadBody.indexOf("updatePatch();");
	const auto unlock = reloadBody.indexOf("emulatorMutex.unlock();");
	QVERIFY2(lock >= 0 && lock < deletion && deletion < replacement && replacement < noteReset
		&& noteReset < lruReset && lruReset < patchUpdate && patchUpdate < unlock,
		"reload must delete, replace, reset voices and patch the emulator within one emulatorMutex section");

	// updatePatch() takes the same mutex again, so the mutex must be re-entrant for that nested call.
	QVERIFY2(contents.contains("QRecursiveMutex OpulenzInstrument::emulatorMutex;"),
		"emulatorMutex must be recursive because reloadEmulator calls updatePatch() while holding it");
}

void PluginPortsMigrationTest::opulenzRendersTheProvidedSpanSize()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* library = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_opulenz)};
	library->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library->load(), qPrintable(library->errorString()));
	m_libraries.push_back(library);
	auto entry = reinterpret_cast<MainFn>(library->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("opulenz");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	const auto periodFrames = static_cast<std::size_t>(lmms::Engine::audioEngine()->framesPerPeriod());
	QVERIFY(periodFrames > 1);
	constexpr float Sentinel = 123.0f;
	const auto shortSpanFrames = periodFrames / 2;
	std::vector<lmms::SampleFrame> shortBuffer(periodFrames, lmms::SampleFrame{Sentinel, Sentinel});
	instrument->play(std::span<lmms::SampleFrame>{shortBuffer.data(), shortSpanFrames});
	const bool shortSpanBoundaryPreserved = std::all_of(shortBuffer.begin() + shortSpanFrames,
		shortBuffer.end(), [](const lmms::SampleFrame& sample) {
			return sample.left() == Sentinel && sample.right() == Sentinel;
		});

	const auto longSpanFrames = periodFrames * 2;
	std::vector<lmms::SampleFrame> longBuffer(longSpanFrames, lmms::SampleFrame{Sentinel, Sentinel});
	instrument->play(std::span<lmms::SampleFrame>{longBuffer});
	const bool longSpanFullyWritten = std::all_of(longBuffer.begin(), longBuffer.end(),
		[](const lmms::SampleFrame& sample) {
			return sample.left() != Sentinel && sample.right() != Sentinel;
		});
	QVERIFY2(shortSpanBoundaryPreserved && longSpanFullyWritten,
		"OpulenZ wrote outside a short span or left the tail of a long span unwritten");
}

/*!
 * Regression for cluster new-p23-186: playImpl must leave its loop when a
 * non-empty output span produces a zero-sized chunk. Calling the broken path
 * would hang the test, so verify the guard sits between chunk calculation and
 * emulator update instead.
 */
void PluginPortsMigrationTest::opulenzZeroFrameCountStopsLoop()
{
	const auto sourcePath = QDir{QCoreApplication::applicationDirPath()}
		.filePath(QStringLiteral("../../plugins/OpulenZ/OpulenZ.cpp"));
	QFile source{sourcePath};
	QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(sourcePath));
	const auto playBody = functionBody(source.readAll(), "void OpulenzInstrument::playImpl(");
	QVERIFY2(!playBody.isEmpty(), "OpulenzInstrument::playImpl() source was not found");

	const auto chunkPosition = playBody.indexOf("const auto chunkSize =");
	const auto zeroGuardPosition = playBody.indexOf("if (chunkSize == 0)", chunkPosition);
	const auto breakPosition = playBody.indexOf("break;", zeroGuardPosition);
	const auto emulatorUpdatePosition = playBody.indexOf("theEmulator->update(", chunkPosition);
	QVERIFY2(chunkPosition >= 0 && zeroGuardPosition > chunkPosition
		&& breakPosition > zeroGuardPosition && emulatorUpdatePosition > breakPosition,
		"playImpl must break on a zero chunk before updating the emulator, so offset cannot stall");
}

/*!
 * Regression for cluster new-p9-326: OpulenZ never re-applied the
 * velocity-scaled operator levels to a voice that was already sounding, so a
 * held note kept the level it was given at note-on and the operator level
 * knobs had no effect on it until some later MIDI event touched the voice.
 *
 * The plugin's models are addressed through the attribute names its own
 * saveSettings()/loadSettings() pair uses - the same round trip the track
 * uses to persist an instrument - so the test drives the product's public
 * surface and needs no copy of the plugin's private class layout.
 */
void PluginPortsMigrationTest::opulenzReappliesVelocityScalingToHeldVoice()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	QLibrary library{QString::fromUtf8(PART_C_MIGRATED_opulenz)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	// The module entry point hands out an instrument the track does NOT own:
	// Instrument's QObject parent is null, so InstrumentTrack's destructor
	// (which deletes only its own m_instrument) would leave this instance and
	// its sample-rate connection alive for the rest of the test process. The
	// test owns it instead. The unique_ptr is declared after the track and
	// while the module is still loaded, so the instrument is destroyed first -
	// before the track it points at and before the library it came from - on
	// every exit path, including the early returns of the assertions.
	auto instrument = std::unique_ptr<lmms::Instrument>{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	// The OPL2 operator level register is inverted: level 63 writes register
	// value 0 (loudest), level 0 writes register value 63 (silent).
	const auto setOperatorLevels = [](lmms::Instrument& inst, const QString& op1Level,
		const QString& op2Level)
	{
		QDomDocument doc;
		QDomElement root = doc.createElement("instrument");
		QDomElement state = inst.saveState(doc, root);
		state.setAttribute("op1_lvl", op1Level);
		state.setAttribute("op2_lvl", op2Level);
		inst.restoreState(state);
	};
	setOperatorLevels(*instrument, QStringLiteral("63"), QStringLiteral("63"));

	// Open a voice and never release it: the plugin re-scales only the
	// voices that are still sounding.
	instrument->handleMidiEvent(lmms::MidiEvent{lmms::MidiNoteOn, 0, 69, 127}, lmms::TimePos{0}, 0);

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const auto renderEnergy = [&](int periods)
	{
		double energy = 0.0;
		for (int period = 0; period < periods; ++period)
		{
			std::fill(buffer.begin(), buffer.end(), lmms::SampleFrame{0.0f, 0.0f});
			instrument->play(std::span<lmms::SampleFrame>{buffer});
			for (const auto& sample : buffer)
			{
				energy += sample.left() * sample.left() + sample.right() * sample.right();
			}
		}
		return energy;
	};

	// Warm up first: the envelope has to reach its sustain level before the
	// energy of the held voice is measured.
	constexpr int WarmupPeriods = 8;
	constexpr int MeasuredPeriods = 8;
	renderEnergy(WarmupPeriods);
	const double before = renderEnergy(MeasuredPeriods);

	setOperatorLevels(*instrument, QStringLiteral("0"), QStringLiteral("0"));
	const double after = renderEnergy(MeasuredPeriods);

	qInfo("OpulenZ held voice energy: before=%g after=%g ratio=%g",
		before, after, before > 0.0 ? after / before : 0.0);
	QVERIFY2(before > 1.0e-6, "held OpulenZ note rendered silence before the level change");
	QVERIFY2(after < before * 0.01, "held OpulenZ voice did not apply the changed velocity scaling");
}

void PluginPortsMigrationTest::realtimeWatchCoversSingleStreamedPlay()
{
	// renderInstrumentBuffers() must charge the allocation that play() makes on a
	// single-streamed instrument to the period it happens in, not drop it.
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	AllocatingSingleStreamInstrument inst{track.get()};

	const auto frames = static_cast<lmms::f_cnt_t>(lmms::Engine::audioEngine()->framesPerPeriod());
	partc::RealtimeAllocationCounts counts;
	partc::renderInstrumentBuffers(inst, frames, lmms::DefaultKey, &counts);

	QVERIFY2(counts.allocations == static_cast<std::size_t>(partc::Buffers),
		qPrintable(QString{"play() allocations counted: %1, expected one per period (%2)"}
			.arg(static_cast<qulonglong>(counts.allocations))
			.arg(partc::Buffers)));
}

void PluginPortsMigrationTest::gigPatchDialogPreviewsSelectedProgram()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this build");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto* library = new QLibrary{QString::fromUtf8(PART_C_MIGRATED_gigplayer)};
	library->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library->load(), qPrintable(library->errorString()));
	m_libraries.push_back(library);
	auto entry = reinterpret_cast<MainFn>(library->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	partc::prepareInstrumentEnvironment("gigplayer");
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);
	partc::applyInstrumentTestSettings(*instrument, "gigplayer");
	auto* bankModel = instrument->childModel("bank");
	auto* patchModel = instrument->childModel("patch");
	QVERIFY(bankModel != nullptr);
	QVERIFY(patchModel != nullptr);
	const int originalBank = bankModel->value<int>();
	const int originalPatch = patchModel->value<int>();
	const int previewPatch = originalPatch == 127 ? 126 : originalPatch + 1;

	QWidget viewWindow;
	QWidget viewContainer{&viewWindow};
	std::unique_ptr<lmms::gui::PluginView> view{instrument->createView(&viewContainer)};
	QVERIFY(view != nullptr);
	QAbstractButton* patchButton = nullptr;
	for (auto* button : view->findChildren<QAbstractButton*>())
	{
		if (button->toolTip().contains("Choose patch", Qt::CaseInsensitive))
		{
			patchButton = button;
			break;
		}
	}
	QVERIFY(patchButton != nullptr);
	QVERIFY(patchButton->isEnabled());

	bool dialogHandled = false;
	bool previewApplied = false;
	QTimer::singleShot(0, view.get(), [&] {
		auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
		if (dialog == nullptr)
		{
			return;
		}
		auto* programList = dialog->findChild<QTreeWidget*>("m_progListView");
		if (programList != nullptr)
		{
			auto* item = new QTreeWidgetItem{programList};
			item->setText(0, QString::number(previewPatch));
			item->setText(1, QStringLiteral("Preview probe"));
			programList->setCurrentItem(item);
			previewApplied = patchModel->value<int>() == previewPatch;
		}
		dialogHandled = true;
		dialog->reject();
	});
	viewWindow.show();
	viewContainer.show();
	view->show();
	QTest::mouseClick(patchButton, Qt::LeftButton);
	QVERIFY(dialogHandled);
	QVERIFY2(previewApplied, "selecting a GIG program did not preview it through the instrument models");
	QCOMPARE(bankModel->value<int>(), originalBank);
	QCOMPARE(patchModel->value<int>(), originalPatch);

	view.reset();
	delete instrument;
#endif
}

void PluginPortsMigrationTest::rmsHelperCopiesHaveIndependentBuffers()
{
	lmms::RmsHelper source{2};
	source.update(1.0f);

	lmms::RmsHelper constructed{source};
	lmms::RmsHelper assigned{1};
	assigned = source;

	source.update(0.0f);
	source.update(0.0f);

	constructed.update(0.0f);
	const float constructedRms = constructed.update(0.0f);
	assigned.update(0.0f);
	const float assignedRms = assigned.update(0.0f);

	QVERIFY(qFuzzyIsNull(constructedRms));
	QVERIFY(qFuzzyIsNull(assignedRms));
}

void PluginPortsMigrationTest::sf2FailureTestChecksPluginModuleHostBeforeLoading()
{
	QFile source{QDir{QStringLiteral(QT_TESTCASE_SOURCEDIR)}
		.filePath(QStringLiteral("src/plugins/PluginPortsMigrationTest.cpp"))};
	QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.fileName()));
	const auto sf2FailureTest = functionBody(source.readAll(),
		"void PluginPortsMigrationTest::sf2FailedLoadPreservesCurrentFontAndSource()");
	QVERIFY2(!sf2FailureTest.isEmpty(), "SF2 failed-load test source was not found");

	const auto hostGuard = sf2FailureTest.indexOf("if (!testHostCanLoadPluginModules())");
	const auto skip = sf2FailureTest.indexOf("QSKIP(PluginModuleHostSkipMessage);");
	const auto moduleLoad = sf2FailureTest.indexOf("partc::loadPluginModule(");
	QVERIFY2(hostGuard >= 0 && hostGuard < skip && skip < moduleLoad,
		"the SF2 failed-load test must skip unsupported hosts before loading its module");
}

void PluginPortsMigrationTest::sf2FailedLoadPreservesCurrentFontAndSource()
{
#ifndef PART_C_MIGRATED_sf2player
	QSKIP("Sf2Player is not built in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto [entry, descriptor] = partc::loadPluginModule(PART_C_MIGRATED_sf2player,
		QStringLiteral("sf2player"));
	QVERIFY(entry != nullptr && descriptor != nullptr);

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = static_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);

	const QString validPath = QString::fromUtf8(PART_C_SF2_FILE);
	QVERIFY(QMetaObject::invokeMethod(instrument, "openFile", Qt::DirectConnection,
		Q_ARG(QString, validPath), Q_ARG(bool, false)));
	QDomDocument validDocument;
	QDomElement validRoot = validDocument.createElement("instrument");
	const QString validSource = instrument->saveState(validDocument, validRoot).attribute("src");
	QVERIFY(!validSource.isEmpty());
	QWidget viewRoot;
	QWidget viewParent(&viewRoot);
	auto view = std::unique_ptr<lmms::gui::PluginView>(instrument->createView(&viewParent));
	QVERIFY(view != nullptr);
	lmms::gui::PixmapButton* patchButton = nullptr;
	for (auto* button : view->findChildren<lmms::gui::PixmapButton*>())
	{
		if (button->toolTip() == QStringLiteral("Choose patch"))
		{
			patchButton = button;
			break;
		}
	}
	QVERIFY(patchButton != nullptr);
	QVERIFY(patchButton->isEnabled());

	const QString invalidPath = QStringLiteral("/missing/soundfonts/corrupt.sf2");
	QVERIFY(QMetaObject::invokeMethod(instrument, "openFile", Qt::DirectConnection,
		Q_ARG(QString, invalidPath), Q_ARG(bool, false)));
	QVERIFY(patchButton->isEnabled());

	QDomDocument failedDocument;
	QDomElement failedRoot = failedDocument.createElement("instrument");
	const QString failedSource = instrument->saveState(failedDocument, failedRoot).attribute("src");
	lmms::NotePlayHandleManager::init();
	const auto samples = partc::renderInstrumentBuffers(*instrument,
		lmms::Engine::audioEngine()->framesPerPeriod(), lmms::DefaultKey);
	const std::vector<float> silence(samples.size(), 0.0f);
	const float peak = partc::maxAbsDiff(samples, silence);
	view.reset();
	delete instrument;

	const bool sourcePreserved = failedSource == validSource;
	QVERIFY2(sourcePreserved && peak > 1e-4f,
		qPrintable(QString("source preserved: %1; retained-font peak: %2")
			.arg(sourcePreserved ? "yes" : "no").arg(peak, 0, 'g', 4)));
#endif
}

void PluginPortsMigrationTest::frequencyShifterSampleRateDropKeepsDelayIndicesInRange()
{
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	auto library = std::make_unique<QLibrary>(QString::fromUtf8(PART_C_MIGRATED_frequencyshifter));
	library->setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library->load(), qPrintable(library->errorString()));
	m_libraries.push_back(library.release());
	auto entry = reinterpret_cast<MainFn>(m_libraries.back()->resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);
	auto shifter = std::unique_ptr<lmms::Effect>{static_cast<lmms::Effect*>(entry(nullptr, nullptr))};
	QVERIFY(shifter != nullptr);

	auto* engine = lmms::Engine::audioEngine();
	auto* device = engine->audioDev();
	QVERIFY(device != nullptr);
	const auto originalRate = engine->outputSampleRate();
	struct RestoreRate
	{
		lmms::AudioEngine* engine;
		lmms::AudioDevice* device;
		lmms::sample_rate_t sampleRate;
		~RestoreRate()
		{
			device->setSampleRateForTesting(sampleRate);
			engine->sampleRateChanged();
		}
	} restore{engine, device, originalRate};

	constexpr lmms::sample_rate_t HighRate = 192000;
	constexpr lmms::sample_rate_t LowRate = 44100;
	lmms::FrequencyShifterEffectTestAccess::setMaximumDelay(*shifter);
	device->setSampleRateForTesting(HighRate);
	engine->sampleRateChanged();
	const int highBufferSize = lmms::FrequencyShifterEffectTestAccess::ringBufferSize(*shifter);
	const int lowBufferSize = lmms::FrequencyShifterEffectTestAccess::ringBufferSizeForRate(*shifter, LowRate);
	QVERIFY(highBufferSize > lowBufferSize + 1);

	const auto frames = static_cast<lmms::f_cnt_t>(lowBufferSize + 1);
	std::vector<lmms::SampleFrame> input(static_cast<std::size_t>(frames), lmms::SampleFrame{0.25f, -0.25f});
	lmms::SampleFrame* inputChannels[1] = {input.data()};
	lmms::AudioBus inputBus{inputChannels, 1, frames};
	shifter->processAudioBuffer(inputBus);
	QCOMPARE(lmms::FrequencyShifterEffectTestAccess::writeIndex(*shifter), static_cast<int>(frames));

	device->setSampleRateForTesting(LowRate);
	engine->sampleRateChanged();
	const int newBufferSize = lmms::FrequencyShifterEffectTestAccess::ringBufferSize(*shifter);
	QCOMPARE(newBufferSize, lowBufferSize);
	const int writeIndex = lmms::FrequencyShifterEffectTestAccess::writeIndex(*shifter);
	QVERIFY2(writeIndex >= 0 && writeIndex < newBufferSize,
		"write index must be inside the resized delay ring");
	const float trueDelay = lmms::FrequencyShifterEffectTestAccess::trueDelay(*shifter);
	QVERIFY2(trueDelay >= 1.f && trueDelay < newBufferSize,
		"delay length in samples must be inside the resized delay ring");

	lmms::SampleFrame next{};
	lmms::SampleFrame* nextChannels[1] = {&next};
	lmms::AudioBus nextBus{nextChannels, 1, 1};
	shifter->processAudioBuffer(nextBus);
}

void PluginPortsMigrationTest::waterfallHistoryIsCopiedUnderProcessorLocks()
{
	const QDir sourceDir = QFileInfo(QStringLiteral(__FILE__)).absoluteDir();
	QFile viewFile(sourceDir.filePath(QStringLiteral("../../../plugins/SpectrumAnalyzer/SaWaterfallView.cpp")));
	QVERIFY2(viewFile.open(QIODevice::ReadOnly), qPrintable(viewFile.fileName()));
	const QByteArray viewSource = viewFile.readAll();
	const auto snapshotUse = viewSource.indexOf("m_processor->historySnapshot()");
	const auto scaledUse = viewSource.indexOf("history.scaled(", snapshotUse);
	QVERIFY2(snapshotUse >= 0 && scaledUse > snapshotUse,
		"waterfall painting must scale the owned history snapshot");
	QVERIFY(!viewSource.contains("QImage(m_processor->getHistory()"));

	QFile processorFile(sourceDir.filePath(QStringLiteral("../../../plugins/SpectrumAnalyzer/SaProcessor.cpp")));
	QVERIFY2(processorFile.open(QIODevice::ReadOnly), qPrintable(processorFile.fileName()));
	const QByteArray processorSource = processorFile.readAll();
	const auto snapshotStart = processorSource.indexOf("QImage SaProcessor::historySnapshot()");
	QVERIFY(snapshotStart >= 0);
	const auto snapshotEnd = processorSource.indexOf("\n}\n", snapshotStart);
	QVERIFY(snapshotEnd > snapshotStart);
	const QByteArray snapshot = processorSource.mid(snapshotStart, snapshotEnd - snapshotStart);
	const auto reallocationLock = snapshot.indexOf("QMutexLocker reloc_lock(&m_reallocationAccess)");
	const auto dataLock = snapshot.indexOf("QMutexLocker data_lock(&m_dataAccess)");
	const auto deepCopy = snapshot.indexOf(".copy()");
	QVERIFY2(reallocationLock >= 0 && dataLock > reallocationLock && deepCopy > dataLock,
		"history must be copied while holding reallocation and data locks");
	QVERIFY(processorSource.contains("m_flipRequest.exchange(false, std::memory_order_relaxed)"));
}

void PluginPortsMigrationTest::gigKeyboardDimensionUsesPressedKey()
{
#ifndef PART_C_MIGRATED_gigplayer
	QSKIP("GigPlayer is unavailable in this configuration");
#else
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	const auto module = std::find_if(migratedInstruments().begin(), migratedInstruments().end(),
		[](const MigratedInstrument& instrument) { return std::string{instrument.plugin} == "gigplayer"; });
	QVERIFY(module != migratedInstruments().end());

	const QString gigFile = QFileInfo{QString::fromUtf8(PART_C_GIG_FILE)}.dir()
		.filePath(QStringLiteral("keyboard-dimension.gig"));
	QVERIFY2(QFileInfo::exists(gigFile), qPrintable(gigFile));

	QLibrary library{QString::fromUtf8(module->path)};
	library.setLoadHints(QLibrary::PreventUnloadHint);
	QVERIFY2(library.load(), qPrintable(library.errorString()));
	auto entry = reinterpret_cast<MainFn>(library.resolve("lmms_plugin_main"));
	QVERIFY(entry != nullptr);

	float lowKeyPeak = 0.0f;
	float highKeyPeak = 0.0f;
	for (const int key : {48, 80})
	{
		auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
		std::unique_ptr<lmms::Instrument> inst{
			static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
		QVERIFY(inst != nullptr);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("instrument"));
		QDomElement settings = inst->saveState(doc, root);
		settings.setAttribute(QStringLiteral("src"), gigFile);
		settings.setAttribute(QStringLiteral("patch"), 0);
		settings.setAttribute(QStringLiteral("bank"), 0);
		inst->restoreState(settings);

		const auto samples = partc::renderInstrumentInFreshThread(*inst,
			lmms::Engine::audioEngine()->framesPerPeriod(), key);
		float peak = 0.0f;
		for (const float sample : samples)
		{
			peak = std::max(peak, std::fabs(sample));
		}
		if (key == 48)
		{
			lowKeyPeak = peak;
		}
		else
		{
			highKeyPeak = peak;
		}
	}

	QVERIFY2(lowKeyPeak > 1e-4f, "keyboard-dimension fixture zone zero did not render");
	QVERIFY2(highKeyPeak < 1e-6f,
		"high note used zone zero instead of its keyboard dimension zone");
#endif
}


void PluginPortsMigrationTest::gigPlayerKeepsReleaseTriggerAcrossRegions()
{
#if defined(PART_C_MIGRATED_gigplayer) && defined(PART_C_GIG_FILE)
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	lmms::NotePlayHandleManager::init();
	const auto [entry, descriptor] = partc::loadPluginModule(
		PART_C_MIGRATED_gigplayer, QStringLiteral("gigplayer"));
	QVERIFY2(entry != nullptr && descriptor != nullptr, "GigPlayer module did not load");

	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	auto* instrument = dynamic_cast<lmms::Instrument*>(entry(track.get(), nullptr));
	QVERIFY(instrument != nullptr);
	std::unique_ptr<lmms::Instrument> instrumentOwner{instrument};
	partc::applyInstrumentTestSettings(*instrument, "gigplayer");

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * 2)},
		lmms::TimePos{0}, lmms::DefaultKey};
	lmms::NotePlayHandle handle{track.get(), 0, frames * 2, note, nullptr, -1,
		lmms::NotePlayHandle::Origin::MidiClip};
	std::vector<lmms::SampleFrame> output(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> buffer{output};

	instrument->playNote(&handle, buffer);
	instrument->play(buffer);
	instrument->deleteNotePluginData(&handle);
	handle.m_pluginData = nullptr;
	instrument->play(buffer);

	double releaseMean = 0.0;
	for (const auto& frame : output)
	{
		releaseMean += (frame.left() + frame.right()) * 0.5;
	}
	releaseMean /= static_cast<double>(frames);
	QVERIFY2(releaseMean > 0.05,
		"GigPlayer did not render the release-trigger probe after note-off");
#else
	QSKIP("GigPlayer or its .gig fixture is unavailable in this build");
#endif
}

void PluginPortsMigrationTest::gigOneFrameReleaseSampleIsAudible()
{
#if defined(PART_C_MIGRATED_gigplayer) && defined(PART_C_GIG_FILE)
	if (!testHostCanLoadPluginModules())
	{
		QSKIP(PluginModuleHostSkipMessage);
	}

	lmms::NotePlayHandleManager::init();
	const auto [entry, descriptor] = partc::loadPluginModule(
		PART_C_MIGRATED_gigplayer, QStringLiteral("gigplayer"));
	QVERIFY2(entry != nullptr && descriptor != nullptr, "GigPlayer module did not load");

	const QString fixture = QFileInfo{QString::fromUtf8(PART_C_GIG_FILE)}.dir()
		.filePath(QStringLiteral("one-frame-release.gig"));
	QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));
	auto track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
	std::unique_ptr<lmms::Instrument> instrument{
		static_cast<lmms::Instrument*>(entry(track.get(), nullptr))};
	QVERIFY(instrument != nullptr);

	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("instrument"));
	QDomElement settings = instrument->saveState(doc, root);
	settings.setAttribute(QStringLiteral("src"), fixture);
	settings.setAttribute(QStringLiteral("patch"), 0);
	settings.setAttribute(QStringLiteral("bank"), 0);
	instrument->restoreState(settings);

	// The sustained sample is silent, so only the one-frame release sample can
	// make this post-note-off render audible.
	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(frames * 2)},
		lmms::TimePos{0}, lmms::DefaultKey};
	lmms::NotePlayHandle handle{track.get(), 0, frames * 2, note, nullptr, -1,
		lmms::NotePlayHandle::Origin::MidiClip};
	std::vector<lmms::SampleFrame> output(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> buffer{output};

	instrument->playNote(&handle, buffer);
	instrument->play(buffer);
	instrument->deleteNotePluginData(&handle);
	handle.m_pluginData = nullptr;
	instrument->play(buffer);

	float peak = 0.0f;
	for (const auto& frame : output)
	{
		peak = std::max({peak, std::fabs(frame.left()), std::fabs(frame.right())});
	}
	QVERIFY2(peak > 0.1f,
		qPrintable(QString{"one-frame release sample rendered at peak %1"}.arg(peak, 0, 'g', 4)));
#else
	QSKIP("GigPlayer or its .gig fixture is unavailable in this build");
#endif
}

QTEST_MAIN(PluginPortsMigrationTest)
#include "PluginPortsMigrationTest.moc"
