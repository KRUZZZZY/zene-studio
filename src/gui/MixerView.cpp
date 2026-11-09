/*
 * MixerView.cpp - effect-mixer-view for LMMS
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * This file is part of LMMS - https://lmms.io
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include "MixerView.h"
#include "VcaGroup.h"
#include "VcaStripView.h"
/* ---------------------------------------------------------------------------
 * THE VIEW LIST AND THE MIXER'S CHANNEL LIST ARE NOT THE SAME LIST.
 * One MixerView per mixer channel, and THE MIXER is indexed with a VIEW index -
 * here, in updateFaders(), in the solo/mute wiring. The ENGINE deletes channels
 * without telling the view (Mixer::deleteChannel: TrackFolder::releaseRouting(),
 * mixer.remove_channel, Mixer::clear() on a project load), and Mixer::mixerChannel()
 * does not bounds-check, so a view index past the mixer's last channel is a wild
 * pointer: measured SIGSEGV inside QObject::disconnectImpl at shutdown, exit code
 * -11. So the LOOKUP is bounded by the MIXER's own channel count, and the TEARDOWN is
 * not: a surplus view must still be DELETED, because a view left alive with a dead
 * model is painted later and walks it (measured: SIGSEGV in
 * Fader::calculateKnobPosYFromModel). A SHORTER list needs nothing, every view-side
 * lookup already being `i < views.size()`.
 * Those bounds cover the LOOKUPS; they cannot cover a PAINT. A strip resolves its
 * channel by index when it is painted and its faders hold the dead channel's models,
 * so a surplus strip left for the 500 ms sync is painted against freed memory
 * (measured: SIGSEGV in MixerChannelView::paintEvent, a folder releasing its routing
 * channel with the mixer on screen - BUGS_FOUND 11.27). The mixer therefore announces
 * every delete (Mixer::channelDeleted) and the view rebuilds before it can paint.
 * --------------------------------------------------------------------------- */

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QTimer>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStyle>
#include <QKeyEvent>
#include <QStackedLayout>
#include <QStackedWidget>

#include "EffectRackView.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "CommandForm.h"
#include "ControlMeterSupport.h"
#include "ControlRegistry.h"
#include "MixerModeBar.h"
#include "MixerOverlays.h"
#include "MixerSendsBanner.h"
#include "MixerStripInfo.h"
#include "MixerStripTags.h"
#include "MixerViewOptions.h"
#include "MixerZones.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "PatternStore.h"
#include "SampleTrack.h"
#include "SendButtonIndicator.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SubWindow.h"
#include "TrackContainer.h" // For TrackContainer::TrackList typedef
#include "embed.h"

namespace lmms::gui
{


namespace
{

//! The strips the channel area always keeps in view; it scrolls the rest. Few enough that the master, this
//! and the rack fit beside the Inspector and the Browser on a 1024 px display (gui-1030).
constexpr int MinVisibleStrips = 2;

//! Holds the mode bar so its 13 controls do not set the Mix panel's minimum width (and so the main window's):
//! a narrower panel scrolls the bar sideways instead of pushing the rack off a small display.
class ModeBarArea : public QScrollArea
{
public:
	ModeBarArea(QWidget* bar, QWidget* parent) : QScrollArea(parent)
	{
		setWidgetResizable(true);
		setFrameStyle(QFrame::NoFrame);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		setWidget(bar);
		fitHeight();
	}
	QSize minimumSizeHint() const override { return {0, height()}; }
	QSize sizeHint() const override { return {widget()->sizeHint().width(), height()}; }

protected:
	void resizeEvent(QResizeEvent* event) override
	{
		QScrollArea::resizeEvent(event);
		fitHeight();
	}

private:
	//! The bar's own height, plus the scroll bar's only while the bar is wider than the viewport.
	void fitHeight()
	{
		const bool scrolls = widget()->minimumSizeHint().width() > width();
		const int wanted = widget()->sizeHint().height() + (scrolls ? horizontalScrollBar()->sizeHint().height() : 0);
		if (wanted != minimumHeight()) { setFixedHeight(wanted); }
	}
};

} // namespace


MixerView::MixerView(Mixer* mixer) :
	QWidget(),
	ModelView(nullptr, this),
	SerializingObjectHook(),
	m_mixer(mixer)
{
	mixer->setHook(this);

	//QPalette pal = palette();
	//pal.setColor(QPalette::Window, QColor(72, 76, 88));
	//setPalette(pal);

	setAutoFillBackground(true);

	setWindowTitle(tr("Mixer"));
	setWindowIcon(embed::getIconPixmap("mixer"));

	// main-layout, under the mode bar (0.5.0 theme 16) and the sends-on-faders banner (theme 6), which is
	// there only in that mode
	auto* outer = new QVBoxLayout{this};
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	m_modeBar = new MixerModeBar(this);
	connect(m_modeBar, &MixerModeBar::changed, this, &MixerView::syncWithMixer, Qt::QueuedConnection);
	auto* modeBarArea = new ModeBarArea(m_modeBar, this);
	// MX-021: with following on, a new arranger selection re-lays the Selected filter's strips.
	connect(ShellSelection::instance(), &ShellSelection::changed, this, &MixerView::syncWithMixer, Qt::QueuedConnection);
	outer->addWidget(modeBarArea);
	m_sendsBanner = new MixerSendsBanner(this);
	outer->addWidget(m_sendsBanner);

	// Q050-227.1: the body is one horizontal splitter of dockable regions, so every boundary is a
	// separator the user can drag. The channels' own dock keeps the scrolling area and the add
	// buttons together, which is why a separator never falls between them.
	m_zoneSplitter = new QSplitter(Qt::Horizontal, this);
	m_zoneSplitter->setObjectName(QStringLiteral("mixerZones"));
	m_zoneSplitter->setChildrenCollapsible(false);
	m_zoneSplitter->setHandleWidth(3);
	outer->addWidget(m_zoneSplitter, 1);
	m_zoneParking = new QWidget(this);
	m_zoneParking->setObjectName(QStringLiteral("mixerZoneParking"));
	m_zoneParking->hide();
	m_middleHost = new QWidget(this);
	m_middleHost->setObjectName(QStringLiteral("mixerChannelsDock"));
	auto ml = new QHBoxLayout(m_middleHost);
	ml->setContentsMargins(0, 4, 0, 0);
	ml->setSpacing(0);

	// A docked region's host gives it the top margin the body always had, whatever side it is on.
	const auto dockHost = [this](const QString& id, QWidget* content) {
		auto* host = new QWidget(this);
		host->setObjectName(QStringLiteral("mixerZone_") + id);
		auto* box = new QVBoxLayout(host);
		box->setContentsMargins(0, 4, 0, 0);
		box->setSpacing(0);
		box->addWidget(content);
		m_zoneHosts.insert(id, host);
		return host;
	};

	// Channel area
	m_channelAreaWidget = new QWidget;
	chLayout = new QHBoxLayout(m_channelAreaWidget);
	chLayout->setSizeConstraint(QLayout::SetMinimumSize);
	chLayout->setSpacing(0);
	chLayout->setContentsMargins(0, 0, 0, 0);
	chLayout->setAlignment(Qt::AlignLeft);
	m_channelAreaWidget->setLayout(chLayout);

	// create rack layout before creating the first channel
	m_racksWidget = new QWidget;
	m_racksLayout = new QStackedLayout(m_racksWidget);
	m_racksLayout->setContentsMargins(0, 0, 0, 0);
	m_racksWidget->setLayout(m_racksLayout);

	// add master channel
	m_mixerChannelViews.resize(mixer->numChannels());
	MixerChannelView * masterView = new MixerChannelView(this, this, 0);
	connectToSoloAndMute(0);
	m_mixerChannelViews[0] = masterView;

	m_racksLayout->addWidget(m_mixerChannelViews[0]->m_effectRackView);

	auto mixerChannelSize = masterView->sizeHint();

	// add mixer channels
	for (int i = 1; i < m_mixerChannelViews.size(); ++i)
	{
		m_mixerChannelViews[i] = new MixerChannelView(m_channelAreaWidget, this, i);
		connectToSoloAndMute(i);
		chLayout->addWidget(m_mixerChannelViews[i]);
	}

	// add the scrolling section to the main layout
	// class solely for scroll area to pass key presses down
	class ChannelArea : public QScrollArea
	{
		public:
			ChannelArea(QWidget* parent, MixerView* mv) :
				QScrollArea(parent), m_mv(mv) { setAccessibleName(MixerView::tr("Mixer channels")); } // R8.5
			~ChannelArea() override = default;
			void keyPressEvent(QKeyEvent* e) override
			{
				m_mv->keyPressEvent(e);
			}
		private:
			MixerView* m_mv;
	};
	channelArea = new ChannelArea(this, this);
	channelArea->setWidget(m_channelAreaWidget);
	channelArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	channelArea->setFrameStyle(QFrame::NoFrame);
	channelArea->setMinimumWidth(mixerChannelSize.width() * MinVisibleStrips);
	channelArea->setWidgetResizable(true);

	int const scrollBarExtent = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
	channelArea->setMinimumHeight(mixerChannelSize.height() + scrollBarExtent);

	// M3.7: the VCA groups' strips (hidden while the mix has none).
	m_vcaArea = new QWidget(this);
	m_vcaArea->setObjectName(QStringLiteral("vcaArea"));
	m_vcaArea->setAccessibleName(tr("VCA groups"));
	m_vcaLayout = new QHBoxLayout(m_vcaArea);
	m_vcaLayout->setContentsMargins(0, 0, 0, 0);
	m_vcaLayout->setSpacing(0);

	// show the add new mixer channel button
	auto newChannelBtn = new QPushButton(embed::getIconPixmap("new_channel"), QString(), this);
	newChannelBtn->setObjectName("newChannelBtn");
	newChannelBtn->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
	newChannelBtn->setFixedWidth(mixerChannelSize.width());
	newChannelBtn->setFocusPolicy(Qt::NoFocus);
	newChannelBtn->setToolTip(tr("New mixer channel"));
	newChannelBtn->setAccessibleName(tr("New mixer channel"));
	connect(newChannelBtn, SIGNAL(clicked()), this, SLOT(addNewChannel()));
	// 0.5.0 L09 (MX-045): a return channel beside it - a bus that sends land on before its fader.
	auto* addColumn = new QWidget(this);
	auto* addLayout = new QVBoxLayout(addColumn);
	addLayout->setContentsMargins(0, 0, 0, 0);
	addLayout->setSpacing(2);
	addLayout->addWidget(newChannelBtn, 1);
	auto* newReturnBtn = new QPushButton(tr("Return"), addColumn);
	newReturnBtn->setObjectName(QStringLiteral("newReturnBtn"));
	newReturnBtn->setToolTip(tr("New return channel (bus.create): sends into it are pre-fader"));
	newReturnBtn->setFixedWidth(mixerChannelSize.width());
	newReturnBtn->setFocusPolicy(Qt::NoFocus);
	newReturnBtn->setProperty("controlCommand", QStringLiteral("bus.create"));
	connect(newReturnBtn, &QPushButton::clicked, this, [this] {
		// A refusal is shown (runCommand); a new return becomes the selected strip.
		const ControlResult created = runCommand(this, CommandCall{QStringLiteral("bus.create"), {}, {}, QString(), false});
		syncWithMixer();
		if (created.ok) { setCurrentMixerChannel(created.result.value(QStringLiteral("index")).toInt()); }
	});
	addLayout->addWidget(newReturnBtn, 0);

	// Q050-227.1: the four regions, each in its own dock host. The channels' dock keeps the scroll
	// area and the add buttons; a separator therefore never falls inside it. Docking, the order and
	// the widths all come from the one layout state, so a restore and a menu choice end in the same
	// relayoutZones() call.
	m_zoneHosts.insert(QStringLiteral("master"), dockHost(QStringLiteral("master"), masterView));
	m_zoneHosts.insert(QStringLiteral("channels"), m_middleHost);
	ml->addWidget(channelArea, 1);
	ml->addWidget(addColumn, 0);
	m_zoneHosts.insert(QStringLiteral("vca"), dockHost(QStringLiteral("vca"), m_vcaArea));

	// add the stacked layout for the effect racks of mixer channels
	m_zoneHosts.insert(QStringLiteral("rack"), dockHost(QStringLiteral("rack"), m_racksWidget));

	for (QWidget* host : m_zoneHosts) { host->setParent(m_zoneParking); }
	m_zoneSplitter->addWidget(m_middleHost);
	relayoutZones();
	connect(mixerzones::MixerZoneState::instance(), &mixerzones::MixerZoneState::changed,
		this, &MixerView::relayoutZones);
	// A drag writes its widths back THROUGH THE REGISTERED COMMAND - the same window.mixer_zones an agent
	// calls - with 'live': true so the command records them without re-laying the splitter under the
	// mouse. The state then lives with the layout and is saved with the project. A relayout's own
	// setSizes() is not a gesture and is skipped.
	connect(m_zoneSplitter, &QSplitter::splitterMoved, this, [this] {
		if (m_applyingZones) { return; }
		// A refused report (typed, never silent) leaves the state as it was; the next sync pass refreshes the
		// mode bar's Zones chip from whatever the state holds.
		runCommand(this, CommandCall{QStringLiteral("window.mixer_zones"),
			{{QStringLiteral("live"), true}, {QStringLiteral("sizes"), zoneSizesJson()}}, {}, QString(), false});
	});

	setCurrentMixerChannel(m_mixerChannelViews[0]);

	// Q050-100: the focused strip's level in words, from the keyboard (and the strip's menu).
	m_speakMeterAction = new QAction(tr("Speak Meter"), this);
	m_speakMeterAction->setObjectName(QStringLiteral("speakMeterAction"));
	m_speakMeterAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+M")));
	m_speakMeterAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	m_speakMeterAction->setProperty("controlCommand", QStringLiteral("meter.speak"));
	m_speakMeterAction->setToolTip(tr("Speak the focused channel's peak and the master short-term loudness"));
	addAction(m_speakMeterAction);
	connect(m_speakMeterAction, &QAction::triggered, this, [this] {
		const MixerChannelView* strip = focusedChannelView();
		if (strip == nullptr) { return; }
		MixerChannel* channel = getMixer()->mixerChannel(strip->channelIndex());
		if (channel == nullptr) { return; }
		runCommand(this, CommandCall{QStringLiteral("meter.speak"),
			{{QStringLiteral("channel"), control::channelIdOf(channel)}}, {}, QString(), false});
	});

	refreshVcaStrips();

	auto* syncTimer = new QTimer(this);
	connect(syncTimer, &QTimer::timeout, this, &MixerView::syncWithMixer);
	syncTimer->start(500);
	connect(mixer, &Mixer::channelDeleted, this, &MixerView::engineDeletedChannel);

	updateGeometry();

	auto* mainWindow = getGUI()->mainWindow();

	// timer for updating faders
	connect(mainWindow, &MainWindow::periodicUpdate, this, &MixerView::updateFaders);

	layout()->setSizeConstraint(QLayout::SetMinimumSize);

	// The shell mounts the mixer as a panel (MainWindow::finalize); it is not a window.

	// we want to receive dataChanged-signals in order to update
	setModel(mixer);
}


QWidget* MixerView::zoneWidget(const QString& regionId) const
{
	return m_zoneHosts.value(regionId);
}


void MixerView::relayoutZones()
{
	if (m_zoneSplitter == nullptr) { return; }
	const mixerzones::Layout& layout = mixerzones::MixerZoneState::instance()->layout();
	m_applyingZones = true;

	// The docked order: side regions in canonical order on the left, the channels in the middle, the
	// rest on the right. A region hidden on purpose keeps its host in the splitter (so its stored
	// width is preserved), and the splitter collapses it; a hidden VCA area or an empty rack is such
	// a region.
	const auto sideHosts = [this, &layout](const QString& dock) {
		QList<QWidget*> out;
		for (const QString& id : mixerzones::sideRegionIds())
		{
			if (layout.docks.value(id) != dock) { continue; }
			if (QWidget* host = m_zoneHosts.value(id)) { out.append(host); }
		}
		return out;
	};
	QList<QWidget*> hosts;
	hosts += sideHosts(QStringLiteral("left"));
	hosts += m_middleHost;
	hosts += sideHosts(QStringLiteral("right"));

	// Park every host first, then add them in the wanted order: QSplitter::addWidget() on a widget
	// that is already a child leaves it where it was, which would keep a re-docked region on the old
	// side of the channels.
	for (QWidget* host : hosts)
	{
		if (host != nullptr) { host->setParent(m_zoneParking); }
	}
	for (QWidget* host : hosts)
	{
		if (host != nullptr) { m_zoneSplitter->addWidget(host); }
	}

	// The widths, side regions only: the channels take what is left. A region whose stored width is
	// missing gets its default, so a layout written by an older build still lays out usable.
	QList<int> widths;
	for (int i = 0; i < m_zoneSplitter->count(); ++i)
	{
		QWidget* host = m_zoneSplitter->widget(i);
		if (host == m_middleHost) { widths << 0; continue; }  // 0 = let the stretch factor decide
		int width = 0;
		for (auto it = m_zoneHosts.cbegin(); it != m_zoneHosts.cend(); ++it)
		{
			if (it.value() == host) { width = layout.sizes.value(it.key(), mixerzones::defaultSize(it.key())); }
		}
		widths << std::max(mixerzones::MinZoneSize, width);
	}
	// The channels' own stretch keeps them the largest part of the body; the explicit widths of the
	// side regions are what a separator drag changes.
	if (m_zoneSplitter->count() > 0) { m_zoneSplitter->setStretchFactor(m_zoneSplitter->indexOf(m_middleHost), 1); }
	m_zoneSplitter->setSizes(widths);

	// A host follows its content: with no VCA group the region is collapsed, not left as an empty band.
	if (QWidget* vca = m_zoneHosts.value(QStringLiteral("vca"))) { vca->setVisible(!getMixer()->vcaGroups().empty()); }
	m_applyingZones = false;
}


QJsonObject MixerView::zoneSizesJson() const
{
	QJsonObject sizes;
	if (m_zoneSplitter == nullptr) { return sizes; }
	const QList<int> current = m_zoneSplitter->sizes();
	for (int i = 0; i < m_zoneSplitter->count() && i < current.size(); ++i)
	{
		QWidget* host = m_zoneSplitter->widget(i);
		if (host == m_middleHost || !host->isVisible()) { continue; }  // a collapsed region keeps its width
		for (auto it = m_zoneHosts.cbegin(); it != m_zoneHosts.cend(); ++it)
		{
			// Clamped to what the command accepts, so a splitter that a style shrank below the minimum
			// still reports a usable width rather than a refused one.
			if (it.value() == host)
			{
				sizes.insert(it.key(), qBound(mixerzones::MinZoneSize, current.at(i), mixerzones::MaxZoneSize));
			}
		}
	}
	return sizes;
}


namespace
{
//! Defined with the other helpers below; the call sites that keep the strips in step use it.
QString channelOrderSignatureOf(Mixer* mixer);
} // namespace


int MixerView::addNewChannel()
{
	// add new mixer channel and redraw the form.
	Mixer * mix = getMixer();

	int newChannelIndex = mix->createChannel();
	m_mixerChannelViews.push_back(new MixerChannelView(m_channelAreaWidget, this, newChannelIndex));
	connectToSoloAndMute(newChannelIndex);
	chLayout->addWidget(m_mixerChannelViews[newChannelIndex]);
	m_racksLayout->addWidget(m_mixerChannelViews[newChannelIndex]->m_effectRackView);

	updateMixerChannel(newChannelIndex);

	// the new strip is already in place: the next sync pass has nothing to rebuild
	m_channelOrderSignature = channelOrderSignatureOf(mix);
	updateMaxChannelSelector();

	return newChannelIndex;
}


void MixerView::refreshDisplay()
{
	// the channel the user was on, by the stable id taken when it was selected: the model may have
	// changed since (a removal before it moves its index), and the views are about to be deleted
	const int selectedId = m_selectedChannelId;

	// delete all views and re-add them
	for (int i = 1; i<m_mixerChannelViews.size(); ++i)
	{
		// First disconnect from the solo/mute models.
		disconnectFromSoloAndMute(i);

		auto * mixerChannelView = m_mixerChannelViews[i];
		chLayout->removeWidget(mixerChannelView);
		m_racksLayout->removeWidget(mixerChannelView->m_effectRackView);

		delete mixerChannelView;
	}
	m_channelAreaWidget->adjustSize();

	// re-add the views
	m_mixerChannelViews.resize(getMixer()->numChannels());
	for (int i = 1; i < m_mixerChannelViews.size(); ++i)
	{
		m_mixerChannelViews[i] = new MixerChannelView(m_channelAreaWidget, this, i);
		connectToSoloAndMute(i);

		chLayout->addWidget(m_mixerChannelViews[i]);
		m_racksLayout->addWidget(m_mixerChannelViews[i]->m_effectRackView);
	}

	// select the same channel as before, or Master when it is gone (or was Master already)
	int selected = 0;
	for (int i = 1; selectedId >= 0 && i < m_mixerChannelViews.size(); ++i)
	{
		if (m_mixerChannelViews[i]->mixerChannel()->id() == selectedId) { selected = i; }
	}
	setCurrentMixerChannel(selected);

	// update all mixer lines
	for (int i = 0; i < m_mixerChannelViews.size(); ++i)
	{
		updateMixerChannel(i);
	}

	m_channelOrderSignature = channelOrderSignatureOf(getMixer());
	updateMaxChannelSelector();
	refreshVcaStrips();
}




namespace
{

QString vcaSignatureOf(const Mixer* mixer)
{
	QString signature;
	for (const VcaGroup* group : mixer->vcaGroups())
	{
		signature += QStringLiteral("%1:%2:%3;").arg(group->id()).arg(group->name())
			.arg(group->members().size());
	}
	return signature;
}

//! The channels' ids, in order: what the strips are bound to. A reorder (mixer.order_by_tracks,
//! moveChannelLeft from the model) keeps the count, so the count alone never notices it.
QString channelOrderSignatureOf(Mixer* mixer)
{
	QString signature;
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		if (channel != nullptr) { signature += QString::number(channel->id()) + QLatin1Char(','); }
	}
	return signature;
}

//! The fader the arrow keys move: the strip's, unless it is a dimmed "No send" fader (theme 6), which moves nothing.
Fader* keyboardFader(MixerChannelView* view)
{
	return view != nullptr && view->fader()->isEnabled() ? view->fader() : nullptr;
}

} // namespace

void MixerView::engineDeletedChannel()
{
	if (m_deletingOwnChannel) { return; }
	if (m_mixerChannelViews.size() != static_cast<int>(getMixer()->numChannels())) { refreshDisplay(); }
}

void MixerView::syncWithMixer()
{
	if (m_mixerChannelViews.size() != static_cast<int>(getMixer()->numChannels())
		|| m_channelOrderSignature != channelOrderSignatureOf(getMixer()))
	{
		// A reorder keeps the count but moves the channels, and each strip binds its solo, mute,
		// pan and volume models to the channel at its own number: rebuild them.
		refreshDisplay();
		return;
	}
	if (vcaSignatureOf(getMixer()) != m_vcaSignature) { refreshVcaStrips(); }
	// 0.5.0 L09 (MX-066, MX-049): folded folders' strips hidden, faders on one send (MixerOverlays.cpp).
	mixeroverlays::apply(m_mixerChannelViews);
	m_sendsBanner->refresh();  // theme 6: the mode named across the top while the faders show one send
	// 0.5.0 L08: the view options (MixerViewOptions.cpp); a change re-lays the send rows.
	if (m_viewOptionsGeneration != mixerviewoptions::generation())
	{
		m_viewOptionsGeneration = mixerviewoptions::generation();
		for (int i = 0; i < m_mixerChannelViews.size(); ++i) { updateMixerChannel(i); }
	}
	for (int i = 0; i < m_mixerChannelViews.size(); ++i)
	{
		MixerChannel* channel = getMixer()->mixerChannel(i);
		if (channel == nullptr || m_mixerChannelViews[i] == nullptr) { continue; }
		mixerstripinfo::describeStrip(m_mixerChannelViews[i], getMixer(), i);
		m_mixerChannelViews[i]->syncName();  // a rename from the Inspector or an agent shows on the strip
		mixerviewoptions::applyToStrip(m_mixerChannelViews[i], channel);
	}
	// 0.5.0 theme 16: each strip's type, routing and modes in words, and the mode bar - read once per pass.
	const mixerstripinfo::Context context = mixerstripinfo::buildContext();
	mixerstriptags::applyAll(getMixer(), m_mixerChannelViews, context);
	m_modeBar->refresh(context);
}

void MixerView::refreshVcaStrips()
{
	m_vcaSignature = vcaSignatureOf(getMixer());
	while (QLayoutItem* item = m_vcaLayout->takeAt(0))
	{
		delete item->widget();
		delete item;
	}
	for (VcaGroup* group : getMixer()->vcaGroups())
	{
		m_vcaLayout->addWidget(new VcaStripView(group, m_vcaArea));
	}
	m_vcaArea->setVisible(!getMixer()->vcaGroups().empty());
	// Q050-227.1: the VCA region is a dock like any other; with no group it has nothing to show and
	// its host collapses, and it comes back at its stored width when a group is made.
	relayoutZones();
}


// update the and max. channel number for every instrument
void MixerView::updateMaxChannelSelector()
{
	const TrackContainer::TrackList& songTracks = Engine::getSong()->tracks();
	const TrackContainer::TrackList& patternStoreTracks = Engine::patternStore()->tracks();

	for (const auto& trackList : {songTracks, patternStoreTracks})
	{
		for (const auto& track : trackList)
		{
			if (track->type() == Track::Type::Instrument)
			{
				auto inst = (InstrumentTrack*)track;
				inst->mixerChannelModel()->setRange(0,
					m_mixerChannelViews.size()-1,1);
			}
			else if (track->type() == Track::Type::Sample)
			{
				auto strk = (SampleTrack*)track;
				strk->mixerChannelModel()->setRange(0,
					m_mixerChannelViews.size()-1,1);
			}
		}
	}
}


void MixerView::saveSettings(QDomDocument& doc, QDomElement& domElement)
{
	MainWindow::saveWidgetState(this, domElement);
}


void MixerView::loadSettings(const QDomElement& domElement)
{
	MainWindow::restoreWidgetState(this, domElement);
}


void MixerView::toggledSolo()
{
	// 0.5.0 L08: the mixer applies the solo itself (Mixer::createChannel connects it); the view repaints.
	updateAllMixerChannels();
}


void MixerView::toggledMute()
{
	updateAllMixerChannels();
}

Mixer* MixerView::getMixer() const
{
	return m_mixer;
}

void MixerView::updateAllMixerChannels()
{
	for (int i = 0; i < m_mixerChannelViews.size(); ++i)
	{
		m_mixerChannelViews[i]->update();
	}
}

void MixerView::connectToSoloAndMute(int channelIndex)
{
	auto * mixerChannel = getMixer()->mixerChannel(channelIndex);

	connect(&mixerChannel->m_muteModel, &BoolModel::dataChanged, this, &MixerView::toggledMute, Qt::DirectConnection);
	connect(&mixerChannel->m_soloModel, &BoolModel::dataChanged, this, &MixerView::toggledSolo, Qt::DirectConnection);
}

void MixerView::disconnectFromSoloAndMute(int channelIndex)
{
	// The mixer may be SHORTER than this list (see the note at the top of this
	// file): a channel it no longer has cannot be disconnected from, but the
	// CALLER still deletes the view - that half must always happen.
	if (channelIndex >= static_cast<int>(getMixer()->numChannels())) { return; }
	auto * mixerChannel = getMixer()->mixerChannel(channelIndex);

	disconnect(&mixerChannel->m_muteModel, &BoolModel::dataChanged, this, &MixerView::toggledMute);
	disconnect(&mixerChannel->m_soloModel, &BoolModel::dataChanged, this, &MixerView::toggledSolo);
}


MixerChannelView* MixerView::focusedChannelView() const
{
	// Keyboard focus moves between strips without a mouse press, so the selected strip may not be the focused one.
	for (QWidget* widget = QApplication::focusWidget(); widget != nullptr; widget = widget->parentWidget())
	{
		if (auto* strip = qobject_cast<MixerChannelView*>(widget); strip != nullptr && strip->parentWidget() != nullptr
			&& isAncestorOf(strip))
		{
			return strip;
		}
	}
	return m_currentMixerChannel;
}

void MixerView::setCurrentMixerChannel(MixerChannelView* channel)
{
	// select
	m_currentMixerChannel = channel;
	// Taken now, while the view's index is current; see m_selectedChannelId.
	MixerChannel* selected = channel->mixerChannel();
	m_selectedChannelId = selected != nullptr ? selected->id() : -1;
	m_racksLayout->setCurrentWidget(m_mixerChannelViews[channel->channelIndex()]->m_effectRackView);

	// set up send knob
	for (int i = 0; i < m_mixerChannelViews.size(); ++i)
	{
		updateMixerChannel(i);
	}
}


void MixerView::updateMixerChannel(int index)
{
	const auto mixer = getMixer();

	const auto currentIndex = m_currentMixerChannel->channelIndex();
	const auto thisLine = m_mixerChannelViews[index];
	mixerstripinfo::describeStrip(thisLine, mixer, index);

	const auto sendModelCurrentToThis = mixer->channelSendModel(currentIndex, index);
	if (sendModelCurrentToThis == nullptr)
	{
		thisLine->m_sendKnob->setVisible(false);
		thisLine->m_sendArrow->setVisible(false);
	}
	else
	{
		thisLine->m_sendKnob->setVisible(true);
		thisLine->m_sendKnob->setModel(sendModelCurrentToThis);
		thisLine->m_sendArrow->setVisible(true);
	}

	const auto sendModelThisToCurrent = mixer->channelSendModel(index, currentIndex);
	if (sendModelThisToCurrent)
	{
		thisLine->m_receiveArrowOrSendButton->setVisible(true);
		thisLine->m_receiveArrowOrSendButton->setCurrentIndex(thisLine->m_receiveArrowStackedIndex);
	}
	else
	{
		// Board card #709: with the feedback submode on (Allow feedback sends) a send that closes a loop
		// is offered too; Mixer::createRoute flags it as a one-period feedback path.
		thisLine->m_receiveArrowOrSendButton->setVisible(mixer->feedbackMode() || !mixer->isInfiniteLoop(currentIndex, index));
		thisLine->m_receiveArrowOrSendButton->setCurrentIndex(thisLine->m_sendButtonStackedIndex);
	}

	thisLine->setSendsShown(mixerviewoptions::effective(mixerviewoptions::options()).sends);  // L08 (MX-003), theme 16
	thisLine->m_sendButton->updateLightStatus();
	thisLine->m_renameLineEdit->setText(thisLine->elideName(thisLine->mixerChannel()->m_name));
	thisLine->update();
}


void MixerView::deleteChannel(int index)
{
	// Can't delete master, and nothing is there where the MIXER has already removed
	// the channel: `index` is a VIEW index (see the note at the top of this file).
	if (index == 0 || index >= static_cast<int>(getMixer()->numChannels())) return;

	// Disconnect from the solo/mute models of the channel we are about to delete
	disconnectFromSoloAndMute(index);

	// remember selected line
	int selLine = m_currentMixerChannel->channelIndex();

	Mixer* mixer = getMixer();
	// in case the deleted channel is soloed or the remaining
	// channels will be left in a muted state
	mixer->clearChannel(index);

	// delete the real channel
	m_deletingOwnChannel = true;
	mixer->deleteChannel(index);
	m_deletingOwnChannel = false;

	chLayout->removeWidget(m_mixerChannelViews[index]);
	m_racksLayout->removeWidget(m_mixerChannelViews[index]);
	// delete MixerChannelView later to prevent a crash when deleting from context menu
	m_mixerChannelViews[index]->hide();
	m_mixerChannelViews[index]->deleteLater();
	m_channelAreaWidget->adjustSize();

	// make sure every channel knows what index it is
	for (int i = index + 1; i < m_mixerChannelViews.size(); ++i)
	{
		m_mixerChannelViews[i]->setChannelIndex(i - 1);
	}
	m_mixerChannelViews.remove(index);

	// the selection follows its channel: a channel deleted in front of it moved it down one
	if (selLine > index) { --selLine; }

	// select the next channel
	if (selLine >= m_mixerChannelViews.size())
	{
		selLine = m_mixerChannelViews.size() - 1;
	}
	setCurrentMixerChannel(selLine);

	// the views were renumbered with the channels: the next sync pass has nothing to rebuild
	m_channelOrderSignature = channelOrderSignatureOf(getMixer());
	updateMaxChannelSelector();
}

void MixerView::deleteUnusedChannels()
{
	Mixer* mix = getMixer();

	// Check all channels except master, delete those with no incoming sends
	for (int i = m_mixerChannelViews.size() - 1; i > 0 && i < static_cast<int>(mix->numChannels()); --i)
	{
		if (!mix->isChannelInUse(i))
		{
			deleteChannel(i);
		}
	}
}

void MixerView::moveChannelLeft(int index)
{
	// can't move master or first channel left or last channel right
	if (index <= 1 || index >= m_mixerChannelViews.size()) { return; }

	m_mixer->moveChannelLeft(index);

	const auto layoutIndex = chLayout->indexOf(m_mixerChannelViews[index]);
	assert(layoutIndex >= 1);

	chLayout->removeWidget(m_mixerChannelViews[index]);
	chLayout->insertWidget(layoutIndex - 1, m_mixerChannelViews[index]);

	m_mixerChannelViews[index]->setChannelIndex(index - 1);
	m_mixerChannelViews[index - 1]->setChannelIndex(index);
	std::swap(m_mixerChannelViews[index - 1], m_mixerChannelViews[index]);
	// the strips moved with the channels, so the next sync pass has nothing to rebuild
	m_channelOrderSignature = channelOrderSignatureOf(getMixer());
}


void MixerView::moveChannelRight(int index)
{
	moveChannelLeft(index + 1);
}


void MixerView::renameChannel(int index)
{
	m_mixerChannelViews[index]->renameChannel();
}


void MixerView::populatePeakWatchMenu(QMenu* menu) const
{
	menu->clear();
	menu->setObjectName(QStringLiteral("peakWatchMenu"));
	menu->setAccessibleName(tr("Peak watch"));
	const bool enabled = control::meterPeakWatchEnabled();
	const double threshold = control::meterPeakWatchThresholdDbfs();
	QAction* off = menu->addAction(tr("Off"));
	off->setObjectName(QStringLiteral("peakWatchOff"));
	off->setCheckable(true);
	off->setChecked(!enabled);
	connect(off, &QAction::triggered, menu, [menu] {
		runCommand(menu->parentWidget(), CommandCall{QStringLiteral("meter.set_peak_watch"),
			{{QStringLiteral("enabled"), false}}, {}, QString(), false});
	});
	menu->addSeparator();
	for (const int db : {-18, -12, -6, -3, 0})
	{
		QAction* action = menu->addAction(tr("%1 dBFS").arg(db));
		action->setObjectName(QStringLiteral("peakWatch%1").arg(db < 0 ? QStringLiteral("Minus%1").arg(-db) : QStringLiteral("Zero")));
		action->setCheckable(true);
		action->setChecked(enabled && qFuzzyCompare(threshold, static_cast<double>(db)));
		connect(action, &QAction::triggered, menu, [menu, db] {
			runCommand(menu->parentWidget(), CommandCall{QStringLiteral("meter.set_peak_watch"),
				{{QStringLiteral("enabled"), true}, {QStringLiteral("threshold_dbfs"), db}}, {}, QString(), false});
		});
	}
}

void MixerView::keyPressEvent(QKeyEvent * e)
{
	auto adjustCurrentFader = [this](const Qt::KeyboardModifiers& modifiers, Fader::AdjustmentDirection direction)
	{
		if (Fader* fader = keyboardFader(currentMixerChannel()))
		{
			fader->adjust(modifiers, direction);
		}
	};

	switch(e->key())
	{
		case Qt::Key_Delete:
			deleteChannel(m_currentMixerChannel->channelIndex());
			break;
		case Qt::Key_Left:
			if (e->modifiers() & Qt::AltModifier)
			{
				moveChannelLeft(m_currentMixerChannel->channelIndex());
			}
			else
			{
				// select channel to the left
				setCurrentMixerChannel(m_currentMixerChannel->channelIndex() - 1);
			}
			break;
		case Qt::Key_Right:
			if (e->modifiers() & Qt::AltModifier)
			{
				moveChannelRight(m_currentMixerChannel->channelIndex());
			}
			else
			{
				// select channel to the right
				setCurrentMixerChannel(m_currentMixerChannel->channelIndex() + 1);
			}
			break;
		case Qt::Key_Up:
		case Qt::Key_Plus:
			adjustCurrentFader(e->modifiers(), Fader::AdjustmentDirection::Up);
			break;
		case Qt::Key_Down:
		case Qt::Key_Minus:
			adjustCurrentFader(e->modifiers(), Fader::AdjustmentDirection::Down);
			break;
		case Qt::Key_Insert:
			if (e->modifiers() & Qt::ShiftModifier)
			{
				addNewChannel();
			}
			break;
		case Qt::Key_Enter:
		case Qt::Key_Return:
		case Qt::Key_F2:
			renameChannel(m_currentMixerChannel->channelIndex());
			break;
		default:
			e->ignore();
			break;
	}
}


void MixerView::setCurrentMixerChannel(int channel)
{
	if (channel >= 0 && channel < m_mixerChannelViews.size())
	{
		setCurrentMixerChannel(m_mixerChannelViews[channel]);
	}
}


void MixerView::clear()
{
	for (auto i = m_mixerChannelViews.size() - 1; i > 0; --i) { deleteChannel(i); }
	getMixer()->clearChannel(0);

	m_mixerChannelViews[0]->reset();
	mixerviewoptions::clearSelection();  // 0.5.0 L08 (MX-005): the next project's channel ids start again

	refreshDisplay();
}


void MixerView::updateFaders()
{
	Mixer * m = getMixer();

	for (int i = 0; i < m_mixerChannelViews.size() && i < static_cast<int>(m->numChannels()); ++i)
	{
		// 0.5.0 L08 (MX-127 / MX-129): the RMS or pre-fader meter instead (MixerViewOptions.cpp).
		if (mixerviewoptions::feedMeter(m_mixerChannelViews[i]->m_fader, m->mixerChannel(i))) { continue; }
		const float opl = m_mixerChannelViews[i]->m_fader->getPeak_L();
		const float opr = m_mixerChannelViews[i]->m_fader->getPeak_R();
		const float fallOff = mixerviewoptions::meterFallOff();  // 0.5.0 L08 (MX-128): the chosen ballistics
		if (m->mixerChannel(i)->m_peakLeft >= opl/fallOff)
		{
			m_mixerChannelViews[i]->m_fader->setPeak_L(m->mixerChannel(i)->m_peakLeft);
			// Set to -1 so later we'll know if this value has been refreshed yet.
			m->mixerChannel(i)->m_peakLeft = -1;
		}
		else if (m->mixerChannel(i)->m_peakLeft != -1)
		{
			m_mixerChannelViews[i]->m_fader->setPeak_L(opl/fallOff);
		}

		if (m->mixerChannel(i)->m_peakRight >= opr/fallOff)
		{
			m_mixerChannelViews[i]->m_fader->setPeak_R(m->mixerChannel(i)->m_peakRight);
			// Set to -1 so later we'll know if this value has been refreshed yet.
			m->mixerChannel(i)->m_peakRight = -1;
		}
		else if (m->mixerChannel(i)->m_peakRight != -1)
		{
			m_mixerChannelViews[i]->m_fader->setPeak_R(opr/fallOff);
		}
	}
}


} // namespace lmms::gui
