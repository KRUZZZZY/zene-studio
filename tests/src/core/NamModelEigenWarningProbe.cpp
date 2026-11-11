/*
 * NamModelEigenWarningProbe.cpp - keeps the NAM targets' vendored-Eigen warning
 * suppression honest.
 *
 * Vendored Eigen (plugins/NeuralAmp/rtneural/modules/Eigen) multiplies the
 * unnamed enumerators of two gebp_traits specialisations in its aarch64 NEON
 * blocking kernel (GeneralBlockPanelKernel.h:157 and :216). GCC reports that as
 * -Wdeprecated-enum-enum-conversion, and -DUSE_WERROR turns a diagnostic in
 * unmodified third-party headers into a build failure; that is what stopped the
 * linux-arm64 CI job while compiling NamModelLoaderTest (run 42805458633,
 * job 129793314368).
 *
 * Every target that compiles the NAM engine sources therefore carries
 * -Wno-error=deprecated-enum-enum-conversion (the plugin target and its test
 * harnesses did; NamModelLoaderTest had been left out). This translation unit
 * performs the same enum arithmetic through Eigen's own traits, so a target
 * that loses that option stops compiling here instead of failing later on an
 * arm64 runner.
 *
 * Clang names the diagnostic -Wdeprecated-anon-enum-enum-conversion and does not
 * emit it while compiling the NAM engine, so the probe is GCC-only.
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

#include <Eigen/Dense>

namespace lmms::nam
{

bool eigenDeprecatedEnumArithmeticProbe() noexcept
{
#if defined(__GNUC__) && !defined(__clang__)
	// The exact arithmetic GCC flags in GeneralBlockPanelKernel.h: mr from one
	// gebp_traits specialisation multiplied by nr from another.
	using GenericTraits = Eigen::internal::gebp_traits<float, float, false, false,
		Eigen::Architecture::Generic>;
	using NeonTraits = Eigen::internal::gebp_traits<float, float, false, false,
		Eigen::Architecture::NEON>;
	return GenericTraits::mr * NeonTraits::nr > 0;
#else
	return true;
#endif
}

}  // namespace lmms::nam
