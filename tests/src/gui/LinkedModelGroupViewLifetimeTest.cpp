/*
 * LinkedModelGroupViewLifetimeTest.cpp - detached control layout item lifetime
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

#include <QtTest>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "ControlLayout.h"
#include "Controls.h"
#include "LinkedModelGroupViews.h"

namespace allocation_probe
{

constexpr std::size_t freedCapacity = 4096;
std::array<std::atomic<std::uintptr_t>, freedCapacity> freedAddresses{};
std::atomic<std::size_t> freedCount{0};
std::atomic<bool> tracking{false};

void recordFreedAddress(void* address) noexcept
{
	if (tracking.load(std::memory_order_relaxed))
	{
		const auto index = freedCount.fetch_add(1, std::memory_order_relaxed) % freedCapacity;
		freedAddresses[index].store(reinterpret_cast<std::uintptr_t>(address),
			std::memory_order_relaxed);
	}
}

void beginTracking() noexcept
{
	tracking.store(false, std::memory_order_relaxed);
	for (auto& address : freedAddresses) { address.store(0, std::memory_order_relaxed); }
	freedCount.store(0, std::memory_order_relaxed);
	tracking.store(true, std::memory_order_relaxed);
}

void endTracking() noexcept
{
	tracking.store(false, std::memory_order_relaxed);
}

bool wasFreed(std::uintptr_t address) noexcept
{
	for (const auto& freedAddress : freedAddresses)
	{
		if (freedAddress.load(std::memory_order_relaxed) == address) { return true; }
	}
	return false;
}

} // namespace allocation_probe

void* operator new(std::size_t size)
{
	if (void* address = std::malloc(size)) { return address; }
	throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete(void* address) noexcept
{
	allocation_probe::recordFreedAddress(address);
	std::free(address);
}

void operator delete[](void* address) noexcept
{
	allocation_probe::recordFreedAddress(address);
	std::free(address);
}

void operator delete(void* address, std::size_t) noexcept
{
	::operator delete(address);
}

void operator delete[](void* address, std::size_t) noexcept
{
	::operator delete[](address);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, tag);
}

void operator delete(void* address, const std::nothrow_t&) noexcept
{
	::operator delete(address);
}

void operator delete[](void* address, const std::nothrow_t&) noexcept
{
	::operator delete[](address);
}

namespace lmms::gui
{

namespace
{

class TestControl final : public Control
{
public:
	QWidget* topWidget() override { return new QWidget; }
	void setText(const QString&) override {}
	void setModel(AutomatableModel*) override {}
	AutomatableModel* model() override { return nullptr; }
	AutomatableModelView* modelView() override { return nullptr; }
};

class TestLinkedModelGroupView final : public LinkedModelGroupView
{
public:
	TestLinkedModelGroupView() : LinkedModelGroupView(nullptr, nullptr, 1) {}
	using LinkedModelGroupView::addControl;
	using LinkedModelGroupView::removeControl;
};

} // namespace

class LinkedModelGroupViewLifetimeTest : public QObject
{
	Q_OBJECT

private slots:
	void removingAControlFreesItsDetachedLayoutItem()
	{
		TestLinkedModelGroupView view;
		view.addControl(new TestControl, "control", "control", false);
		auto* layout = static_cast<ControlLayout*>(view.layout());
		QVERIFY(layout != nullptr);
		QLayoutItem* item = layout->itemByString(QStringLiteral("control"));
		QVERIFY(item != nullptr);
		const auto itemAddress = reinterpret_cast<std::uintptr_t>(item);

		allocation_probe::beginTracking();
		view.removeControl(QStringLiteral("control"));
		allocation_probe::endTracking();

		QVERIFY2(allocation_probe::wasFreed(itemAddress),
			"removeControl must delete the detached layout item");
		QVERIFY(layout->itemByString(QStringLiteral("control")) == nullptr);
	}
};

} // namespace lmms::gui

QTEST_MAIN(lmms::gui::LinkedModelGroupViewLifetimeTest)
#include "LinkedModelGroupViewLifetimeTest.moc"
