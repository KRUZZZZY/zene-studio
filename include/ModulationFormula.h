/*
 * ModulationFormula.h - a modulator's own formula, compiled once and evaluated on the audio thread
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_MODULATION_FORMULA_H
#define LMMS_MODULATION_FORMULA_H

#include <array>
#include <cstdint>

#include <QString>

#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 L10 (MX-118): a formula a user writes for a modulator, compiled on the control thread into
 *  a short postfix program - plain values in fixed arrays, so it is copied to the audio thread with
 *  the rest of the modulator's source and evaluated there with no allocation, no lock and a bounded
 *  stack.
 *
 *  The language: numbers; t (where in its cycle the modulator is, 0..1) and x (cycles since it
 *  started); pi and e; + - * / % ^ and unary minus, with the usual precedence and parentheses;
 *  sin cos abs floor sqrt (one argument, radians for sin and cos), tri saw square (a waveform of a
 *  cycle position: -1..1), rand (a repeatable random value -1..1 for each whole number), min max
 *  (two arguments). The result is clamped to -1..1; a division by zero, a root of a negative
 *  number or anything not finite reads as 0. Examples: sin(2*pi*t), tri(t)*0.5 + rand(x)*0.5,
 *  square(t*4) * (1 - t). */
struct FormulaProgram
{
	static constexpr int MaxOps = 32;
	static constexpr int MaxDepth = 12;
	static constexpr int MaxLength = 256;

	//! Pushes, then one-argument functions, then two-argument ones: the evaluator dispatches by range.
	enum class Op : std::uint8_t
	{
		Constant, Phase, Cycle,
		Negate, Sin, Cos, Abs, Floor, Sqrt, Random, Tri, Saw, Square,
		Add, Sub, Mul, Div, Mod, Pow, Min, Max
	};

	std::array<Op, MaxOps> ops{};
	std::array<float, MaxOps> constants{};
	int count = 0;

	bool empty() const { return count == 0; }
};

//! Compiles @a text (control thread). False, with @a why naming the problem and where it is, for
//! anything outside the language or over the bounds; @a program is then left empty.
LMMS_EXPORT bool compileFormula(const QString& text, FormulaProgram* program, QString* why);

//! The program's value at cycle position @a phase (0..1) after @a cycles cycles, in -1..1 (0 for an
//! empty program). Audio thread: plain arithmetic.
LMMS_EXPORT double evaluateFormula(const FormulaProgram& program, double phase, double cycles) noexcept;

//! A repeatable value in -1..1 for the whole number @a n (a hash - no state), shared by the layer's
//! random shapes and the formula's rand().
inline double modulationRandomAt(std::int64_t n)
{
	auto x = static_cast<std::uint64_t>(n) * 0x9E3779B97F4A7C15ULL;
	x ^= x >> 31;
	x *= 0xBF58476D1CE4E5B9ULL;
	x ^= x >> 29;
	return static_cast<double>(x >> 11) / static_cast<double>(1ULL << 53) * 2.0 - 1.0;
}

} // namespace lmms

#endif // LMMS_MODULATION_FORMULA_H
