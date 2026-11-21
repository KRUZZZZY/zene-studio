/*
 * RackMacrosTest.cpp - the MACRO half of the rack.* command group (SPEC
 *                      A11-A16): the named, persisted scalars that drive
 *                      existing model parameters through range windows.
 *
 * The rack ENGINE landed with #599 (include/Rack.h, the <rack> element inside a
 * <mixerchannel>, RackTest); its report (docs/RACKS.md section 5) names macros
 * as the half it did not do. The load-bearing assertion here is that a macro's
 * write is REAL and EXACT: the window is a fraction of the parameter's own
 * min..max, so one macro value produces 25 on a 0..100 control and -50 on a
 * -100..100 one, and an inverted window drives the other way. A macro that only
 * stored a number would pass a weaker test and drive nothing.
 *
 * The zone half is RackZonesTest.cpp; the fixture both share is
 * RackTestSupport.h.
 *
 * Evidence is printed unconditionally (stdout, flushed) so the numbers can be
 * pasted into docs/RACK-MACROS.md.
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
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QString>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "AllocationProbe.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ControlRackSupport.h"
#include "ControlReversibility.h"
#include "RackMacros.h"
#include "RackTestSupport.h"
#include "RackZones.h"
#include "ReversibilityTestSupport.h"

using namespace racktest;

namespace
{

//! What control.id_contract reports as the session's fx-<n> count.
int fxIdCount()
{
	const ControlResult contract = revtest::run(QStringLiteral("control.id_contract"));
	if (!contract.ok) { return -1; }
	for (const QJsonValue& family : contract.result.value(QStringLiteral("families")).toArray())
	{
		const QJsonObject entry = family.toObject();
		if (entry.value(QStringLiteral("prefix")).toString() == QLatin1String("fx-"))
		{
			return entry.value(QStringLiteral("count")).toInt();
		}
	}
	return -1;
}

} // namespace

class RackMacrosTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void cleanupTestCase();

	//! A macro writes the REAL model, through the window, exactly - on two
	//! parameters whose ranges are deliberately different, including an
	//! inverted window.
	void macrosDriveParametersThroughTheirWindows();

	//! 0.5.0 L10 (MX-105): a target's curve shapes the response across its window, and is saved
	//! only when it is not linear.
	void aCurveShapesTheResponse();

	//! A target whose chain, effect or parameter is gone is SKIPPED, and the
	//! applied count says so - it is never guessed at.
	void aMacroSkipsTargetsItCannotResolve();

	//! A macro's value is a FINITE 0..1 scalar. std::clamp does not make a
	//! nonfinite value one - every comparison against NaN is false, so clamp
	//! hands NaN straight back - and QString reads a "nan" attribute as NaN,
	//! so a project can carry one. Such a value is refused at every door that
	//! stores it, and it never reaches a parameter model.
	void aNonfiniteMacroValueIsRefused();

	//! A macro target's window is a pair of fractions and a curve, and a NaN is
	//! not a fraction: every comparison against NaN is false, so the range
	//! checks let it through - and the applied fraction is then a NaN, which
	//! lands in the parameter model.
	void aNonfiniteMacroTargetNeverReachesAParameter();

	//! A project may not carry a macro value or a target window outside the
	//! 0..1 contract: the loader clamps the value into it and drops a window
	//! that is not a fraction pair, instead of driving the parameter to its
	//! maximum for every macro value. A project that is inside the contract
	//! loads exactly as it did.
	void aProjectLoadedMacroIsValidated();

	//! With macros and zones configured the rack's own process() allocates
	//! nothing: a macro is not on the audio path, and a zone's routing (0.5.0
	//! E20) reads blocks prepared when the rack re-wired.
	void configuringMacrosAndZonesLeavesTheAudioPathAllocatingNothing();

	//! The group is registered: every id exists, carries both schemas, and has
	//! the SPEC A16 class the contract table states.
	void theGroupIsRegisteredWithSchemasAndA16Classes();

	//! The end-to-end path an agent takes: add a macro, bind a parameter, drive
	//! it - then take it back with control.undo, which must restore BOTH the
	//! macro's value and the parameter it wrote.
	void theSurfaceCreatesDrivesAndUndoesAMacro();

	/*! control.id_contract's fx- count answers "how many fx- ids does this
	 *  session have". A mixer channel carries devices on its own chain, on the
	 *  rack's parallel chains and on its post-fader inserts - and all three hand
	 *  out fx- ids (ControlDeviceSupport's rackChainEffect walks exactly them) -
	 *  so the count must move when a device lands on any of the three. */
	void theIdContractCountsEveryChainThatHandsOutFxIds();

	//! Typed refusals: a caller is told what is wrong, and never gets "ok" for a
	//! selection or a bind that cannot exist.
	void theSurfaceRefusesWhatCannotExist();

	//! Luna cluster 5257: removing a chain renumbers the macro targets that
	//! named a LATER chain, and drops the ones that named the chain that is
	//! gone. Without that, a target on the chain after the removed one drives
	//! the wrong device - and a target on the removed chain drives whichever
	//! chain slid into its index.
	void removingAChainReindexesTheMacroTargets();

	//! A macro/zone id that does not PARSE is a statement about the arguments,
	//! not about the rack: "macro-x" is malformed in the same way a blank name
	//! is, and resolveRackChannel already answers invalid_args for "ch-x". The
	//! two cases must not share one not_found: a caller retrying on not_found
	//! can never fix a typo, and a caller checking the rack's size from that
	//! message is told a real rack question about a malformed string.
	void malformedMacroAndZoneIdsAreInvalidArgs();

	//! Integer index fields must reject values that cannot be represented by
	//! the handlers' int indices before any floating-point narrowing occurs.
	void integerIndexArgumentsAreBoundedToInt();

	//! A repeated selector value is not an undo step of its own.
	void aNoOpSelectionDoesNotConsumeUndo();

	//! A repeated macro value with no changed target write is not an undo step.
	void aNoOpMacroSetDoesNotConsumeUndo();

	//! A macro with an unchanged scalar still needs undo when it repairs a target.
	void anUnchangedMacroScalarStillUndoesItsParameterWrite();

	//! Cluster 5524: a project load and a model initialization are not a person's
	//! change, so neither may become the "last touched" model (ED-191). This is
	//! the same contract as the macro pair above - the store names what a command
	//! wrote - read where a load writes instead.
	void aLoadDoesNotClaimTheLastTouchedModel();

	//! Repeating an unchanged XY pair must leave the first pair edit on top.
	void aNoOpMacroPairDoesNotConsumeUndo();

	//! One undo restores a parameter shared by both axes to its pre-pair value.
	void aNoOpSharedParameterMacroPairDoesNotConsumeUndo();
};

#include "RackMacrosTestParts1.h"
#include "RackMacrosTestParts2.h"

QTEST_GUILESS_MAIN(RackMacrosTest)
#include "RackMacrosTest.moc"
