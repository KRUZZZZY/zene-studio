/*
 * AccessibleWidgets.h - what a screen reader learns about the custom controls, and accessible mode
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

#ifndef LMMS_GUI_ACCESSIBLE_WIDGETS_H
#define LMMS_GUI_ACCESSIBLE_WIDGETS_H

#include <QString>
#include <QStringList>

#include "lmms_export.h"

class QObject;
class QWidget;

namespace lmms
{
class AutomatableModel;
}

/*! 0.5.0 UX decisions (themes 10, 17 and 23; one screen reader per platform - NVDA, VoiceOver, Orca).
 *
 *  Qt's accessibility bridges (UI Automation on Windows, NSAccessibility on macOS, AT-SPI on Linux) describe a
 *  widget by the QAccessibleInterface Qt makes for it. For a stock widget that is a button, a slider or a field; for
 *  a custom-painted control it is a nameless "client" with no value. installAccessibleWidgets() adds a factory
 *  that describes the shared custom controls (src/gui/widgets) as what they are:
 *  - a knob is a dial and every other model editor (a fader, a bar editor) a slider, with a value interface - the
 *    model's value, bounds and step, settable - and the value as it is shown ("-6.00 dB", "440 Hz");
 *  - an LCD number is a spin box, a model combo box a combo box (its current item the value; its actions open the
 *    menu and step the choice), an LED check box a check box, every other model button a push button - checkable
 *    when it toggles - whose action presses it;
 *  - a graph is a chart, a group box and the meter and navigation groups groupings, a tab bar a page-tab list.
 *  Each is named by its accessible name or else the name it already carries (AccessibilityAudit.h's
 *  impliedAccessibleName: its constructed name, its parameter's, its tooltip or its text).
 *
 *  Announcements (theme 17): a value change is announced only for the control with keyboard focus, and at most
 *  every 250 ms, with the settled value last - a value the automation moves is read when it is asked for, never
 *  narrated; meters and scopes have no value events at all. announceStatus() is for the transitions that matter:
 *  an error, a refused edit, a finished render.
 *
 *  installAccessibility() is installAccessibleWidgets() plus accessible mode's runtime part (Appearance.h): a focus
 *  ring painted over the custom controls that draw none themselves - shown for keyboard focus, and in accessible mode
 *  for any focus, focusRingWidth() wide - and reduced motion (no animated menus, combo boxes, tooltips or docks).
 *  Both run by themselves when the application object is made (AccessibleMode.cpp), so every binary that links
 *  the interface, the tests included, has them; calling them again does nothing. */
namespace lmms::a11y
{

LMMS_EXPORT void installAccessibleWidgets();
LMMS_EXPORT void installAccessibility();

//! What a key does to a value control: the keys every one of them answers as a slider does.
enum class KeyStep
{
	None,
	Up,       //!< Up or Right: one step up
	Down,     //!< Down or Left: one step down
	PageUp,   //!< a coarse step up
	PageDown, //!< a coarse step down
	Home,     //!< the minimum
	End       //!< the maximum
};
LMMS_EXPORT KeyStep keyStep(int key);
//! Apply @a step to @a model: @a fine for Up/Down, @a coarse for PageUp/PageDown, the bounds for Home/End; false
//! (and nothing changed) for KeyStep::None or no model.
LMMS_EXPORT bool applyKeyStep(AutomatableModel* model, KeyStep step, float fine, float coarse);
//! What a key did to a number field (an LCD spin box): stepped @a model as applyKeyStep does, or was Enter - the
//! field then asks for a value - or neither (it passes on).
enum class SpinKey
{
	None,
	Stepped,
	Entered
};
LMMS_EXPORT SpinKey spinKey(int key, AutomatableModel* model, float fine, float coarse);

//! How urgently a screen reader should speak an announcement.
enum class Politeness
{
	Polite,   //!< after what it is saying now (a finished render, a saved project)
	Assertive //!< at once (an error, a refused edit)
};
//! Whether a message the policy spoke a moment ago may be spoken again at once.
enum class Repeat
{
	Suppress, //!< the same text within the repeat window is dropped (a status that is re-sent on every refresh)
	Allow     //!< each call is a new event and is spoken (a threshold crossing, an explicit "speak it" request)
};
//! Speak @a message (Qt 6.8's announcement event; on older Qt an alert raised on announcementSource(), whose name
//! the screen reader reads). Nothing when no assistive technology is listening.
LMMS_EXPORT void announceStatus(QObject* source, const QString& message, Politeness politeness = Politeness::Polite,
	Repeat repeat = Repeat::Suppress);
//! The object a pre-6.8 alert for @a message is raised on: @a source when its accessible name already is the
//! message (a label that shows it), otherwise the window's hidden live region, named for the message, so the
//! screen reader reads the text and not the name of the window or application. Null when there is no source.
LMMS_EXPORT QObject* announcementSource(QObject* source, const QString& message);

//! The value @a widget shows, as a screen reader reads it ("-6.00 dB"); empty when it has none. A canvas
//! (AccessibleCanvas.h) reads the value it was described with.
LMMS_EXPORT QString valueText(QWidget* widget);

/*! Theme 17's announcement policy (AccessibleAnnouncements.cpp). announceStatus() speaks nothing when the level
 *  (config ui/announcements {level}, set by view.announcements) is "off", only assertive messages when it is
 *  "errors", and a message it spoke less than a second ago not again. */
enum class AnnounceLevel
{
	All,    //!< errors, refusals and the transitions below (the default)
	Errors, //!< errors and refusals only
	Off     //!< nothing
};
LMMS_EXPORT AnnounceLevel announceLevel();
//! "all", "errors", "off" - view.announcements' enum, in AnnounceLevel order.
LMMS_EXPORT const QStringList& announceLevelNames();
//! Whether announceStatus() may speak @a message now (the level, then the repeat window); true records it as spoken.
LMMS_EXPORT bool shouldAnnounce(const QString& message, Politeness politeness, Repeat repeat = Repeat::Suppress);
//! The last message the policy let through - spoken, or about to be when an assistive technology listens (tests).
LMMS_EXPORT QString lastAnnouncement();
//! How many messages the policy has let through since the program started (tests: a repeat of the same text counts).
LMMS_EXPORT int announcementCount();

//! The state transitions spoken by default.
enum class Transition
{
	Error,         //!< assertive: @a detail is the message
	Refused,       //!< assertive: "Not done: <detail>"
	RecordStarted, //!< polite: "Recording"
	RecordStopped, //!< polite: "Recording stopped"
	Saved,         //!< polite: "Saved <detail>" (the file's name)
	Opened,        //!< polite: "Opened <detail>"
	Finished       //!< polite: @a detail (a render, a bounce)
};
//! Speak @a kind on @a source (null: the active window), through announceStatus().
LMMS_EXPORT void announceTransition(QObject* source, Transition kind, const QString& detail = QString());
//! Follow the song for recording, save and open transitions (once per song; AccessibleMode calls it when a main
//! window shows, after the engine made the song).
LMMS_EXPORT void watchSongTransitions();

} // namespace lmms::a11y

#endif // LMMS_GUI_ACCESSIBLE_WIDGETS_H
