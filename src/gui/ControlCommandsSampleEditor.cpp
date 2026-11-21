/*
 * ControlCommandsSampleEditor.cpp - window.sample_editor: the sample editor's range and view, over the socket
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "MainWindow.h"
#include "SampleClip.h"
#include "SampleEditorDialog.h"

namespace lmms
{

using namespace control;

namespace
{

const QStringList kZooms{QStringLiteral("in"), QStringLiteral("out"), QStringLiteral("selection"), QStringLiteral("all")};

void applyZoom(gui::SampleSelectionView* view, const QString& zoom)
{
	switch (kZooms.indexOf(zoom))
	{
	case 0: view->zoomBy(2.0, view->width() / 2); break;
	case 1: view->zoomBy(0.5, view->width() / 2); break;
	case 2: view->zoomToSelection(); break;
	case 3: view->showAll(); break;
	default: break;
	}
}

ControlResult sampleEditor(const QJsonObject& args)
{
	gui::MainWindow* window = controlMainWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("window.sample_editor")); }
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	if (clip == nullptr || clip->sample().data() == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clip' is an audio clip with audio"));
	}
	gui::SampleEditorDialog* dialog = gui::SampleEditorDialog::openFor(clip, window);
	const int unit = gui::samplerange::unitNames().indexOf(args.value(QStringLiteral("unit")).toString());
	if (unit >= 0) { dialog->setUnit(static_cast<gui::samplerange::Unit>(unit)); }
	if (args.contains(QStringLiteral("from")) || args.contains(QStringLiteral("to")))
	{
		dialog->setRange(args.value(QStringLiteral("from")).toInt(dialog->view()->from()),
			args.value(QStringLiteral("to")).toInt(dialog->view()->to()));
	}
	applyZoom(dialog->view(), args.value(QStringLiteral("zoom")).toString());
	return ControlResult::success(dialog->describe());
}

} // namespace

void registerSampleEditorWindowCommands(ControlRegistry& registry)
{
	ControlCommand cmd = guiCommand("window", "sample_editor");
	cmd.description = QStringLiteral("Open the Sample Editor on an audio clip (what the clip menu's Sample Editor... does), or bring "
		"its open one forward, and set its VIEW: `from` and `to` select the source frames [from, to) - End exclusive, as "
		"sample.edit_range takes them, clamped to the source and swapped if reversed; `unit` is what its Start, End and "
		"Length fields read in (time, bars_beats - at the song's tempo from the clip's start - or samples); `zoom` zooms the "
		"waveform in or out, to the selection, or to all of it. The reply is the editor's state, the range also as the "
		"fields show it. Interface state, not project state: nothing is journalled - sample.edit_range edits the audio. "
		"Refused `requires` in a process with no interface.");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("from"), integerProperty(0, 2147483647)}, {QStringLiteral("to"), integerProperty(0, 2147483647)},
		{QStringLiteral("unit"), enumProperty(gui::samplerange::unitNames())}, {QStringLiteral("zoom"), enumProperty(kZooms)}},
		QJsonArray{QStringLiteral("clip")});
	cmd.resultSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()}, {QStringLiteral("channels"), integerProperty()},
		{QStringLiteral("layout"), stringProperty()}, {QStringLiteral("from"), integerProperty()},
		{QStringLiteral("to"), integerProperty()}, {QStringLiteral("length"), integerProperty()},
		{QStringLiteral("view_from"), integerProperty()}, {QStringLiteral("view_to"), integerProperty()},
		{QStringLiteral("unit"), stringProperty()}, {QStringLiteral("start_text"), stringProperty()},
		{QStringLiteral("end_text"), stringProperty()}, {QStringLiteral("length_text"), stringProperty()},
		{QStringLiteral("status"), stringProperty()}});
	cmd.handler = sampleEditor;
	registry.registerCommand(cmd);
}

} // namespace lmms
