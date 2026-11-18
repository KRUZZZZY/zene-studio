#include "ValueBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace lmms
{

namespace
{

std::size_t checkedLength(int length)
{
	if (length < 0)
	{
		throw std::invalid_argument("ValueBuffer length must be non-negative");
	}
	return static_cast<std::size_t>(length);
}

}

ValueBuffer::ValueBuffer(int length)
	: std::vector<float>(checkedLength(length))
{}

void ValueBuffer::fill(float value)
{
	std::fill(begin(), end(), value);
}

float ValueBuffer::value(int offset) const
{
	const int bufferLength = length();
	if (bufferLength == 0) { return 0.0f; }
	const int index = offset % bufferLength;
	return at(index < 0 ? index + bufferLength : index);
}

const float *ValueBuffer::values() const
{
	return data();
}

float *ValueBuffer::values()
{
	return data();
}

int ValueBuffer::length() const
{
	return size();
}

void ValueBuffer::interpolate(float start, float end_)
{
	float i = 0;
	std::generate(begin(), end(), [&]() { return std::lerp(start, end_, i++ / length()); });
}


} // namespace lmms
