/*
 * WorkspaceWindows.cpp - the 0.5.0 W3 small windows: ask, what's new, large clock
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

#include "WorkspaceWindows.h"

#include <cmath>
#include <utility>

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "HelpCenter.h"
#include "Song.h"
#include "TunerTap.h"

namespace lmms::gui
{

namespace
{

QString answerHtml(const QJsonObject& answer)
{
	const bool manual = answer.value(QStringLiteral("kind")).toString() == QLatin1String("manual");
	const QString id = answer.value(QStringLiteral("id")).toString();
	const QString link = (manual ? QStringLiteral("topic:") : QStringLiteral("command:")) + id;
	return QStringLiteral("<p><a href=\"%1\"><b>%2</b></a> <small>(%3)</small><br>%4</p>")
		.arg(link, answer.value(QStringLiteral("title")).toString().toHtmlEscaped(),
			manual ? AssistantDialog::tr("manual") : AssistantDialog::tr("command"),
			answer.value(QStringLiteral("text")).toString().toHtmlEscaped());
}

//! A manual link opens the manual there; a command link opens its reference entry.
void follow(const QUrl& url)
{
	const QString target = url.path();
	if (url.scheme() == QLatin1String("topic"))
	{
		ControlRegistry::instance()->invoke(QStringLiteral("app.help"), {{QStringLiteral("topic"), target}});
		return;
	}
	HelpCenter::showHelp(QString(), true)->filterReference(target);
}

//! The tuner source @a id ("input", "master") as its button names it.
QString sourceLabel(const QString& id)
{
	if (id == QLatin1String("input")) { return TunerWindow::tr("Input"); }
	return id == QLatin1String("master") ? TunerWindow::tr("Master") : id;
}

QLabel* bigLabel(int pixels, QWidget* parent)
{
	auto* label = new QLabel(parent);
	QFont face = tokens::font(tokens::TextRole::Display);
	face.setPixelSize(pixels);
	label->setFont(face);
	label->setAlignment(Qt::AlignCenter);
	return label;
}

} // namespace

AssistantDialog::AssistantDialog(QWidget* parent) :
	QDialog(parent),
	m_question(new QLineEdit(this)),
	m_answers(new QTextBrowser(this))
{
	setWindowTitle(tr("Ask Zene Studio"));
	resize(620, 480);
	m_question->setPlaceholderText(tr("Ask a question: how do I record a loop, what does freeze do..."));
	m_question->setAccessibleName(tr("Question"));
	m_answers->setAccessibleName(tr("Answers"));
	m_answers->setOpenLinks(false);
	auto* askButton = new QPushButton(tr("Ask"), this);
	askButton->setProperty("controlCommand", QStringLiteral("app.ask"));
	auto* row = new QHBoxLayout;
	row->addWidget(m_question, 1);
	row->addWidget(askButton);
	auto* note = new QLabel(tr("Answers come from the manual and the command reference shipped with the program. "
		"Nothing is sent anywhere."), this);
	note->setWordWrap(true);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(row);
	layout->addWidget(m_answers, 1);
	layout->addWidget(note);
	const auto go = [this] { ask(m_question->text()); };
	connect(m_question, &QLineEdit::returnPressed, this, go);
	connect(askButton, &QPushButton::clicked, this, go);
	connect(m_answers, &QTextBrowser::anchorClicked, this, &follow);
}

int AssistantDialog::ask(const QString& question)
{
	m_question->setText(question);
	const ControlResult answered = ControlRegistry::instance()->invoke(QStringLiteral("app.ask"), {{QStringLiteral("question"), question}});
	if (!answered.ok)
	{
		m_answers->setHtml(QStringLiteral("<p>%1</p>").arg(answered.errorMessage.toHtmlEscaped()));
		return 0;
	}
	const QJsonArray answers = answered.result.value(QStringLiteral("answers")).toArray();
	QString html;
	for (const QJsonValue& answer : answers) { html += answerHtml(answer.toObject()); }
	m_answers->setHtml(html.isEmpty() ? tr("<p>Nothing in the manual or the commands matches. Try other words.</p>") : html);
	return static_cast<int>(answers.size());
}

QString AssistantDialog::answersText() const
{
	return m_answers->toPlainText();
}

WhatsNewDialog::WhatsNewDialog(const QJsonObject& whatsNew, QWidget* parent) :
	QDialog(parent),
	m_notes(new QTextBrowser(this))
{
	setWindowTitle(tr("What's New"));
	resize(560, 460);
	m_notes->setAccessibleName(tr("Release notes"));
	m_notes->setOpenExternalLinks(false);
	QString html;
	// This version's notes, as window.whats_new promises (review W3-agent-custom #4).
	const QJsonObject thisVersion = whatsNew.value(QStringLiteral("notes")).toObject();
	for (const QJsonValue& value : thisVersion.isEmpty() ? QJsonArray{} : QJsonArray{thisVersion})
	{
		const QJsonObject version = value.toObject();
		html += QStringLiteral("<h2>%1</h2><ul>").arg(version.value(QStringLiteral("title")).toString().toHtmlEscaped());
		for (const QJsonValue& item : version.value(QStringLiteral("items")).toArray())
		{
			html += QStringLiteral("<li>%1</li>").arg(item.toString().toHtmlEscaped());
		}
		html += QStringLiteral("</ul>");
	}
	m_notes->setHtml(html);
	auto* close = new QPushButton(tr("Close"), this);
	connect(close, &QPushButton::clicked, this, &QDialog::close);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_notes, 1);
	layout->addWidget(close, 0, Qt::AlignRight);
}

QString WhatsNewDialog::notesText() const
{
	return m_notes->toPlainText();
}

LargeClock::LargeClock(QWidget* parent) :
	QDialog(parent),
	m_bars(bigLabel(96, this)),
	m_time(bigLabel(40, this)),
	m_timer(new QTimer(this))
{
	setWindowTitle(tr("Clock"));
	setAccessibleName(tr("Large clock"));
	m_bars->setAccessibleName(tr("Position in bars, beats and ticks"));
	m_time->setAccessibleName(tr("Position in minutes and seconds"));
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_bars, 1);
	layout->addWidget(m_time);
	resize(560, 240);
	connect(m_timer, &QTimer::timeout, this, [this] { refresh(); });
	m_timer->start(100);
	refresh();
}

QString LargeClock::refresh()
{
	const Song* song = Engine::getSong();
	const int milliseconds = song->getMilliseconds();
	m_bars->setText(QStringLiteral("%1 . %2 . %3").arg(song->getPlayPos().getBar() + 1).arg(song->getBeat() + 1)
		.arg(song->getBeatTicks(), 3, 10, QLatin1Char('0')));
	m_time->setText(QStringLiteral("%1:%2.%3").arg(milliseconds / 60000).arg((milliseconds / 1000) % 60, 2, 10, QLatin1Char('0'))
		.arg((milliseconds % 1000) / 10, 2, 10, QLatin1Char('0')));
	return m_bars->text() + QLatin1Char(' ') + m_time->text();
}

TunerWindow::TunerWindow(QWidget* parent) :
	QDialog(parent),
	m_note(bigLabel(96, this)),
	m_detail(new QLabel(this)),
	m_needle(new QSlider(Qt::Horizontal, this)),
	m_timer(new QTimer(this))
{
	setWindowTitle(tr("Tuner"));
	setAccessibleName(tr("Tuner"));
	m_note->setAccessibleName(tr("Nearest note"));
	m_detail->setAlignment(Qt::AlignCenter);
	m_detail->setAccessibleName(tr("Cents off and frequency"));
	m_needle->setRange(-50, 50);
	m_needle->setEnabled(false);
	m_needle->setAccessibleName(tr("Cents off the note"));
	auto* sources = new QHBoxLayout;
	for (const QString& source : {QStringLiteral("input"), QStringLiteral("master")})
	{
		auto* button = new QPushButton(sourceLabel(source), this);
		button->setProperty("controlCommand", QStringLiteral("tuner.set"));
		button->setProperty("tunerSource", source);
		// The source listened to shows pressed, not only in the status line. refresh() sets both buttons from
		// tuner.get_state (not auto-exclusive: with the tuner off neither is pressed).
		button->setCheckable(true);
		m_sources.append(button);
		connect(button, &QPushButton::clicked, this, [source = source] {
			ControlRegistry::instance()->invoke(QStringLiteral("tuner.set"), {{QStringLiteral("source"), source}});
		});
		sources->addWidget(button);
	}
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_note, 1);
	layout->addWidget(m_needle);
	layout->addWidget(m_detail);
	layout->addLayout(sources);
	resize(420, 300);
	if (TunerTap::instance().source() == TunerTap::Source::Off)
	{
		ControlRegistry::instance()->invoke(QStringLiteral("tuner.set"), {{QStringLiteral("source"), QStringLiteral("input")}});
	}
	connect(m_timer, &QTimer::timeout, this, [this] { refresh(); });
	m_timer->start(100);
	refresh();
}

TunerWindow::~TunerWindow()
{
	TunerTap::instance().setSource(TunerTap::Source::Off);  // nothing left to listen for
}

QString TunerWindow::refresh()
{
	const QJsonObject state = ControlRegistry::instance()->invoke(QStringLiteral("tuner.get_state")).result;
	const bool clear = state.value(QStringLiteral("clear")).toBool();
	const double cents = state.value(QStringLiteral("cents")).toDouble();
	m_note->setText(clear ? state.value(QStringLiteral("note")).toString() : QStringLiteral("-"));
	m_needle->setValue(clear ? static_cast<int>(std::lround(cents)) : 0);
	const QString source = state.value(QStringLiteral("source")).toString();
	for (QPushButton* button : m_sources) { button->setChecked(button->property("tunerSource").toString() == source); }
	m_detail->setText(clear ? tr("%1 cents   %2 Hz   (A4 = %3 Hz, %4)").arg(cents, 0, 'f', 1).arg(state.value(QStringLiteral("hz")).toDouble(), 0, 'f', 1)
		.arg(state.value(QStringLiteral("reference_hz")).toDouble()).arg(sourceLabel(source))
		: tr("Listening to %1 - play a single note").arg(sourceLabel(source)));
	return m_note->text();
}

} // namespace lmms::gui
