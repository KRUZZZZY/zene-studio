/*
 * UpdateCheckWindows.cpp - the update question and notice, and starting the daily check
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

#include "UpdateCheckWindows.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "MainWindow.h"
#include "UpdateCheck.h"

namespace lmms::gui
{

using updatecheck::Outcome;
using updatecheck::UpdateChecker;

namespace
{

constexpr int HourMs = 60 * 60 * 1000;

//! Non-modal, and told apart by object name: there is one of each at a time, shown again when asked for again.
QDialog* openWindow(QPointer<QDialog>& held, QWidget* parent, const char* name, const QString& title)
{
	if (held.isNull())
	{
		held = new QDialog(parent);
		held->setObjectName(QLatin1String(name));
		held->setWindowTitle(title);
		held->setAttribute(Qt::WA_DeleteOnClose);
	}
	return held;
}

QPointer<QDialog>& noticeWindow()
{
	static QPointer<QDialog> window;
	return window;
}

//! The notice in its one layout, filled in by showNotice. @a window gets the widgets once.
void buildNotice(QDialog* window)
{
	window->resize(520, 420);
	auto* heading = new QLabel(window);
	heading->setObjectName(QStringLiteral("updateNoticeHeading"));
	heading->setWordWrap(true);
	heading->setTextFormat(Qt::PlainText);
	auto* notes = new QTextBrowser(window);
	notes->setObjectName(QStringLiteral("updateNoticeNotes"));
	notes->setAccessibleName(QObject::tr("Release notes"));
	notes->setOpenLinks(false);
	auto* download = new QPushButton(QObject::tr("Open Download Page"), window);
	download->setObjectName(QStringLiteral("updateNoticeDownload"));
	QObject::connect(download, &QPushButton::clicked, window, [] {
		ControlRegistry::instance()->invoke(QStringLiteral("window.update_download_page"));
	});
	auto* close = new QPushButton(QObject::tr("Close"), window);
	QObject::connect(close, &QPushButton::clicked, window, &QDialog::close);
	auto* buttons = new QHBoxLayout;
	buttons->addStretch(1);
	buttons->addWidget(download);
	buttons->addWidget(close);
	auto* layout = new QVBoxLayout(window);
	layout->addWidget(heading);
	layout->addWidget(notes, 1);
	layout->addLayout(buttons);
}

void showNotice(QWidget* parent, const QString& heading, const QString& notes, bool hasDownload)
{
	QPointer<QDialog>& held = noticeWindow();
	const bool isNew = held.isNull();
	QDialog* window = openWindow(held, parent, "updateNoticeDialog", QObject::tr("Check for Updates"));
	if (isNew) { buildNotice(window); }
	window->findChild<QLabel*>(QStringLiteral("updateNoticeHeading"))->setText(heading);
	auto* browser = window->findChild<QTextBrowser*>(QStringLiteral("updateNoticeNotes"));
	browser->setPlainText(notes);  // the feed's text is shown as text: its markup can run nothing and open nothing
	browser->setVisible(!notes.isEmpty());
	window->findChild<QPushButton*>(QStringLiteral("updateNoticeDownload"))->setVisible(hasDownload);
	window->show();
	window->raise();
}

void showOutcome(const Outcome& outcome)
{
	MainWindow* window = controlMainWindow();
	if (window == nullptr) { return; }
	if (outcome.state == Outcome::State::Newer)
	{
		// An automatic check announces a release once; asking gets the answer every time.
		if (!outcome.manual && outcome.release.version == updatecheck::noticedVersion()) { return; }
		updatecheck::markNoticed(outcome.release.version);
		const QString title = outcome.release.title.isEmpty() ? QString() : QStringLiteral(" - ") + outcome.release.title;
		showNotice(window, QObject::tr("Zene Studio %1 is available%2. You have %3. Installing it is up to you: the button "
			"opens its download page.").arg(outcome.release.version, title, UpdateChecker::instance()->currentVersion()),
			outcome.release.notes, true);
	}
	else if (outcome.manual && outcome.state == Outcome::State::UpToDate)
	{
		showNotice(window, QObject::tr("Zene Studio is up to date (version %1).").arg(UpdateChecker::instance()->currentVersion()), {}, false);
	}
	else if (outcome.manual && outcome.state == Outcome::State::Failed)
	{
		showNotice(window, QObject::tr("Could not check for updates: %1").arg(outcome.reason), {}, false);
	}
}

void listenForOutcomes()
{
	UpdateChecker::instance()->setListener(showOutcome);
}

//! The question. Yes turns checks on through the registered command, No or closing it records "off" through the same.
void showConsent(QWidget* parent, bool checkAfterwards)
{
	static QPointer<QDialog> held;
	if (!held.isNull())
	{
		held->raise();
		return;
	}
	QDialog* window = openWindow(held, parent, "updateConsentDialog", QObject::tr("Check for Updates"));
	auto* text = new QLabel(QObject::tr("May Zene Studio check once a day whether a newer version exists?\n\n"
		"It asks the release feed (%1) with one request that names only this program's version and platform: %2. "
		"The feed's server also sees your IP address, as with any web request. Nothing is downloaded or installed; "
		"a new version is shown with a button to its download page. You can change this in Settings at any time.")
		.arg(updatecheck::feedUrl(), QString::fromUtf8(updatecheck::userAgent())), window);
	text->setWordWrap(true);
	text->setTextFormat(Qt::PlainText);
	auto* prereleases = new QCheckBox(QObject::tr("Include pre-releases"), window);
	prereleases->setObjectName(QStringLiteral("updateConsentPrereleases"));
	prereleases->setChecked(updatecheck::preferences().prereleases);
	auto* buttons = new QDialogButtonBox(window);
	auto* yes = buttons->addButton(QObject::tr("Check for Updates"), QDialogButtonBox::AcceptRole);
	yes->setObjectName(QStringLiteral("updateConsentYes"));
	auto* no = buttons->addButton(QObject::tr("Do Not Check"), QDialogButtonBox::RejectRole);
	no->setObjectName(QStringLiteral("updateConsentNo"));
	no->setDefault(true);
	QObject::connect(buttons, &QDialogButtonBox::accepted, window, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, window, &QDialog::reject);
	QObject::connect(window, &QDialog::accepted, window, [prereleases, checkAfterwards] {
		ControlRegistry::instance()->invoke(QStringLiteral("app.update_check_set"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("prereleases"), prereleases->isChecked()}});
		if (checkAfterwards) { ControlRegistry::instance()->invoke(QStringLiteral("window.update_check")); }
		else { UpdateChecker::instance()->checkIfDue(); }
	});
	QObject::connect(window, &QDialog::rejected, window, [] {
		ControlRegistry::instance()->invoke(QStringLiteral("app.update_check_set"), {{QStringLiteral("enabled"), false}});
	});
	auto* layout = new QVBoxLayout(window);
	layout->addWidget(text);
	layout->addWidget(prereleases);
	layout->addWidget(buttons);
	window->show();
}

} // namespace

void addUpdateCheckMenuEntry(QMenu* help)
{
	if (!ControlRegistry::instance()->hasCommand(QStringLiteral("window.update_check"))) { return; }
	addCommandAction(help, QObject::tr("Check for Updates"), QStringLiteral("window.update_check"), [] {
		return CommandCall{QStringLiteral("window.update_check"), {}, {}, QString(), false};
	});
}

void runUpdateCheckStartup(QWidget* window)
{
	if (!updatecheck::managedBy().isEmpty()) { return; }
	listenForOutcomes();
	if (!updatecheck::preferences().answered && !updatecheck::consentAsked())
	{
		updatecheck::markConsentAsked();
		showConsent(window, false);
	}
	else { UpdateChecker::instance()->checkIfDue(); }
	auto* hourly = new QTimer(window);
	QObject::connect(hourly, &QTimer::timeout, window, [] { UpdateChecker::instance()->checkIfDue(); });
	hourly->start(HourMs);
}

} // namespace lmms::gui

namespace lmms
{

namespace
{

ControlResult checkWindow(const QJsonObject&)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.update_check")); }
	gui::listenForOutcomes();
	if (!updatecheck::preferences().enabled)
	{
		// Being asked is not being allowed: the question comes first, and no request goes out until it is answered yes.
		gui::showConsent(window, true);
		return ControlResult::success({{QStringLiteral("shown"), QStringLiteral("question")}, {QStringLiteral("started"), false}});
	}
	const auto started = updatecheck::UpdateChecker::instance()->check(true);
	gui::showNotice(window, QObject::tr("Checking for updates..."), {}, false);
	return ControlResult::success({{QStringLiteral("shown"), QStringLiteral("notice")},
		{QStringLiteral("started"), started == updatecheck::UpdateChecker::Start::Started}});
}

ControlResult downloadPage(const QJsonObject&)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.update_download_page")); }
	const QString url = updatecheck::UpdateChecker::instance()->lastOutcome().release.url;
	if (url.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no check this session found a newer release"));
	}
	const bool opened = QDesktopServices::openUrl(QUrl(url));
	if (!opened)
	{
		// As Online Help: nothing opening must not look like nothing happening, and the address can be opened by hand.
		window->statusBar()->showMessage(QObject::tr("Could not open %1 - no application on this system is registered to open it.").arg(url));
	}
	return ControlResult::success({{QStringLiteral("opened"), opened}, {QStringLiteral("url"), url}});
}

} // namespace

void registerUpdateCheckWindowCommands(ControlRegistry& registry)
{
	if (!updatecheck::managedBy().isEmpty()) { return; }
	control::defineCommandWithSchema(registry, QStringLiteral("window.update_check"), QStringLiteral("Help > Check for Updates: "
		"check the feed now and show the answer in a notice that blocks nothing (a newer release with its notes and a button to "
		"its download page, up to date, or why it could not tell). While update checks are off it asks first and makes no "
		"request unless the answer is yes."), control::objectSchema({}), false, checkWindow, false);
	control::defineCommandWithSchema(registry, QStringLiteral("window.update_download_page"), QStringLiteral("Open the download "
		"page of the newer release the last check found, in the system's browser ('opened', 'url'). Nothing is downloaded "
		"or installed by the program."), control::objectSchema({}), false, downloadPage, false);
}

} // namespace lmms
