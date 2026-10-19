/*
 * ExportFilter.h - declaration of class ExportFilter, the base-class for all
 *                  file export filters
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_EXPORT_FILTER_H
#define LMMS_EXPORT_FILTER_H

#include <QFile>

#include "TrackContainer.h"
#include "Plugin.h"


namespace lmms
{

/*! What an export did with the file it was asked to write.
 *
 *  The caller of an export has to know whether the file at the output path is ITS to remove:
 *  a path that was already taken was never created by the export and must be left exactly as
 *  it was, while a path the export created may be removed when the export did not finish. A
 *  filter therefore SAYS which of the two happened, instead of leaving the caller to guess
 *  from the file's presence on disk - a guess that is wrong exactly when it matters. */
enum class ExportOutcome
{
	//! The output path was NOT created by this call: it was already taken, or the filter
	//! could not be loaded at all. Nothing at that path belongs to the caller.
	NotCreated,
	//! This call created the output path but did not write the whole export into it: the
	//! file holds a partial export, and it belongs to the caller.
	Incomplete,
	//! This call created the output path and wrote the whole export into it.
	Written,
};


class LMMS_EXPORT ExportFilter : public Plugin
{
public:
	ExportFilter( const Descriptor * _descriptor ) : Plugin( _descriptor, nullptr ) {}
	~ExportFilter() override = default;


	/*! Write the export to \a filename and report what it did with that path.
	 *
	 *  The output is created EXCLUSIVELY: a path that is already there belongs to someone
	 *  else and is never written to. The outcome says whether THIS call created the file,
	 *  so the caller knows what it may remove. */
	virtual ExportOutcome tryExport(const TrackContainer::TrackList &tracks,
				const TrackContainer::TrackList &patternTracks,
				int tempo, int masterPitch, const QString &filename ) = 0;
protected:

	void saveSettings( QDomDocument &, QDomElement & ) override
	{
	}

	void loadSettings( const QDomElement & ) override
	{
	}

	QString nodeName() const override
	{
		return "import_filter";
	}


private:

} ;


} // namespace lmms

#endif // LMMS_EXPORT_FILTER_H
