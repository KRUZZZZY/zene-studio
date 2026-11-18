/*
 * ShellIcons.cpp - the interface's one icon set (Lucide, ISC licence), rendered from SVG
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

#include "ShellIcons.h"

#include <map>

#include <QFile>
#include <QGuiApplication>
#include <QIconEngine>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSvgRenderer>

#include "DesignTokens.h"

namespace lmms::gui::shellicons
{

namespace
{

//! The shell's own icon ids -> Lucide icon names.
const std::map<QString, QString>& shellTable()
{
	static const std::map<QString, QString> table{
	{"arranger", "rows-3"},
	{"audition", "volume-2"},
	{"automation", "chart-spline"},
	{"browser", "library"},
	{"chevron", "chevron-down"},
	{"close", "x"},
	{"detail", "piano"},
	{"devices", "boxes"},
	{"draw", "pencil"},
	{"drums", "drum"},
	{"editor", "maximize-2"},
	{"erase", "eraser"},
	{"export", "upload"},
	{"float", "external-link"},
	{"fold", "fold-vertical"},
	{"follow", "scan"},
	{"ghost", "ghost"},
	{"inspector", "info"},
	{"launcher", "layout-grid"},
	{"list", "align-left"},  // theme 10: a canvas's editable item list (ItemList.h)
	{"loop", "repeat"},
	{"metronome", "timer"},
	{"mixer", "sliders-vertical"},
	{"modulator", "activity"},
	{"mute", "volume-x"},
	{"new", "file-plus"},
	{"open", "folder-open"},
	{"paint", "paintbrush"},
	{"pattern", "grid-3x3"},
	{"pins", "plug"},
	{"play", "play"},
	{"plus", "plus"},
	{"power", "power"},
	{"project_tools", "wrench"},
	{"quantize", "magnet"},
	{"record", "circle-dot"},
	{"save", "save"},
	{"scale", "music"},
	{"select", "mouse-pointer-2"},
	{"slice", "scissors"},
	{"stop", "square"}
	};
	return table;
}

//! The inherited interface's icon names (embed::getIconPixmap) -> Lucide icon names: every
//! bitmap icon the old interface drew is drawn from the one modern set instead.
const std::map<QString, QString>& legacyTable()
{
	static const std::map<QString, QString> table{
		{"add", "plus"},
		{"add_automation", "chart-spline"},
		{"add_folder", "folder-plus"},
		{"add_pattern_track", "grid-3x3"},
		{"add_sample_track", "audio-waveform"},
		{"analysis", "activity"},
		{"apply", "check"},
		{"apply-selected", "check-check"},
		{"arp_down", "arrow-down"},
		{"arp_down_on", "arrow-down"},
		{"arp_free", "shuffle"},
		{"arp_up", "arrow-up"},
		{"arp_up_on", "arrow-up"},
		{"auto_resize", "move-horizontal"},
		{"auto_resize_disable", "lock"},
		{"automation", "chart-spline"},
		{"automation_ghost_note", "ghost"},
		{"automation_track", "chart-spline"},
		{"autoscroll_continuous_on", "arrow-right"},
		{"autoscroll_off", "pause"},
		{"autoscroll_stepped_on", "chevrons-right"},
		{"back_to_start", "skip-back"},
		{"back_to_zero", "rewind"},
		{"cancel", "x"},
		{"chord", "layers"},
		{"clear_ghost_note", "ghost"},
		{"clear_notes_out_of_bounds", "eraser"},
		{"clip_rec", "circle-dot"},
		{"clock", "clock"},
		{"clone_pattern_track_clip", "copy-plus"},
		{"close", "x"},
		{"colorize", "palette"},
		{"computer", "monitor"},
		{"controller", "sliders-horizontal"},
		{"cursor_select_left", "chevrons-left"},
		{"cursor_select_right", "chevrons-right"},
		{"cut_overlaps", "scissors-line-dashed"},
		{"detach", "external-link"},
		{"discard", "trash-2"},
		{"dont_know", "circle-question-mark"},
		{"edit_copy", "copy"},
		{"edit_cut", "scissors"},
		{"edit_draw", "pencil"},
		{"edit_draw_outvalue", "pen-line"},
		{"edit_draw_small", "pencil"},
		{"edit_erase", "eraser"},
		{"edit_knife", "scissors"},
		{"edit_merge", "merge"},
		{"edit_move", "move"},
		{"edit_paste", "clipboard-paste"},
		{"edit_redo", "redo-2"},
		{"edit_rename", "text-cursor-input"},
		{"edit_select", "square-dashed-mouse-pointer"},
		{"edit_tangent", "spline"},
		{"edit_undo", "undo-2"},
		{"edit_unlink", "unlink"},
		{"effect_plugin", "wand-sparkles"},
		{"envelope_graph", "activity"},
		{"error", "circle-alert"},
		{"exit", "log-out"},
		{"file", "file"},
		{"fill", "paint-bucket"},
		{"flip_x", "flip-horizontal-2"},
		{"flip_y", "flip-vertical-2"},
		{"folder", "folder"},
		{"folder_locked", "folder-lock"},
		{"folder_opened", "folder-open"},
		{"fullscreen", "maximize"},
		{"ghost_note", "ghost"},
		{"glue", "combine"},
		{"gridmode", "grid-2x2"},
		{"help", "circle-question-mark"},
		{"hint", "lightbulb"},
		{"home", "house"},
		{"icon", "audio-waveform"},
		{"icon_small", "audio-waveform"},
		{"insert_bar", "square-plus"},
		{"instrument_track", "piano"},
		{"keep_stop_position", "map-pin"},
		{"lfo_graph", "activity"},
		{"loop_points_off", "repeat"},
		{"loop_points_on", "repeat"},
		{"master_pitch", "arrow-up-down"},
		{"master_volume", "volume-2"},
		{"max_length", "maximize-2"},
		{"maximize", "maximize-2"},
		{"metronome", "timer"},
		{"microtuner", "audio-lines"},
		{"midi_cc_rack", "sliders-horizontal"},
		{"midi_file", "file-music"},
		{"min_length", "minimize-2"},
		{"mixer", "sliders-vertical"},
		{"mixer_send_off", "circle"},
		{"mixer_send_on", "circle-arrow-up"},
		{"muted", "volume-x"},
		{"new_channel", "square-plus"},
		{"note", "music-2"},
		{"note_double_whole", "music-2"},
		{"note_eighth", "music-2"},
		{"note_half", "music-2"},
		{"note_none", "music-2"},
		{"note_quarter", "music-2"},
		{"note_sixteenth", "music-2"},
		{"note_thirtysecond", "music-2"},
		{"note_triplet", "music-2"},
		{"note_whole", "music-2"},
		{"output_graph", "activity"},
		{"pattern_track", "grid-3x3"},
		{"pattern_track_btn", "grid-3x3"},
		{"pause", "pause"},
		{"piano", "piano"},
		{"play", "play"},
		{"playpos_marker", "triangle"},
		{"plugins", "plug"},
		{"pr_no_clip", "music"},
		{"preset_file", "file-cog"},
		{"progression_cubic_hermite", "chart-spline"},
		{"progression_discrete", "chart-column"},
		{"progression_linear", "trending-up"},
		{"project_export", "upload"},
		{"project_file", "file-music"},
		{"project_import", "download"},
		{"project_new", "file-plus"},
		{"project_new_from_template", "file-plus-2"},
		{"project_notes", "notebook-pen"},
		{"project_open", "folder-open"},
		{"project_open_recent", "history"},
		{"project_save", "save"},
		{"proportional_snap", "ruler"},
		{"quantize", "magnet"},
		{"receive_bg_arrow", "arrow-down"},
		{"record", "circle-dot"},
		{"record_accompany", "disc-3"},
		{"record_step_off", "footprints"},
		{"record_step_on", "footprints"},
		{"recording_playpos_marker", "triangle"},
		{"recover", "life-buoy"},
		{"reload", "refresh-cw"},
		{"remove_bar", "square-minus"},
		{"restore", "minimize-2"},
		{"sample_file", "file-audio"},
		{"sample_track", "audio-waveform"},
		{"sample_track_btn", "audio-waveform"},
		{"scale", "music"},
		{"send_bg_arrow", "arrow-down"},
		{"setup_audio", "speaker"},
		{"setup_directories", "folder-cog"},
		{"setup_general", "settings"},
		{"setup_midi", "keyboard-music"},
		{"setup_performance", "gauge"},
		{"songeditor", "rows-3"},
		{"soundfont_file", "file-audio-2"},
		{"star", "star"},
		{"stop", "square"},
		{"tempo_sync", "clock"},
		{"text_block", "align-justify"},
		{"text_bold", "bold"},
		{"text_center", "align-center"},
		{"text_italic", "italic"},
		{"text_left", "align-left"},
		{"text_right", "align-right"},
		{"text_under", "underline"},
		{"tool", "wrench"},
		{"track_op_grip", "grip-vertical"},
		{"track_op_grip_c", "grip-vertical"},
		{"unknown_file", "file-question-mark"},
		{"vst_plugin_file", "plug"},
		{"whatsthis", "circle-question-mark"},
		{"zoom", "zoom-in"},
		{"zoom_x", "move-horizontal"},
		{"zoom_y", "move-vertical"}
	};
	return table;
}

QByteArray lucide(const QString& name)
{
	if (name.isEmpty()) { return {}; }
	QFile file(QStringLiteral(":/zene/icons/%1.svg").arg(name));
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

//! Render @a svg into @a rect of @a painter, its strokes and fills in @a colour.
void renderSvg(QPainter& painter, const QByteArray& svg, const QRectF& rect, const QColor& colour)
{
	QByteArray coloured = svg;
	coloured.replace("currentColor", colour.name(QColor::HexRgb).toLatin1());
	QSvgRenderer renderer(coloured);
	if (renderer.isValid()) { renderer.render(&painter, rect); }
}

QPixmap renderPixmap(const QByteArray& svg, const QSize& size, const QColor& colour, qreal dpr)
{
	QPixmap pm(size * dpr);
	pm.fill(Qt::transparent);
	{
		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing);
		const qreal side = std::min(pm.width(), pm.height());
		renderSvg(p, svg, QRectF((pm.width() - side) / 2.0, (pm.height() - side) / 2.0, side, side), colour);
	}
	pm.setDevicePixelRatio(dpr);
	return pm;
}

class SvgIconEngine : public QIconEngine
{
public:
	explicit SvgIconEngine(QByteArray svg) : m_svg(std::move(svg)) {}

	void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
	{
		const tokens::Palette& pal = tokens::palette();
		QColor colour = state == QIcon::On ? pal.accent : pal.text;
		if (mode == QIcon::Disabled) { colour = pal.textDisabled; }
		const qreal side = std::min(rect.width(), rect.height());
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		renderSvg(*painter, m_svg, QRectF(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0,
			side, side), colour);
		painter->restore();
	}

	QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
	{
		QPixmap pm(size);
		pm.fill(Qt::transparent);
		QPainter p(&pm);
		paint(&p, QRect(QPoint(0, 0), size), mode, state);
		return pm;
	}

	QIconEngine* clone() const override { return new SvgIconEngine(m_svg); }

private:
	QByteArray m_svg;
};

} // namespace

QStringList ids()
{
	QStringList list;
	for (const auto& entry : shellTable()) { list << entry.first; }
	return list;
}

QByteArray svg(const QString& id)
{
	const auto it = shellTable().find(id);
	return it == shellTable().end() ? QByteArray() : lucide(it->second);
}

QIcon icon(const QString& id) { return QIcon(new SvgIconEngine(svg(id))); }

QStringList legacyNames()
{
	QStringList list;
	for (const auto& entry : legacyTable()) { list << entry.first; }
	return list;
}

QString lucideForLegacy(const QString& name)
{
	const auto it = legacyTable().find(name);
	if (it != legacyTable().end()) { return it->second; }
	// A plugin's own logo ("tripleoscillator/logo") becomes the one device glyph: devices are
	// told apart by name, not by a bitmap each.
	return name.endsWith(QStringLiteral("/logo")) ? QStringLiteral("audio-waveform") : QString();
}

QPixmap legacyPixmap(const QString& name, int width, int height)
{
	const QByteArray data = lucide(lucideForLegacy(name));
	if (data.isEmpty()) { return {}; }
	QSize size(width, height);
	if (width <= 0 || height <= 0)
	{
		// Keep the inherited layout: the old bitmap's own size, read from its header only.
		size = QImageReader(QStringLiteral("artwork:") + name).size();
		if (!size.isValid() || size.isEmpty()) { size = QSize(16, 16); }
	}
	const qreal dpr = QGuiApplication::primaryScreen() != nullptr ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
	return renderPixmap(data, size, tokens::palette().text, dpr);
}

} // namespace lmms::gui::shellicons
