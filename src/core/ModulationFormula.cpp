/*
 * ModulationFormula.cpp - a modulator's own formula, compiled once and evaluated on the audio thread
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

#include "ModulationFormula.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace lmms
{

namespace
{

using Op = FormulaProgram::Op;

constexpr double kPi = 3.14159265358979323846;

double fraction(double a) { return a - std::floor(a); }

//! rand's whole number: @a a's floor, held inside int64's range (a cast outside it is undefined), 0 when not finite.
std::int64_t wholeNumber(double a)
{
	constexpr double kLimit = 9.0e18;
	return std::isfinite(a) ? static_cast<std::int64_t>(std::clamp(std::floor(a), -kLimit, kLimit)) : 0;
}

//! The one-argument functions, in Op order from Negate.
using Unary = double (*)(double);
constexpr Unary kUnary[] = {
	[](double a) { return -a; },
	[](double a) { return std::sin(a); },
	[](double a) { return std::cos(a); },
	[](double a) { return std::fabs(a); },
	[](double a) { return std::floor(a); },
	[](double a) { return a < 0.0 ? 0.0 : std::sqrt(a); },
	[](double a) { return modulationRandomAt(wholeNumber(a)); },
	[](double a) { return 1.0 - 4.0 * std::fabs(fraction(a) - 0.5); },
	[](double a) { return 2.0 * fraction(a) - 1.0; },
	[](double a) { return fraction(a) < 0.5 ? 1.0 : -1.0; },
};

//! The two-argument functions and operators, in Op order from Add.
using Binary = double (*)(double, double);
constexpr Binary kBinary[] = {
	[](double a, double b) { return a + b; },
	[](double a, double b) { return a - b; },
	[](double a, double b) { return a * b; },
	[](double a, double b) { return b == 0.0 ? 0.0 : a / b; },
	[](double a, double b) { return b == 0.0 ? 0.0 : std::fmod(a, b); },
	[](double a, double b) { return std::pow(a, b); },
	[](double a, double b) { return std::min(a, b); },
	[](double a, double b) { return std::max(a, b); },
};

const std::map<QString, Op>& functions()
{
	static const std::map<QString, Op> table{{QStringLiteral("sin"), Op::Sin}, {QStringLiteral("cos"), Op::Cos},
		{QStringLiteral("abs"), Op::Abs}, {QStringLiteral("floor"), Op::Floor}, {QStringLiteral("sqrt"), Op::Sqrt},
		{QStringLiteral("rand"), Op::Random}, {QStringLiteral("tri"), Op::Tri}, {QStringLiteral("saw"), Op::Saw},
		{QStringLiteral("square"), Op::Square}, {QStringLiteral("min"), Op::Min}, {QStringLiteral("max"), Op::Max}};
	return table;
}

bool isBinary(Op op) { return op >= Op::Add; }

//! A recursive-descent compiler over the grammar in the header; it tracks the stack the program
//! will need so the evaluator's fixed stack can never overflow.
class Compiler
{
public:
	Compiler(const QString& text, FormulaProgram* out) : m_text(text), m_out(out) {}

	bool run(QString* why)
	{
		const bool ok = expression() && (skip(), m_pos == m_text.size() || fail(QStringLiteral("unexpected '%1'").arg(m_text.at(m_pos))));
		if (!ok && why != nullptr) { *why = m_error; }
		return ok;
	}

private:
	void skip()
	{
		while (m_pos < m_text.size() && m_text.at(m_pos).isSpace()) { ++m_pos; }
	}

	bool accept(QChar c)
	{
		skip();
		if (m_pos < m_text.size() && m_text.at(m_pos) == c) { ++m_pos; return true; }
		return false;
	}

	bool fail(const QString& what)
	{
		if (m_error.isEmpty()) { m_error = QStringLiteral("%1 at character %2").arg(what).arg(m_pos + 1); }
		return false;
	}

	bool put(Op op, float value = 0.f)
	{
		if (m_out->count >= FormulaProgram::MaxOps) { return fail(QStringLiteral("the formula is too long")); }
		m_depth += op <= Op::Cycle ? 1 : isBinary(op) ? -1 : 0;
		if (m_depth > FormulaProgram::MaxDepth) { return fail(QStringLiteral("the formula nests too deeply")); }
		m_out->ops[static_cast<std::size_t>(m_out->count)] = op;
		m_out->constants[static_cast<std::size_t>(m_out->count)] = value;
		++m_out->count;
		return true;
	}

	//! Left-associative operators over @a next: @a ops maps a character to its Op.
	bool chain(bool (Compiler::*next)(), const std::map<QChar, Op>& ops)
	{
		if (!(this->*next)()) { return false; }
		for (;;)
		{
			skip();
			const auto found = m_pos < m_text.size() ? ops.find(m_text.at(m_pos)) : ops.end();
			if (found == ops.end()) { return true; }
			++m_pos;
			if (!(this->*next)() || !put(found->second)) { return false; }
		}
	}

	bool expression()
	{
		static const std::map<QChar, Op> ops{{QLatin1Char('+'), Op::Add}, {QLatin1Char('-'), Op::Sub}};
		return chain(&Compiler::term, ops);
	}

	bool term()
	{
		static const std::map<QChar, Op> ops{{QLatin1Char('*'), Op::Mul}, {QLatin1Char('/'), Op::Div}, {QLatin1Char('%'), Op::Mod}};
		return chain(&Compiler::unary, ops);
	}

	bool unary()
	{
		if (accept(QLatin1Char('-'))) { return unary() && put(Op::Negate); }
		accept(QLatin1Char('+'));
		return power();
	}

	//! '^' binds tighter than a unary minus before it (-2^2 is -4, as usual), takes one after it (2^-1)
	//! and groups to the right (2^3^2 is 2^9).
	bool power()
	{
		if (!primary()) { return false; }
		return !accept(QLatin1Char('^')) || (unary() && put(Op::Pow));
	}

	//! Whether the character at @a i is a digit (or, with @a point, a decimal point).
	bool digitAt(int i, bool point = false) const
	{
		return i < m_text.size() && (m_text.at(i).isDigit() || (point && m_text.at(i) == QLatin1Char('.')));
	}

	//! An exponent ("e3", "E-2") starts at the current character.
	bool exponentAhead() const
	{
		const bool marker = m_pos < m_text.size() && m_text.at(m_pos).toLower() == QLatin1Char('e');
		return marker && (digitAt(m_pos + 1) || (m_pos + 2 < m_text.size() && m_text.at(m_pos + 1) == QLatin1Char('-') && digitAt(m_pos + 2)));
	}

	bool number()
	{
		const int start = m_pos;
		while (digitAt(m_pos, true)) { ++m_pos; }
		if (exponentAhead())
		{
			m_pos += 2;
			while (digitAt(m_pos)) { ++m_pos; }
		}
		bool ok = false;
		const double value = m_text.mid(start, m_pos - start).toDouble(&ok);
		return ok ? put(Op::Constant, static_cast<float>(value)) : fail(QStringLiteral("a malformed number"));
	}

	bool call(Op op)
	{
		if (!accept(QLatin1Char('('))) { return fail(QStringLiteral("a function needs its argument in brackets")); }
		if (!expression()) { return false; }
		if (isBinary(op) && (!accept(QLatin1Char(',')) || !expression())) { return fail(QStringLiteral("min and max take two arguments")); }
		return accept(QLatin1Char(')')) ? put(op) : fail(QStringLiteral("a missing ')'"));
	}

	bool name()
	{
		const int start = m_pos;
		while (m_pos < m_text.size() && m_text.at(m_pos).isLetter()) { ++m_pos; }
		const QString word = m_text.mid(start, m_pos - start).toLower();
		if (word == QLatin1String("t")) { return put(Op::Phase); }
		if (word == QLatin1String("x")) { return put(Op::Cycle); }
		if (word == QLatin1String("pi")) { return put(Op::Constant, static_cast<float>(kPi)); }
		if (word == QLatin1String("e")) { return put(Op::Constant, static_cast<float>(std::exp(1.0))); }
		const auto found = functions().find(word);
		return found != functions().end() ? call(found->second) : fail(QStringLiteral("unknown name '%1'").arg(word));
	}

	bool primary()
	{
		skip();
		if (m_pos >= m_text.size()) { return fail(QStringLiteral("the formula ends too early")); }
		const QChar c = m_text.at(m_pos);
		if (c.isDigit() || c == QLatin1Char('.')) { return number(); }
		if (c.isLetter()) { return name(); }
		if (!accept(QLatin1Char('('))) { return fail(QStringLiteral("unexpected '%1'").arg(c)); }
		return expression() && (accept(QLatin1Char(')')) || fail(QStringLiteral("a missing ')'")));
	}

	const QString& m_text;
	FormulaProgram* m_out;
	int m_pos = 0;
	int m_depth = 0;
	QString m_error;
};

} // namespace

bool compileFormula(const QString& text, FormulaProgram* program, QString* why)
{
	*program = FormulaProgram{};
	if (text.trimmed().isEmpty() || text.size() > FormulaProgram::MaxLength)
	{
		if (why != nullptr) { *why = QStringLiteral("a formula is 1..%1 characters").arg(FormulaProgram::MaxLength); }
		return false;
	}
	FormulaProgram compiled;
	if (!Compiler(text, &compiled).run(why)) { return false; }
	*program = compiled;
	return true;
}

double evaluateFormula(const FormulaProgram& program, double phase, double cycles) noexcept
{
	if (program.empty()) { return 0.0; }
	std::array<double, FormulaProgram::MaxDepth + 1> stack{};
	int top = -1;
	for (int i = 0; i < program.count; ++i)
	{
		const Op op = program.ops[static_cast<std::size_t>(i)];
		if (op <= Op::Cycle)
		{
			const double pushed = op == Op::Phase ? phase : op == Op::Cycle ? cycles : program.constants[static_cast<std::size_t>(i)];
			stack[static_cast<std::size_t>(++top)] = pushed;
			continue;
		}
		if (isBinary(op))
		{
			const double b = stack[static_cast<std::size_t>(top--)];
			double& a = stack[static_cast<std::size_t>(top)];
			a = kBinary[static_cast<int>(op) - static_cast<int>(Op::Add)](a, b);
			continue;
		}
		double& a = stack[static_cast<std::size_t>(top)];
		a = kUnary[static_cast<int>(op) - static_cast<int>(Op::Negate)](a);
	}
	const double value = top >= 0 ? stack[static_cast<std::size_t>(top)] : 0.0;
	return std::isfinite(value) ? std::clamp(value, -1.0, 1.0) : 0.0;
}

} // namespace lmms
