/*
 * ReferenceProject.h - a second project open read-only beside the session (WF-042)
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

#ifndef LMMS_REFERENCE_PROJECT_H
#define LMMS_REFERENCE_PROJECT_H

#include <memory>

#include <QDateTime>
#include <QDomElement>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class DataFile;

/*! 0.5.0 F3 (WF-042): several projects open at once, the honest way. The engine and the song are
 *  singletons - one project plays - so the second project is a REFERENCE: its file parsed into a
 *  document (DataFile, the loader's own reader, upgrades included), browsed in a panel and over the
 *  socket, never played or edited. Its tracks, clips and devices are copied into the session through
 *  the paths a paste already uses (ControlProjectTransplant.h), each copy one undo step of the session.
 *  It is interface state: not saved with either project, and it stays open across project.open and
 *  project.switch - which is how a project left by a switch becomes the reference to copy back from.
 *  UI-thread state. */
class LMMS_EXPORT ReferenceProject
{
public:
	static ReferenceProject& instance();

	//! Parse the project file at @a path (.mmp / .mmpz) as the reference, replacing any other; false and
	//! @a why (when not nullptr) when it is not a project with song tracks.
	bool open(const QString& path, QString* why);
	void close();
	bool isOpen() const { return m_file != nullptr; }
	QString path() const { return m_path; }
	//! The folder its relative media references resolve against.
	QString sourceDir() const;
	//! True when the file has been written since it was parsed (the copies still read what was parsed).
	bool stale() const;

	//! The reference's song tracks, in its order.
	QList<QDomElement> tracks() const;
	//! The project's <mixer> element (null while none is open): copied tracks bring their channels from it.
	QDomElement mixer() const;
	//! Its mixer channels (<mixerchannel>), master first.
	QList<QDomElement> channels() const;
	//! The clips of a reference @a track element, in its order.
	static QList<QDomElement> clipsOf(const QDomElement& track);
	//! The devices of a reference track or mixer channel element (its <fxchain>'s <effect>s), in order.
	static QList<QDomElement> effectsOf(const QDomElement& owner);

	//! What reference.get_state answers: the file, and each track (with its clips and devices) and channel.
	QJsonObject listing() const;

	/*! Called after every open and close, so the panel can refresh. One hook; nullptr (the default, and in
	 *  a core test) calls nothing. */
	using ChangeHook = void (*)();
	static void setChangeHook(ChangeHook hook);

private:
	ReferenceProject();
	~ReferenceProject();  // out of line: DataFile is incomplete here (MSVC C2027)
	std::unique_ptr<DataFile> m_file;
	QString m_path;
	QDateTime m_parsedAt;
};

} // namespace lmms

#endif // LMMS_REFERENCE_PROJECT_H
