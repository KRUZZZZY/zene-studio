/*
 * MixerModeBar.cpp - the bar across the mixer that shows and sets its view, density, filter and modes
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
#include "MixerModeBar.h"

#include <algorithm>

#include <QButtonGroup>
#include <QComboBox>
#include <QLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

#include "ChannelPickDialog.h"
#include "CommandForm.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerHardwareMenu.h"
#include "MixerListen.h"
#include "MixerOverlays.h"
#include "MixerRoutingMatrix.h"
#include "MixerStrip.h"
#include "MixerZones.h"
#include "MixerViewOptions.h"

namespace lmms::gui
{

namespace
{

const QString kViewCommand = QStringLiteral("window.mixer_view");

//! A checkable entry running @a id with @a args, checked when it is the state in force.
void commandChoice(QMenu* menu, const QString& label, const QString& id, const QJsonObject& args, bool checked)
{
	QAction* action = addCommandAction(menu, label, id, [id, args] { return CommandCall{id, args, {}, QString(), false}; });
	action->setCheckable(true);
	action->setChecked(checked);
}

//! Name @a chip's state; @a active (a mode that changes what is heard) fills it with the accent as well.
void setChip(QToolButton* chip, const QString& text, bool active)
{
	if (chip->property("active").toBool() != active)
	{
		chip->setProperty("active", active);
		const tokens::Palette& pal = tokens::palette();
		chip->setStyleSheet(active ? QStringLiteral("QToolButton { background-color: %1; color: %2; }")
			.arg(pal.accent.name(), pal.textOnAccent.name()) : QString());
	}
	if (chip->text() == text) { return; }  // each set is announced
	chip->setText(text);
	chip->setAccessibleName(text);
}

//! The names of the channels listening, in mixer order.
QStringList listeningNames(const mixerstripinfo::Context& context)
{
	QList<int> indices = context.listening.values();
	std::sort(indices.begin(), indices.end());
	QStringList names;
	for (const int index : indices)
	{
		if (index < Engine::mixer()->numChannels()) { names << Engine::mixer()->mixerChannel(index)->m_name; }
	}
	return names;
}

//! gui-1030: lays the bar's controls out left to right and wraps them onto another row when the mixer is
//! too narrow for one, so none is squeezed or pushed past the bar's edge (after Qt's flow layout example).
class WrapLayout : public QLayout
{
public:
	explicit WrapLayout(QWidget* parent) : QLayout(parent) {}
	~WrapLayout() override { qDeleteAll(m_items); }
	void addItem(QLayoutItem* item) override { m_items.append(item); }
	void alignRightFrom(QWidget* widget) { m_rightAlignedFrom = widget; }
	int count() const override { return static_cast<int>(m_items.size()); }
	QLayoutItem* itemAt(int index) const override { return m_items.value(index); }
	QLayoutItem* takeAt(int index) override
	{
		return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
	}
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
	void setGeometry(const QRect& rect) override
	{
		QLayout::setGeometry(rect);
		arrange(rect, true);
	}
	//! One row: what the bar asks for when there is room.
	QSize sizeHint() const override
	{
		QSize row;
		for (QLayoutItem* item : shown())
		{
			row.rwidth() += item->sizeHint().width() + (row.width() > 0 ? spacing() : 0);
			row.setHeight(std::max(row.height(), item->sizeHint().height()));
		}
		return row.grownBy(contentsMargins());
	}
	//! The widest control alone, so the mixer can be narrowed down to it.
	QSize minimumSize() const override
	{
		QSize widest;
		for (QLayoutItem* item : shown()) { widest = widest.expandedTo(item->sizeHint()); }
		return widest.grownBy(contentsMargins());
	}

private:
	QList<QLayoutItem*> shown() const
	{
		QList<QLayoutItem*> items;
		for (QLayoutItem* item : m_items) { if (!item->isEmpty()) { items << item; } }
		return items;
	}
	//! Fill rows inside @a rect (placing the items if @a apply); the height they take.
	int arrange(const QRect& rect, bool apply) const
	{
		const QRect area = rect.marginsRemoved(contentsMargins());
		const QList<QLayoutItem*> items = shown();
		int y = area.y();
		for (int first = 0; first < items.size();)
		{
			int last = first;
			int width = items.at(first)->sizeHint().width();
			while (last + 1 < items.size() && width + spacing() + items.at(last + 1)->sizeHint().width() <= area.width())
			{
				width += spacing() + items.at(++last)->sizeHint().width();
			}
			const QList<QLayoutItem*> row = items.mid(first, last - first + 1);
			const bool alignRight = last + 1 == items.size() && std::any_of(row.begin(), row.end(), [this](QLayoutItem* item) {
				return item->widget() == m_rightAlignedFrom;
			});
			const int height = placeRow(row, area, y, apply, alignRight);
			y += height + spacing();
			first = last + 1;
		}
		const int used = items.isEmpty() ? 0 : y - spacing() - area.y();
		return used + contentsMargins().top() + contentsMargins().bottom();
	}
	int rightAlignedGroupWidth(const QList<QLayoutItem*>& row) const
	{
		const auto first = std::find_if(row.begin(), row.end(), [this](QLayoutItem* item) {
			return item->widget() == m_rightAlignedFrom;
		});
		int width = 0;
		int count = 0;
		for (auto item = first; item != row.end(); ++item) { width += (*item)->sizeHint().width(); ++count; }
		return width + std::max(0, count - 1) * spacing();
	}
	//! One row from @a x at @a y, each item centred in its height; the row's height.
	int placeRow(const QList<QLayoutItem*>& row, const QRect& area, int y, bool apply, bool alignRight) const
	{
		int height = 0;
		for (QLayoutItem* item : row) { height = std::max(height, item->sizeHint().height()); }
		int x = area.x();
		int rightAlignedGroupX = x;
		if (alignRight) { rightAlignedGroupX = area.x() + area.width() - rightAlignedGroupWidth(row); }
		for (QLayoutItem* item : row)
		{
			if (alignRight && item->widget() == m_rightAlignedFrom) { x = std::max(x, rightAlignedGroupX); }
			const QSize size = item->sizeHint();
			if (apply) { item->setGeometry(QRect(QPoint(x, y + (height - size.height()) / 2), size)); }
			x += size.width() + spacing();
		}
		return height;
	}

	QList<QLayoutItem*> m_items;
	QWidget* m_rightAlignedFrom = nullptr;
};

} // namespace

MixerModeBar::MixerModeBar(QWidget* parent) :
	QWidget(parent),
	m_filter(new QComboBox(this))
{
	setObjectName(QStringLiteral("mixerModeBar"));
	setAccessibleName(tr("Mixer modes"));
	auto* layout = new WrapLayout(this);  // gui-1030: wraps in a narrow mixer
	layout->setContentsMargins(tokens::space::sm, tokens::space::xs, tokens::space::sm, tokens::space::xs);
	layout->setSpacing(tokens::space::sm);
	m_viewButtons = addSegment(layout, tr("View"), "view", mixerviewoptions::viewNames(), {tr("Mix"), tr("Routing")});
	m_densityButtons = addSegment(layout, tr("Density"), "density", mixerviewoptions::densityNames(), {tr("Compact"), tr("Expanded")});
	auto* show = new QLabel(tr("Show:"), this);
	show->setBuddy(m_filter);
	layout->addWidget(show);
	const QStringList captions{tr("All Channels"), tr("Instruments"), tr("Audio"), tr("Returns"), tr("Used"), tr("Selected")};
	for (int i = 0; i < captions.size(); ++i) { m_filter->addItem(captions.at(i), mixerviewoptions::filterNames().at(i)); }
	m_filter->setObjectName(QStringLiteral("mixerModeFilter"));
	m_filter->setAccessibleName(tr("Show channels"));
	m_filter->setProperty("controlCommand", kViewCommand);
	connect(m_filter, QOverload<int>::of(&QComboBox::activated), this, [this](int row) { setOption(QStringLiteral("filter"), m_filter->itemData(row).toString()); });
	layout->addWidget(m_filter);
	auto* pick = new QPushButton(tr("Pick..."), this);  // MX-005: the channels the Selected filter shows
	pick->setObjectName(QStringLiteral("mixerModePickSelected"));
	pick->setMinimumHeight(tokens::targetSize());
	pick->setAccessibleName(tr("Pick the channels to show"));
	pick->setToolTip(tr("Pick the channels the Selected filter shows (Ctrl+click a strip to add or remove one)"));
	pick->setProperty("controlCommand", kViewCommand);
	connect(pick, &QPushButton::clicked, this, &MixerModeBar::pickSelected);
	layout->addWidget(pick);
	// MX-021: the Selected filter follows the focused arranger track (or the focused clip's track).
	m_follow = new QPushButton(tr("Follow Selection"), this);
	m_follow->setObjectName(QStringLiteral("mixerModeFollow"));
	m_follow->setCheckable(true);
	m_follow->setMinimumHeight(tokens::targetSize());
	m_follow->setAccessibleName(tr("Follow the arranger selection"));
	m_follow->setToolTip(tr("With the Selected filter on, show the focused arranger track's channel"));
	m_follow->setProperty("controlCommand", kViewCommand);
	connect(m_follow, &QPushButton::toggled, this, [this](bool on) {
		setOption(QStringLiteral("follow_selection"), on);
	});
	layout->addWidget(m_follow);
	m_faders = addChip(layout, QStringLiteral("mixerModeFaders"), tr("What every fader drives: its channel's volume, or its send to one return"), &MixerModeBar::fillFaders);
	m_solo = addChip(layout, QStringLiteral("mixerModeSolo"), tr("The solo mode: whether a solo unsolos the others"), &MixerModeBar::fillSolo);
	m_listen = addChip(layout, QStringLiteral("mixerModeListen"), tr("The listen bus: the channels heard in place of the mix, after or before their faders"), &MixerModeBar::fillListen);
	m_master = addChip(layout, QStringLiteral("mixerModeMaster"), tr("Where the master plays (its hardware output) and its speaker format"), &MixerModeBar::fillMaster);
	m_zones = addChip(layout, QStringLiteral("mixerModeZones"), tr("Where the mixer's regions are docked and how wide their separators leave them"), &MixerModeBar::fillZones);
	auto* matrix = new QPushButton(tr("Routing Matrix..."), this);
	matrix->setObjectName(QStringLiteral("mixerModeMatrix"));
	matrix->setMinimumHeight(tokens::targetSize());
	matrix->setProperty("controlCommand", QStringLiteral("routing.matrix"));
	connect(matrix, &QPushButton::clicked, this, [] { showRoutingMatrix(); });
	layout->addWidget(matrix);
	layout->alignRightFrom(m_faders);  // keep the mode chips and matrix at the right on a wide mixer
	refresh(mixerstripinfo::buildContext());
}

QHash<QString, QPushButton*> MixerModeBar::addSegment(QLayout* layout, const QString& label, const char* key,
	const QStringList& values, const QStringList& captions)
{
	auto* caption = new QLabel(label + QLatin1Char(':'), this);
	layout->addWidget(caption);
	auto* group = new QButtonGroup(this);
	QHash<QString, QPushButton*> buttons;
	for (int i = 0; i < values.size(); ++i)
	{
		auto* button = new QPushButton(captions.at(i), this);
		button->setObjectName(QStringLiteral("mixerMode_%1").arg(values.at(i)));
		button->setCheckable(true);
		button->setMinimumHeight(tokens::targetSize());
		button->setAccessibleName(QStringLiteral("%1: %2").arg(label, captions.at(i)));
		button->setProperty("controlCommand", kViewCommand);
		group->addButton(button);
		layout->addWidget(button);
		const QString name = QLatin1String(key);
		const QString value = values.at(i);
		connect(button, &QPushButton::clicked, this, [this, name, value] { setOption(name, value); });
		buttons.insert(value, button);
	}
	caption->setBuddy(buttons.value(values.value(0)));
	return buttons;
}

QToolButton* MixerModeBar::addChip(QLayout* layout, const QString& objectName, const QString& hint,
	void (MixerModeBar::*fill)(QMenu*))
{
	auto* chip = new QToolButton(this);
	chip->setObjectName(objectName);
	chip->setPopupMode(QToolButton::InstantPopup);
	chip->setToolButtonStyle(Qt::ToolButtonTextOnly);
	chip->setMinimumHeight(tokens::targetSize());
	chip->setFocusPolicy(Qt::StrongFocus);
	chip->setToolTip(hint);
	chip->setAccessibleDescription(hint);
	auto* menu = new QMenu(chip);
	// Built when it opens, so it shows the state now; the submenus of the last opening go with it.
	connect(menu, &QMenu::aboutToShow, this, [this, menu, fill] {
		for (QMenu* old : menu->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly)) { old->deleteLater(); }
		menu->clear();
		(this->*fill)(menu);
	});
	connect(menu, &QMenu::triggered, this, &MixerModeBar::changed);
	chip->setMenu(menu);
	layout->addWidget(chip);
	return chip;
}

void MixerModeBar::setOption(const QString& key, const QJsonValue& value)
{
	// A refusal is shown by runCommand; the bar follows the state either way at the view's next pass.
	runCommand(this, CommandCall{kViewCommand, {{key, value}}, {}, QString(), false});
	emit changed();
}

void MixerModeBar::pickSelected()
{
	const QStringList picked = pickChannels(this, tr("Channels to Show"), QString());
	if (picked.isEmpty()) { return; }  // cancelled: the selection and the filter stay as they were
	runCommand(this, CommandCall{kViewCommand, {{QStringLiteral("selected_channels"), QJsonArray::fromStringList(picked)},
		{QStringLiteral("filter"), QStringLiteral("selected")}}, {}, QString(), false});
	emit changed();
}

void MixerModeBar::fillFaders(QMenu* menu)
{
	const int target = mixeroverlays::sendsOnFaders();
	commandChoice(menu, tr("Volume Faders"), kViewCommand, {{QStringLiteral("sends_on_faders"), QString()}}, target < 0);
	menu->addSeparator();
	Mixer* mixer = Engine::mixer();
	for (int i = 1; i < mixer->numChannels(); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		if (!channel->isBus() && i != target) { continue; }
		commandChoice(menu, tr("Sends to %1").arg(channel->m_name), kViewCommand,
			{{QStringLiteral("sends_on_faders"), control::channelIdOf(channel)}}, i == target);
	}
}

void MixerModeBar::fillSolo(QMenu* menu)
{
	const bool additive = mixerstrip::additiveSolo();
	const QString id = QStringLiteral("mixer.solo_mode");
	commandChoice(menu, tr("Exclusive (a solo unsolos the others)"), id, {{QStringLiteral("mode"), QStringLiteral("exclusive")}}, !additive);
	commandChoice(menu, tr("Additive (each solo adds to the others)"), id, {{QStringLiteral("mode"), QStringLiteral("additive")}}, additive);
}

void MixerModeBar::fillListen(QMenu* menu)
{
	const bool pfl = mixerlisten::mode() == mixerlisten::Mode::PreFader;
	const QString id = QStringLiteral("mixer.listen_mode");
	commandChoice(menu, tr("After the Fader (AFL)"), id, {{QStringLiteral("mode"), QStringLiteral("afl")}}, !pfl);
	commandChoice(menu, tr("Before the Fader (PFL)"), id, {{QStringLiteral("mode"), QStringLiteral("pfl")}}, pfl);
	menu->addSeparator();
	QAction* clear = addCommandAction(menu, tr("Clear the Listen Bus"), id,
		[id] { return CommandCall{id, {{QStringLiteral("clear"), true}}, {}, QString(), false}; });
	clear->setEnabled(clear->isEnabled() && mixerlisten::anyListening());
}

void MixerModeBar::fillMaster(QMenu* menu)
{
	const QString master = control::channelIdOf(Engine::mixer()->mixerChannel(0));
	addHardwareMenu(menu, master);
	addSurroundMenu(menu, master);
}

void MixerModeBar::fillZones(QMenu* menu)
{
	const QString kZonesCommand = QStringLiteral("window.mixer_zones");
	const auto run = [menu](const QJsonObject& args) {
		runCommand(menu->parentWidget(), CommandCall{QStringLiteral("window.mixer_zones"), args, {}, QString(), false});
	};
	for (const QString& id : mixerzones::sideRegionIds())
	{
		QMenu* region = menu->addMenu(mixerzones::regionTitle(id));
		region->setProperty("controlCommand", kZonesCommand);
		const gui::mixerzones::Layout& layout = mixerzones::MixerZoneState::instance()->layout();
		for (const QString& dock : {QStringLiteral("left"), QStringLiteral("right")})
		{
			QAction* action = region->addAction(mixerzones::dockTitle(dock));
			action->setProperty("controlCommand", kZonesCommand);
			action->setCheckable(true);
			action->setChecked(layout.docks.value(id) == dock);
			connect(action, &QAction::triggered, menu, [run, id, dock] {
				run({{QStringLiteral("dock"), id + QLatin1Char('=') + dock}});
			});
		}
	}
	menu->addSeparator();
	QAction* reset = menu->addAction(tr("Reset the Layout"));
	reset->setProperty("controlCommand", kZonesCommand);
	connect(reset, &QAction::triggered, menu, [run] { run({{QStringLiteral("reset"), true}}); });
}

void MixerModeBar::refresh(const mixerstripinfo::Context& context)
{
	const mixerviewoptions::Options& options = mixerviewoptions::options();
	if (QPushButton* view = m_viewButtons.value(options.view); view != nullptr && !view->isChecked()) { view->setChecked(true); }
	if (QPushButton* density = m_densityButtons.value(options.density); density != nullptr && !density->isChecked()) { density->setChecked(true); }
	const int row = m_filter->findData(options.filter);
	if (row != m_filter->currentIndex())
	{
		const QSignalBlocker quiet(m_filter);
		m_filter->setCurrentIndex(row);
	}
	if (m_follow->isChecked() != mixerviewoptions::followSelection())
	{
		const QSignalBlocker quiet(m_follow);
		m_follow->setChecked(mixerviewoptions::followSelection());
	}
	m_follow->setEnabled(options.filter == QLatin1String("selected") || mixerviewoptions::followSelection());
	refreshChips(context);
}

void MixerModeBar::refreshChips(const mixerstripinfo::Context& context)
{
	Mixer* mixer = Engine::mixer();
	const int target = mixeroverlays::sendsOnFaders();
	setChip(m_faders, target < 0 ? tr("Faders: Volume") : tr("Faders: Sends to %1").arg(mixer->mixerChannel(target)->m_name), target >= 0);
	setChip(m_solo, mixerstrip::additiveSolo() ? tr("Solo: Additive") : tr("Solo: Exclusive"), false);
	const QStringList names = listeningNames(context);
	const QString point = context.preFaderListen ? tr("PFL") : tr("AFL");
	setChip(m_listen, names.isEmpty() ? tr("Listen: Off")
		: tr("Listen %1: %2").arg(point, names.size() == 1 ? names.first() : tr("%1 channels").arg(names.size())), !names.isEmpty());
	const mixerstripinfo::StripInfo master = mixerstripinfo::describe(*mixer, 0, context);
	setChip(m_master, master.format.isEmpty() ? tr("Master: %1").arg(master.hardware)
		: tr("Master: %1, %2").arg(master.hardware, master.format), false);
	const gui::mixerzones::Layout& zones = mixerzones::MixerZoneState::instance()->layout();
	const auto side = [&zones](const QString& dock) {
		QStringList names;
		for (const QString& id : mixerzones::sideRegionIds())
		{
			if (zones.docks.value(id) == dock) { names << mixerzones::regionTitle(id); }
		}
		return names.isEmpty() ? tr("none") : names.join(QStringLiteral(", "));
	};
	setChip(m_zones, tr("Zones: %1 | %2").arg(side(QStringLiteral("left")), side(QStringLiteral("right"))), false);
}

} // namespace lmms::gui
