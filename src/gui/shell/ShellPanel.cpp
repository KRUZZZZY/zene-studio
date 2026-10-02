/*
 * ShellPanel.cpp - one panel of the 0.5.0 shell: a header and the editor it frames
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

#include "ShellPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include "Accessibility.h"
#include "DesignTokens.h"
#include "ShellIcons.h"

namespace lmms::gui
{

ShellPanel::ShellPanel(const QString& id, const QString& title, QWidget* parent)
	: QWidget(parent)
	, m_id(id)
{
	setObjectName(QStringLiteral("shellPanel_") + id);
	a11y::announce(this, title, tr("The %1 panel.").arg(title));
	setAttribute(Qt::WA_StyledBackground, true);

	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	auto* header = new QWidget(this);
	header->setObjectName(QStringLiteral("shellPanelHeader"));
	header->setFixedHeight(tokens::size::panelHeader);
	auto* row = new QHBoxLayout(header);
	row->setContentsMargins(tokens::space::md, 0, tokens::space::xs, 0);
	row->setSpacing(tokens::space::sm);

	auto* glyph = new QLabel(header);
	glyph->setPixmap(shellicons::icon(id).pixmap(tokens::size::icon, tokens::size::icon));
	glyph->setFixedSize(tokens::size::icon, tokens::size::icon);
	row->addWidget(glyph);

	m_title = new QLabel(title.toUpper(), header);
	m_title->setFont(tokens::font(tokens::TextRole::Small));
	QPalette pal = m_title->palette();
	pal.setColor(QPalette::WindowText, tokens::palette().textMuted);
	m_title->setPalette(pal);
	row->addWidget(m_title);
	row->addStretch(1);

	m_tools = new QHBoxLayout();
	m_tools->setSpacing(tokens::space::xs);
	row->addLayout(m_tools);
	outer->addWidget(header);

	m_body = new QVBoxLayout();
	m_body->setContentsMargins(0, 0, 0, 0);
	outer->addLayout(m_body, 1);
}

QWidget* ShellPanel::setContent(QWidget* content)
{
	QWidget* previous = takeContent();
	m_content = content;
	if (content != nullptr)
	{
		content->setParent(this);
		content->setWindowFlags(Qt::Widget);
		m_body->addWidget(content);
		content->show();
	}
	return previous;
}

QWidget* ShellPanel::takeContent()
{
	QWidget* content = m_content.data();
	if (content == nullptr) { return nullptr; }
	m_body->removeWidget(content);
	content->hide();
	content->setParent(nullptr);
	m_content = nullptr;
	return content;
}

void ShellPanel::setFocusedPanel(bool focused)
{
	if (m_focused == focused) { return; }
	m_focused = focused;
	update();
}

void ShellPanel::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.panel);
	const QRect header(0, 0, width(), tokens::size::panelHeader);
	p.fillRect(header, pal.raised);
	p.fillRect(QRect(0, header.bottom(), width(), 1), pal.base);
	if (m_focused) { p.fillRect(QRect(0, header.bottom() - 1, width(), 2), pal.accent); }
}

} // namespace lmms::gui
