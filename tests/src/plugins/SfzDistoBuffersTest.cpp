#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif
#include "sfizz/effects/Disto.h"
#include "sfizz/Synth.h"
#include "sfizz/AudioSpan.h"
namespace
{
thread_local bool g_count = false;
thread_local std::uint64_t g_alloc = 0;
thread_local std::uint64_t g_free = 0;
thread_local int g_failAt = -1;
thread_local int g_seen = 0;
inline void noteA() noexcept { if (g_count) { ++g_alloc; } }
inline void noteF(void const *p) noexcept { if (g_count && p != nullptr) { ++g_free; } }
inline void armFail(int n) noexcept { g_seen = 0; g_failAt = n; }
inline void disarmFail() noexcept { g_failAt = -1; }
}
void *operator new(std::size_t s)
{
	noteA();
	if (void *p = std::malloc(s != 0 ? s : 1)) { return p; }
	throw std::bad_alloc();
}
void *operator new[](std::size_t s)
{
	noteA();
	if (g_failAt >= 0) { ++g_seen; if (g_seen == g_failAt) { throw std::bad_alloc(); } }
	if (void *p = std::malloc(s != 0 ? s : 1)) { return p; }
	throw std::bad_alloc();
}
void *operator new(std::size_t s, std::nothrow_t) noexcept
{
	noteA();
	return std::malloc(s != 0 ? s : 1);
}
void *operator new[](std::size_t s, std::nothrow_t) noexcept
{
	noteA();
	return std::malloc(s != 0 ? s : 1);
}
void *operator new(std::size_t s, std::align_val_t a)
{
	noteA();
	auto align = static_cast<std::size_t>(a);
	auto bytes = s != 0 ? s : 1;
#if defined(_MSC_VER)
	if (void *p = _aligned_malloc(bytes, align < sizeof(void *) ? sizeof(void *) : align)) { return p; }
#else
	void *p = nullptr;
	if (posix_memalign(&p, align < sizeof(void *) ? sizeof(void *) : align, bytes) == 0) { return p; }
#endif
	throw std::bad_alloc();
}
void *operator new[](std::size_t s, std::align_val_t a) { return ::operator new(s, a); }
void *operator new(std::size_t s, std::align_val_t a, std::nothrow_t) noexcept
{
	try { return ::operator new(s, a); } catch (...) { return nullptr; }
}
void *operator new[](std::size_t s, std::align_val_t a, std::nothrow_t) noexcept
{
	try { return ::operator new(s, a); } catch (...) { return nullptr; }
}
void operator delete(void *p) noexcept { noteF(p); std::free(p); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void *p, std::nothrow_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::nothrow_t) noexcept { ::operator delete(p); }
void operator delete(void *p, std::align_val_t) noexcept
{
	noteF(p);
#if defined(_MSC_VER)
	_aligned_free(p);
#else
	std::free(p);
#endif
}
void operator delete[](void *p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void *p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void *p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void *p, std::align_val_t a, std::nothrow_t) noexcept { ::operator delete(p, a); }
void operator delete[](void *p, std::align_val_t a, std::nothrow_t) noexcept { ::operator delete(p, a); }
namespace
{
constexpr float kGuard = 12345.0f;
constexpr std::size_t kGuardN = 8;
struct Guarded
{
	std::vector<float> in0;
	std::vector<float> in1;
	std::vector<float> out0;
	std::vector<float> out1;
	const float *ins[2]{};
	float *outs[2]{};
	void setup(unsigned frames)
	{
		in0.assign(frames + kGuardN, 0.0f);
		in1.assign(frames + kGuardN, 0.0f);
		out0.assign(frames + kGuardN, kGuard);
		out1.assign(frames + kGuardN, kGuard);
		for (unsigned i = 0; i < frames; ++i) {
			float v = ((i % 16) == 0) ? 0.5f : 0.1f;
			in0[i] = v;
			in1[i] = v;
		}
		for (std::size_t i = frames; i < frames + kGuardN; ++i) { in0[i] = kGuard; in1[i] = kGuard; }
		ins[0] = in0.data(); ins[1] = in1.data();
		outs[0] = out0.data(); outs[1] = out1.data();
	}
	bool guardsOk(unsigned frames) const
	{
		for (std::size_t i = frames; i < frames + kGuardN; ++i) {
			if (out0[i] != kGuard || out1[i] != kGuard) { return false; }
			if (in0[i] != kGuard || in1[i] != kGuard) { return false; }
		}
		return true;
	}
	bool finite(unsigned frames) const
	{
		for (unsigned i = 0; i < frames; ++i) {
			if (!std::isfinite(out0[i]) || !std::isfinite(out1[i])) { return false; }
		}
		return true;
	}
};
bool writeWav(QString const &path, int frames, int rate)
{
	QByteArray pcm;
	auto const value = static_cast<qint16>(0.4f * 32767.f);
	for (int i = 0; i < frames; ++i) {
		pcm.append(static_cast<char>(value & 0xff));
		pcm.append(static_cast<char>((value >> 8) & 0xff));
	}
	QByteArray out;
	auto put32 = [&out](quint32 v) { for (int s = 0; s < 32; s += 8) { out.append(static_cast<char>((v >> s) & 0xff)); } };
	auto put16 = [&out](quint16 v) { out.append(static_cast<char>(v & 0xff)); out.append(static_cast<char>((v >> 8) & 0xff)); };
	out.append("RIFF");
	put32(36 + static_cast<quint32>(pcm.size()));
	out.append("WAVEfmt ");
	put32(16); put16(1); put16(1);
	put32(static_cast<quint32>(rate)); put32(static_cast<quint32>(rate * 2));
	put16(2); put16(16);
	out.append("data");
	put32(static_cast<quint32>(pcm.size()));
	out.append(pcm);
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(out) == out.size();
}
}
class SfzDistoBuffersTest : public QObject
{
	Q_OBJECT
private slots:
	void preparedOrdinaryPath();
	void capacityExhaustionUsesBoundedChunks();
	void failedReconfigurationPreservesOldPair();
	void realtimeProcessAllocatesNothing();
	void synthPreparedDistoPath();
	void unpreparedProcessIsSafe();
};
void SfzDistoBuffersTest::preparedOrdinaryPath()
{
	sfz::fx::Disto d;
	d.setSampleRate(48000.0);
	d.setSamplesPerBlock(64);
	unsigned sizes[] = {1, 7, 64};
	for (unsigned frames : sizes) {
		Guarded g; g.setup(frames);
		d.process(g.ins, g.outs, frames);
		QVERIFY(g.finite(frames));
		QVERIFY(g.guardsOk(frames));
	}
}
void SfzDistoBuffersTest::capacityExhaustionUsesBoundedChunks()
{
	sfz::fx::Disto d;
	d.setSampleRate(48000.0);
	d.setSamplesPerBlock(32);
	unsigned sizes[] = {32, 33, 100};
	for (unsigned frames : sizes) {
		Guarded g; g.setup(frames);
		d.process(g.ins, g.outs, frames);
		QVERIFY(g.finite(frames));
		QVERIFY(g.guardsOk(frames));
	}
}
void SfzDistoBuffersTest::failedReconfigurationPreservesOldPair()
{
	sfz::fx::Disto d;
	d.setSampleRate(48000.0);
	d.setSamplesPerBlock(16);
	armFail(2);
	bool threw = false;
	try { d.setSamplesPerBlock(1024); } catch (std::bad_alloc const &) { threw = true; }
	disarmFail();
	QVERIFY(threw);
	{
		Guarded g; g.setup(200);
		d.process(g.ins, g.outs, 200);
		QVERIFY(g.finite(200));
		QVERIFY(g.guardsOk(200));
	}
	d.setSamplesPerBlock(128);
	{
		Guarded g; g.setup(128);
		d.process(g.ins, g.outs, 128);
		QVERIFY(g.finite(128));
		QVERIFY(g.guardsOk(128));
	}
	d.setSamplesPerBlock(16);
	{
		Guarded g; g.setup(16);
		d.process(g.ins, g.outs, 16);
		QVERIFY(g.finite(16));
		QVERIFY(g.guardsOk(16));
	}
}
void SfzDistoBuffersTest::realtimeProcessAllocatesNothing()
{
	sfz::fx::Disto d;
	d.setSampleRate(48000.0);
	d.setSamplesPerBlock(32);
	Guarded g; g.setup(32);
	g_count = true; g_alloc = 0; g_free = 0;
	d.process(g.ins, g.outs, 32);
	g_count = false;
	QCOMPARE(g_alloc, std::uint64_t{0});
	QCOMPARE(g_free, std::uint64_t{0});
	{
		Guarded h; h.setup(100);
		g_count = true; g_alloc = 0; g_free = 0;
		d.process(h.ins, h.outs, 100);
		g_count = false;
		QCOMPARE(g_alloc, std::uint64_t{0});
		QCOMPARE(g_free, std::uint64_t{0});
		QVERIFY(h.finite(100));
		QVERIFY(h.guardsOk(100));
	}
	{
		Guarded z; z.setup(1);
		g_count = true; g_alloc = 0; g_free = 0;
		d.process(z.ins, z.outs, 0);
		g_count = false;
		QCOMPARE(g_alloc, std::uint64_t{0});
		QCOMPARE(g_free, std::uint64_t{0});
	}
}
void SfzDistoBuffersTest::synthPreparedDistoPath()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QString const rate = QStringLiteral("48000");
	QVERIFY(writeWav(dir.path() + "/tone.wav", 4000, 48000));
	QByteArray sfz = "<global> amp_veltrack=0\n<effect> type=disto\n<region> sample=tone.wav key=60\n";
	QFile f(dir.path() + "/test.sfz");
	QVERIFY(f.open(QIODevice::WriteOnly) && f.write(sfz) == sfz.size());
	f.close();
	sfz::Synth synth;
	synth.setSampleRate(48000.0f);
	synth.setSamplesPerBlock(64);
	QVERIFY(synth.loadSfzFile((dir.path() + "/test.sfz").toStdString()));
	QCOMPARE(synth.getNumRegions(), 1);
	synth.noteOn(0, 60, 100);
	std::vector<float> left(64, 0.0f);
	std::vector<float> right(64, 0.0f);
	sfz::AudioSpan<float> span{{left.data(), right.data()}, 64};
	synth.renderBlock(span);
	bool ok = true;
	for (unsigned i = 0; i < 64; ++i) { if (!std::isfinite(left[i]) || !std::isfinite(right[i])) { ok = false; } }
	(void)rate;
	QVERIFY(ok);
}
void SfzDistoBuffersTest::unpreparedProcessIsSafe()
{
	sfz::fx::Disto d;
	d.setSampleRate(48000.0);
	{
		Guarded g; g.setup(1);
		d.process(g.ins, g.outs, 0);
		QVERIFY(g.guardsOk(1));
	}
	{
		Guarded g; g.setup(32);
		d.process(g.ins, g.outs, 32);
		QVERIFY(g.finite(32));
		QVERIFY(g.guardsOk(32));
	}
}
QTEST_GUILESS_MAIN(SfzDistoBuffersTest)
#include "SfzDistoBuffersTest.moc"
