/*
 * CollabPanel.cpp - the small collaboration panel: host, invite, join, and who
 *                   is connected (0.5.0 F6, WF-050/052)
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

#include "CollabPanel.h"

#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include "CollabSession.h"
#include "GuiApplication.h"
#include "MainWindow.h"

namespace lmms::gui
{

namespace
{

QWidget* mainWindowWidget()
{
	GuiApplication* application = getGUI();
	return application != nullptr ? application->mainWindow() : nullptr;
}

QString peerLine(const QJsonObject& peer)
{
	return QStringLiteral("%1 - %2 - %3").arg(peer.value(QStringLiteral("name")).toString(),
		peer.value(QStringLiteral("role")).toString(),
		peer.value(QStringLiteral("connected")).toBool(true) ? CollabPanel::tr("connected") : CollabPanel::tr("disconnected"));
}

QPushButton* button(const QString& text, QWidget* parent)
{
	auto* created = new QPushButton(text, parent);
	created->setAutoDefault(false);
	return created;
}

} // namespace

bool collabConsentPrompt(const QString& title, const QString& text)
{
	return QMessageBox::question(mainWindowWidget(), title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
		== QMessageBox::Yes;
}

ControlResult showCollabPanel()
{
	static QPointer<CollabPanel> panel;
	QWidget* parent = mainWindowWidget();
	if (parent == nullptr) { return ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("the panel needs the main window")); }
	if (panel.isNull()) { panel = new CollabPanel(parent); }
	panel->show();
	panel->raise();
	panel->activateWindow();
	return ControlResult::success({{QStringLiteral("shown"), true}});
}

CollabPanel::CollabPanel(QWidget* parent) :
	QWidget(parent, Qt::Tool),
	m_status(new QLabel(this)),
	m_peers(new QListWidget(this)),
	m_role(new QComboBox(this)),
	m_invite(new QLineEdit(this)),
	m_host(button(tr("Host..."), this)),
	m_stop(button(tr("Stop hosting"), this)),
	m_copy(button(tr("Copy invite"), this)),
	m_copyLan(button(tr("Copy LAN invite"), this)),
	m_join(button(tr("Join"), this)),
	m_leave(button(tr("Leave"), this)),
	m_resync(button(tr("Re-sync"), this))
{
	setWindowTitle(tr("Collaborate"));
	setAccessibleName(tr("Collaboration panel"));
	m_status->setWordWrap(true);
	m_role->addItems({QStringLiteral("editor"), QStringLiteral("viewer"), QStringLiteral("remote")});
	m_role->setAccessibleName(tr("Role the invite grants"));
	m_invite->setPlaceholderText(tr("Paste an invite (ZENE1-...)"));
	m_invite->setAccessibleName(tr("Invite to join"));

	auto* hostRow = new QHBoxLayout;
	for (QWidget* w : std::initializer_list<QWidget*>{m_host, m_stop, m_role, m_copy, m_copyLan}) { hostRow->addWidget(w); }
	auto* joinRow = new QHBoxLayout;
	for (QWidget* w : std::initializer_list<QWidget*>{m_invite, m_join, m_leave, m_resync}) { joinRow->addWidget(w); }
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_status);
	layout->addWidget(m_peers);
	layout->addLayout(hostRow);
	layout->addLayout(joinRow);

	connect(m_host, &QPushButton::clicked, this, [this] { run(QStringLiteral("collab.host"), {{QStringLiteral("consent"), true}}); });
	connect(m_stop, &QPushButton::clicked, this, [this] { run(QStringLiteral("collab.stop"), {}); });
	connect(m_copy, &QPushButton::clicked, this, [this] { copyInvite(QStringLiteral("auto")); });
	connect(m_copyLan, &QPushButton::clicked, this, [this] { copyInvite(QStringLiteral("lan")); });
	connect(m_join, &QPushButton::clicked, this, [this] { run(QStringLiteral("collab.join"), {{QStringLiteral("invite"), m_invite->text()}}); });
	connect(m_leave, &QPushButton::clicked, this, [this] { run(QStringLiteral("collab.leave"), {}); });
	connect(m_resync, &QPushButton::clicked, this, [this] { run(QStringLiteral("collab.resync"), {}); });
	connect(collab::CollabSession::instance(), &collab::CollabSession::changed, this, &CollabPanel::refresh);
	refresh();
}

void CollabPanel::run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (!result.ok) { m_status->setText(result.errorMessage); return; }
	refresh();
}

void CollabPanel::copyInvite(const QString& scope)
{
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("collab.invite"),
		{{QStringLiteral("role"), m_role->currentText()}, {QStringLiteral("scope"), scope}});
	if (!result.ok) { m_status->setText(result.errorMessage); return; }
	QGuiApplication::clipboard()->setText(result.result.value(QStringLiteral("invite")).toString());
	const QString warning = result.result.value(QStringLiteral("warning")).toString();
	m_status->setText(tr("Invite copied (%1 at %2). It carries the session key: send it only to that person.%3")
		.arg(m_role->currentText(), result.result.value(QStringLiteral("address")).toString(),
			warning.isEmpty() ? QString() : QStringLiteral(" ") + warning));
}

void CollabPanel::fillPeers(const QJsonObject& state)
{
	m_peers->clear();
	const QJsonObject host = state.value(QStringLiteral("host")).toObject();
	const QJsonObject peer = state.value(QStringLiteral("peer")).toObject();
	if (!host.isEmpty()) { m_peers->addItem(tr("%1 - host (you)").arg(host.value(QStringLiteral("name")).toString())); }
	if (!peer.isEmpty()) { m_peers->addItem(tr("host at %1 - %2").arg(peer.value(QStringLiteral("host")).toString(), peer.value(QStringLiteral("state")).toString())); }
	const QJsonArray peers = host.isEmpty() ? peer.value(QStringLiteral("peers")).toArray() : host.value(QStringLiteral("peers")).toArray();
	for (const QJsonValue& entry : peers) { m_peers->addItem(peerLine(entry.toObject())); }
}

void CollabPanel::refresh()
{
	const QJsonObject state = collab::CollabSession::instance()->state();
	const QString mode = state.value(QStringLiteral("mode")).toString();
	if (state.value(QStringLiteral("peer")).toObject().value(QStringLiteral("state")).toString() == QLatin1String("joined"))
	{
		m_invite->clear();
	}
	const bool idle = mode == QLatin1String("idle");
	const bool hosting = mode == QLatin1String("hosting");
	m_host->setEnabled(idle);
	m_join->setEnabled(idle);
	m_invite->setEnabled(idle);
	m_stop->setEnabled(hosting);
	m_copy->setEnabled(hosting);
	m_copyLan->setEnabled(hosting);
	m_role->setEnabled(hosting);
	m_leave->setEnabled(mode == QLatin1String("joined"));
	m_resync->setEnabled(!idle);
	m_status->setText(tr("Collaboration is %1. %2").arg(mode, state.value(QStringLiteral("last_notice")).toString()));
	fillPeers(state);
}

} // namespace lmms::gui
