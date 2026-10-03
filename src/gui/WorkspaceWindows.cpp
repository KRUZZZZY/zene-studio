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

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "HelpCenter.h"
#include "Song.h"

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
	for (const QJsonValue& value : whatsNew.value(QStringLiteral("all")).toArray())
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

} // namespace lmms::gui
