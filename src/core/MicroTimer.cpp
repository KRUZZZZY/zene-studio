#include "MicroTimer.h"

#include <limits>

namespace lmms
{

using namespace std;
using namespace std::chrono;

static_assert(ratio_less_equal<steady_clock::duration::period, micro>::value, 
	"MicroTimer: steady_clock doesn't support microsecond resolution");

MicroTimer::MicroTimer()
{
	reset();
}

void MicroTimer::reset()
{
	begin = steady_clock::now();
}

int MicroTimer::elapsed() const
{
	auto now = steady_clock::now();
	const auto elapsedMicroseconds = duration_cast<microseconds>(now - begin).count();
	return elapsedMicroseconds > numeric_limits<int>::max()
		? numeric_limits<int>::max()
		: static_cast<int>(elapsedMicroseconds);
}

} // namespace lmms
