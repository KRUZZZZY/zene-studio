/*
 * WasmSandboxHostFuelTest.cpp - the host's fuel budget must bound STARTUP too
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*! The regression for cluster new-p14-378.
 *
 * `wasm.load` takes a `fuel` argument ("'fuel' overrides the per-call fuel
 * budget"), and WasmSandboxHost::load() is the only place a caller can ask for
 * a budget other than the ABI default. The sandbox itself already treats the
 * CONFIGURED budget as the one in force while a module is instantiated:
 * `WasmSandbox::Impl::primeFuelBudget()` (src/wasm/WasmSandbox.cpp:271-279)
 * sets the store's fuel before instantiation, because "instantiation runs the
 * module's start function ... and needs the same headroom".
 *
 * The host set that budget AFTER the module was instantiated, so a module's
 * START function ran under the default budget (1 000 000) whatever the caller
 * asked for. A caller that asks for 100 fuel and gets a module whose start
 * function spends 1 000 000 is not confined at all: the budget it asked for
 * bounded nothing.
 *
 * The test is deterministic and needs no clock: the fixture is a module whose
 * start function spins a fixed number of times, so its fuel cost is a property
 * of the module, not of the machine it runs on. The small budget is chosen far
 * below the start function's cost and far below the default budget, so the
 * outcome cannot depend on the runtime's exact fuel accounting.
 */

#include "WasmSandboxHost.h"

#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <cstdint>
#include <string>

namespace
{

/*! A module whose START function costs real fuel.
 *
 * 1000 loop iterations of a handful of wasm instructions each: far more than
 * the 100 fuel the test asks for, far less than the ABI's default budget of
 * 1 000 000. The counter is written to a global and to linear memory so the
 * loop cannot be optimised away.
 */
const char kStartFuelWat[] = R"(
(module
  (memory (export "memory") 1)
  (global $counter (mut i32) (i32.const 0))
  (func $start
    (local $i i32)
    (loop $spin
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (global.set $counter (local.get $i))
      (br_if $spin (i32.lt_u (local.get $i) (i32.const 1000)))))
  (start $start))
)";

//! The budget the test asks for: less than the start function costs.
constexpr std::uint64_t kSmallBudget = 100;

//! Write the fixture module into \p dir and return its path.
QString writeModule(const QTemporaryDir& dir)
{
	const QString path = dir.path() + "/startup-fuel.wat";
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return {}; }
	file.write(kStartFuelWat);
	file.close();
	return path;
}

} // namespace

class WasmSandboxHostFuelTest : public QObject
{
	Q_OBJECT

private slots:
	void requestedBudgetBoundsModuleStartup();
	void defaultBudgetStillHostsTheSameModule();
	void refusedLoadKeepsTheHostedModule();
};

void WasmSandboxHostFuelTest::requestedBudgetBoundsModuleStartup()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = writeModule(dir);
	QVERIFY(!path.isEmpty());

	lmms::wasm::WasmSandboxHost& host = lmms::wasm::WasmSandboxHost::instance();
	host.unload();

	// The start function costs more than the requested budget, so hosting the
	// module under that budget must be refused - the budget the caller asked
	// for has to be in force while the module STARTS, not only while it runs.
	const lmms::wasm::HostLoadOutcome outcome = host.load(path.toStdString(), kSmallBudget);
	QVERIFY2(!outcome.ok,
		"a module whose start function spends more fuel than the requested budget "
		"must be refused, not hosted under the default budget");
	QVERIFY(!outcome.error.empty());
	QVERIFY(!host.isLoaded());
	qInfo("new-p14-378: a %llu fuel budget refused the module: %s",
		static_cast<unsigned long long>(kSmallBudget), outcome.error.c_str());
}

void WasmSandboxHostFuelTest::defaultBudgetStillHostsTheSameModule()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = writeModule(dir);
	QVERIFY(!path.isEmpty());

	lmms::wasm::WasmSandboxHost& host = lmms::wasm::WasmSandboxHost::instance();
	host.unload();

	// The same module, under the ABI's default budget: it fits, so it loads.
	// This is the control that pins the refusal above to the BUDGET and not to
	// a module the runtime cannot run.
	const lmms::wasm::HostLoadOutcome outcome = host.load(path.toStdString(), 0);
	QVERIFY2(outcome.ok,
		qPrintable(QString::fromStdString("the default budget must host the module: "
										  + outcome.error)));
	QVERIFY(host.isLoaded());
	QCOMPARE(outcome.fuelBudget, lmms::wasm::abi::defaultFuelBudget);
	QCOMPARE(host.fuelBudget(), lmms::wasm::abi::defaultFuelBudget);
}

void WasmSandboxHostFuelTest::refusedLoadKeepsTheHostedModule()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = writeModule(dir);
	QVERIFY(!path.isEmpty());

	lmms::wasm::WasmSandboxHost& host = lmms::wasm::WasmSandboxHost::instance();
	host.unload();

	// A module hosted under the default budget is running...
	const lmms::wasm::HostLoadOutcome loaded = host.load(path.toStdString(), 0);
	QVERIFY2(loaded.ok, qPrintable(QString::fromStdString(loaded.error)));
	QVERIFY(host.isLoaded());

	// ...and a later load that the requested budget refuses leaves it alone:
	// `wasm.load` is all-or-nothing (src/core/ControlCommandsWasmEdit.cpp:143).
	const lmms::wasm::HostLoadOutcome refused = host.load(path.toStdString(), kSmallBudget);
	QVERIFY(!refused.ok);
	QVERIFY(host.isLoaded());
	QCOMPARE(host.modulePath(), path.toStdString());
}

QTEST_MAIN(WasmSandboxHostFuelTest)
#include "WasmSandboxHostFuelTest.moc"
