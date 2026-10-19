/*
 * MpeConsumerProbe.h - the readback interface of the MPE test consumer fixture
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*! The observation surface of tests/src/plugins/MpeTestConsumer.cpp, so a test
 *  can read back WHAT the consumer received and WHERE (the frame offset inside
 *  the period Q050-016 schedules each expression point at). The consumer's
 *  audio level alone cannot show the offset - it applies a period's expression
 *  to the whole period - which is exactly why this probe exists.
 *
 *  The class is marked default-visibility: the build compiles with
 *  -fvisibility=hidden, and a test binary dynamic_casts the loaded module's
 *  instrument to this interface, which needs the typeinfo to be shared across
 *  the module boundary. Windows test hosts cannot load plugin modules (the
 *  module's import descriptor names zene.exe), so the probe is only ever used
 *  on the platforms where that works.
 */

#ifndef LMMS_TESTS_MPE_CONSUMER_PROBE_H
#define LMMS_TESTS_MPE_CONSUMER_PROBE_H

#include <cstdint>

#if defined(_WIN32)
#define MPE_CONSUMER_PROBE_EXPORT
#else
#define MPE_CONSUMER_PROBE_EXPORT __attribute__((visibility("default")))
#endif

namespace lmms
{
namespace testmpe
{

//! The three MPE axes, in the order Q050-016 schedules them.
enum class Axis
{
	Pitch = 0,
	Pressure = 1,
	Timbre = 2,
	Count = 3
};

class MPE_CONSUMER_PROBE_EXPORT ConsumerProbe
{
public:
	virtual ~ConsumerProbe() = default;

	//! The last value of \a axis that reached channel \a channel.
	virtual int value( Axis axis, int channel ) const = 0;
	//! The frame offset inside the period of the last \a axis event.
	virtual std::uint64_t lastOffset( Axis axis, int channel ) const = 0;
	//! How many \a axis events reached \a channel since its last note-on.
	virtual int eventCount( Axis axis, int channel ) const = 0;
};

} // namespace testmpe
} // namespace lmms

#endif // LMMS_TESTS_MPE_CONSUMER_PROBE_H
