/*
 * TaskIndicator.cpp - the status bar's Tasks indicator and its list of background jobs
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

#include "TaskIndicator.h"

#include <algorithm>

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include "Accessibility.h"
#include "AccessibleWidgets.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "MainWindow.h"
#include "TaskCenter.h"
#include "TaskCommands.h"

namespace lmms::gui
{

namespace
{

constexpr int PollMs = 250;        //!< how often an open list re-reads the running jobs' progress
constexpr int ProgressSteps = 1000;
constexpr std::size_t MaxAnnounced = 64;

//! A job's state as a person reads it.
QString stateWord(TaskState state)
{
	switch (state)
	{
		case TaskState::Queued: return TaskList::tr("Queued");
		case TaskState::Running: return TaskList::tr("Running");
		case TaskState::Done: return TaskList::tr("Done");
		case TaskState::Failed: return TaskList::tr("Failed");
		case TaskState::Cancelled: return TaskList::tr("Cancelled");
	}
	return QString();
}

QLabel* textLabel(const QString& text, tokens::TextRole role, const QColor& colour, QWidget* parent)
{
	auto* label = new QLabel(text, parent);
	label->setFont(tokens::font(role));
	label->setStyleSheet(QStringLiteral("color: %1;").arg(colour.name()));
	return label;
}

void showProgress(QProgressBar* bar, const TaskInfo& task)
{
	if (task.state == TaskState::Running && task.progress < 0.0) { bar->setRange(0, 0); return; }  // busy: it cannot say
	bar->setRange(0, ProgressSteps);
	bar->setValue(static_cast<int>(std::max(task.progress, 0.0) * ProgressSteps));
}

ControlResult invoke(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

TaskList::TaskList(QWidget* parent) :
	QFrame(parent, Qt::Popup),
	m_rows(new QVBoxLayout),
	m_empty(textLabel(tr("No background jobs. Renders from the render queue, stem splits, mastering runs, library "
		"indexing and video audio extraction show here while they run."), tokens::TextRole::Small,
		tokens::palette().textMuted, this)),
	m_clear(new QPushButton(tr("Clear finished"), this)),
	m_poll(new QTimer(this))
{
	setObjectName(QStringLiteral("zeneTaskList"));
	setAccessibleName(tr("Background tasks"));
	const tokens::Palette& pal = tokens::palette();
	setStyleSheet(QStringLiteral("#zeneTaskList { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
		.arg(pal.panel.name(), pal.separator.name()).arg(tokens::radius::md));
	setFixedWidth(tokens::size::browserWidth + 2 * tokens::space::xxl);
	m_empty->setWordWrap(true);
	m_clear->setMinimumHeight(tokens::targetSize());
	a11y::announce(m_clear, tr("Clear finished"), tr("Remove the ended jobs from this list"));

	auto* header = new QHBoxLayout;
	header->addWidget(textLabel(tr("Tasks"), tokens::TextRole::Title, pal.text, this));
	header->addStretch(1);
	header->addWidget(m_clear);
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(tokens::space::md, tokens::space::md, tokens::space::md, tokens::space::md);
	layout->setSpacing(tokens::space::sm);
	layout->addLayout(header);
	layout->addWidget(m_empty);
	layout->addLayout(m_rows);

	m_poll->setInterval(PollMs);
	connect(m_poll, &QTimer::timeout, this, [this] {
		if (!isVisible()) { m_poll->stop(); return; }
		refresh();
	});
	connect(m_clear, &QPushButton::clicked, this, [] { invoke(QStringLiteral("task.clear_finished")); });
	// Queued: a change can come from inside a row's own Cancel click, and a rebuild must not delete that button under it.
	connect(&TaskCenter::instance(), &TaskCenter::changed, this, [this] {
		if (isVisible()) { QMetaObject::invokeMethod(this, &TaskList::refresh, Qt::QueuedConnection); }
	});
}

int TaskList::rowCount() const
{
	return static_cast<int>(m_live.size());
}

QPushButton* TaskList::cancelButton(int row) const
{
	return row >= 0 && row < rowCount() ? m_live[static_cast<std::size_t>(row)].cancel : nullptr;
}

void TaskList::clearRows()
{
	while (QLayoutItem* item = m_rows->takeAt(0))
	{
		if (QWidget* row = item->widget())
		{
			row->hide();
			row->deleteLater();  // never under a signal it is still emitting
		}
		delete item;
	}
	m_live.clear();
}

QWidget* TaskList::makeRow(const TaskInfo& task, Row* live)
{
	const tokens::Palette& pal = tokens::palette();
	auto* row = new QWidget(this);
	row->setAccessibleName(QStringLiteral("%1, %2").arg(task.title, stateWord(task.state)));
	auto* top = new QHBoxLayout;
	top->addWidget(textLabel(task.title, tokens::TextRole::Body, pal.text, row), 1);
	top->addWidget(textLabel(stateWord(task.state), tokens::TextRole::Small, pal.textMuted, row));
	auto* layout = new QVBoxLayout(row);
	layout->setContentsMargins(0, tokens::space::xs, 0, tokens::space::xs);
	layout->setSpacing(tokens::space::xxs);
	layout->addLayout(top);
	*live = Row{task.id, task.state, task.status, nullptr, nullptr};
	if (task.active())
	{
		live->bar = new QProgressBar(row);
		live->bar->setTextVisible(false);
		live->bar->setFixedHeight(tokens::space::sm);
		live->bar->setAccessibleName(tr("%1 progress").arg(task.title));
		showProgress(live->bar, task);
		layout->addWidget(live->bar);
	}
	if (!task.status.isEmpty())
	{
		QLabel* status = textLabel(task.status, tokens::TextRole::Small, task.state == TaskState::Failed ? pal.warning : pal.textMuted, row);
		status->setWordWrap(true);
		layout->addWidget(status);
	}
	if (task.cancellable && task.active())
	{
		live->cancel = new QPushButton(tr("Cancel"), row);
		live->cancel->setMinimumHeight(tokens::targetSize());
		a11y::announce(live->cancel, tr("Cancel %1").arg(task.title));
		connect(live->cancel, &QPushButton::clicked, this, [id = task.id] { invoke(QStringLiteral("task.cancel"), {{QStringLiteral("id"), id}}); });
		layout->addWidget(live->cancel, 0, Qt::AlignRight);
	}
	return row;
}

bool TaskList::updateInPlace(const std::vector<TaskInfo>& newestFirst)
{
	if (newestFirst.size() != m_live.size()) { return false; }
	for (std::size_t i = 0; i < m_live.size(); ++i)
	{
		const TaskInfo& task = newestFirst[i];
		const Row& row = m_live[i];
		if (task.id != row.id || task.state != row.state || task.status != row.status) { return false; }
	}
	for (std::size_t i = 0; i < m_live.size(); ++i)
	{
		if (m_live[i].bar != nullptr) { showProgress(m_live[i].bar, newestFirst[i]); }
	}
	return true;
}

void TaskList::rebuild(const std::vector<TaskInfo>& newestFirst)
{
	// A keyboard user's place survives a rebuild: the row of the focused Cancel keeps the focus.
	int focused = -1;
	for (int i = 0; i < rowCount(); ++i)
	{
		if (cancelButton(i) != nullptr && cancelButton(i) == focusWidget()) { focused = i; }
	}
	clearRows();
	m_live.reserve(newestFirst.size());
	for (const TaskInfo& task : newestFirst)
	{
		Row live;
		m_rows->addWidget(makeRow(task, &live));
		m_live.push_back(live);
	}
	if (QPushButton* again = cancelButton(focused)) { again->setFocus(Qt::OtherFocusReason); }
	adjustSize();
}

void TaskList::refresh()
{
	const std::vector<TaskInfo> all = TaskCenter::instance().tasks();
	const std::vector<TaskInfo> newestFirst(all.rbegin(), all.rend());
	if (!updateInPlace(newestFirst)) { rebuild(newestFirst); }
	const bool anyEnded = std::any_of(all.begin(), all.end(), [](const TaskInfo& t) { return !t.active(); });
	const bool anyRunning = std::any_of(all.begin(), all.end(), [](const TaskInfo& t) { return t.state == TaskState::Running; });
	m_empty->setVisible(all.empty());
	m_clear->setEnabled(anyEnded);
	if (anyRunning && isVisible() && !m_poll->isActive()) { m_poll->start(); }
}

void TaskList::mousePressEvent(QMouseEvent* event)
{
	// A press on the indicator closes the list and must not reach the indicator too, or its click would open it again.
	const QPoint global = event->globalPosition().toPoint();
	if (!rect().contains(event->position().toPoint()) && m_anchor != nullptr && m_anchor->rect().contains(m_anchor->mapFromGlobal(global)))
	{
		setAttribute(Qt::WA_NoMouseReplay);
	}
	QFrame::mousePressEvent(event);
}

void TaskList::popUpAt(QWidget* anchor)
{
	m_anchor = anchor;
	setAttribute(Qt::WA_NoMouseReplay, false);
	refresh();
	adjustSize();
	const QRect screen = anchor->screen() != nullptr ? anchor->screen()->availableGeometry() : QRect();
	QPoint at = anchor->isWindow()
		? anchor->mapToGlobal(QPoint(tokens::space::md, anchor->height() - height() - tokens::space::md))
		: anchor->mapToGlobal(QPoint(0, -height()));
	if (!screen.isNull())
	{
		if (at.y() < screen.top()) { at.setY(anchor->mapToGlobal(QPoint(0, anchor->height())).y()); }
		at.setX(std::clamp(at.x(), screen.left(), std::max(screen.left(), screen.right() - width())));
	}
	move(at);
	show();
	m_poll->start();
	if (QPushButton* first = cancelButton(0)) { first->setFocus(Qt::PopupFocusReason); }
	else { m_clear->setFocus(Qt::PopupFocusReason); }
}

TaskIndicator::TaskIndicator(QWidget* parent) :
	QToolButton(parent),
	m_list(new TaskList(this))
{
	setObjectName(QStringLiteral("zeneTaskIndicator"));
	setFocusPolicy(Qt::TabFocus);
	setAttribute(Qt::WA_Hover, true);
	setCursor(Qt::PointingHandCursor);
	setToolTip(tr("Background jobs - renders, stem splits, mastering, indexing. Click to see them and cancel one."));
	a11y::announce(this, tr("Tasks"), toolTip());
	// The click is task.show, as an agent's would be (the agent-surface gate reads the declaration); a lone indicator
	// outside the main window opens its own list.
	setProperty("controlCommand", QStringLiteral("task.show"));
	connect(this, &QToolButton::clicked, this, [this] { if (!invoke(QStringLiteral("task.show")).ok) { setListShown(-1); } });
	connect(&TaskCenter::instance(), &TaskCenter::changed, this, &TaskIndicator::tasksChanged);
	for (const TaskInfo& task : TaskCenter::instance().tasks())  // what ended before this indicator existed is not news
	{
		if (!task.active()) { m_announced.push_back(task.id); }
	}
	tasksChanged();
}

QString TaskIndicator::label() const
{
	return m_active > 0 ? tr("TASKS %1").arg(m_active) : tr("TASKS");
}

QSize TaskIndicator::sizeHint() const
{
	const QFontMetrics fm(tokens::font(tokens::TextRole::Small));
	const int mark = m_active > 0 ? tokens::space::md + tokens::space::xs : 0;
	return {fm.horizontalAdvance(label()) + mark + 2 * tokens::space::md, tokens::targetSize()};
}

bool TaskIndicator::setListShown(int shown)
{
	const bool visible = m_list->isVisible();
	const bool wanted = shown < 0 ? !visible : shown == 1;
	if (wanted && !visible) { m_list->popUpAt(this); }
	else if (!wanted && visible) { m_list->hide(); }
	update();
	return m_list->isVisible();
}

double TaskIndicator::combinedProgress() const
{
	double sum = 0.0;
	int known = 0;
	for (const TaskInfo& task : TaskCenter::instance().tasks())
	{
		if (task.state != TaskState::Running || task.progress < 0.0) { continue; }
		sum += task.progress;
		++known;
	}
	return known > 0 ? sum / known : -1.0;
}

void TaskIndicator::tasksChanged()
{
	m_active = TaskCenter::instance().activeCount();
	setAccessibleDescription(tr("%n background job(s) running", "", m_active));
	updateGeometry();
	update();
	announceEnded();
}

void TaskIndicator::announceEnded()
{
	for (const TaskInfo& task : TaskCenter::instance().tasks())
	{
		if (task.active() || std::find(m_announced.begin(), m_announced.end(), task.id) != m_announced.end()) { continue; }
		m_announced.push_back(task.id);
		const QString message = tr("%1: %2").arg(task.title, stateWord(task.state).toLower());
		a11y::announceStatus(this, message, task.state == TaskState::Failed ? a11y::Politeness::Assertive : a11y::Politeness::Polite);
	}
	if (m_announced.size() > MaxAnnounced) { m_announced.erase(m_announced.begin(), m_announced.end() - MaxAnnounced); }
}

QColor TaskIndicator::fillColour() const
{
	const tokens::Palette& pal = tokens::palette();
	if (isDown() || (m_list != nullptr && m_list->isVisible())) { return pal.selected; }
	return underMouse() && isEnabled() ? pal.controlHover : QColor(Qt::transparent);
}

void TaskIndicator::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
	p.setPen(Qt::NoPen);
	p.setBrush(fillColour());
	p.drawRoundedRect(r, tokens::radius::md, tokens::radius::md);

	int textLeft = tokens::space::md;
	if (m_active > 0)  // a busy mark beside the count: the count, not the colour, says how many
	{
		p.setBrush(isEnabled() ? pal.accent : pal.textDisabled);
		p.drawEllipse(QPointF(textLeft + tokens::space::xs, height() / 2.0), tokens::space::xs - 1, tokens::space::xs - 1);
		textLeft += tokens::space::md + tokens::space::xs;
		const double progress = combinedProgress();
		p.setBrush(pal.accent);
		if (progress >= 0.0) { p.drawRect(QRectF(r.left(), r.bottom() - 2.0, r.width() * progress, 2.0)); }
	}
	p.setFont(tokens::font(tokens::TextRole::Small));
	p.setPen(!isEnabled() ? pal.textDisabled : m_active > 0 ? pal.text : pal.textMuted);
	p.drawText(QRect(textLeft, 0, width() - textLeft, height()), Qt::AlignVCenter | Qt::AlignLeft, label());
	if (!hasFocus()) { return; }
	p.setPen(QPen(pal.focusRing, tokens::focusRingWidth()));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(r, tokens::radius::md, tokens::radius::md);
}

} // namespace lmms::gui

namespace lmms
{

namespace
{

ControlResult showTasks(const QJsonObject& args)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("task.show")); }
	const int wanted = args.contains(QStringLiteral("shown")) ? (args.value(QStringLiteral("shown")).toBool() ? 1 : 0) : -1;
	bool shown = false;
	if (auto* indicator = window->findChild<gui::TaskIndicator*>())
	{
		shown = indicator->setListShown(wanted);
	}
	else
	{
		// A window without the status bar's indicator still shows the list, at its bottom-left corner.
		static QPointer<gui::TaskList> loose;
		if (loose.isNull()) { loose = new gui::TaskList(window); }
		const bool show = wanted < 0 ? !loose->isVisible() : wanted == 1;
		if (show) { loose->popUpAt(window); }
		else { loose->hide(); }
		shown = loose->isVisible();
	}
	return ControlResult::success({{QStringLiteral("shown"), shown}, {QStringLiteral("active"), TaskCenter::instance().activeCount()}});
}

} // namespace

void registerTaskWindowCommand(ControlRegistry& registry)
{
	ControlCommand cmd = guiCommand("task", "show");
	cmd.description = QStringLiteral("Show the list of background jobs - the status bar's Tasks list ('shown' false hides it; it "
		"toggles when left out): each job's title, state, progress and Cancel where the job supports it. It never blocks "
		"the window: Escape or a click elsewhere closes it. Interface state.");
	cmd.argsSchema = control::objectSchema({{QStringLiteral("shown"), control::booleanProperty()}});
	cmd.resultSchema = control::objectProperty();
	cmd.handler = showTasks;
	registry.registerCommand(cmd);
}

} // namespace lmms
