/* MixerModeVisibilityTestSupport.h - visible and unobscured mode controls, independent of layout implementation.
 * Copyright (c) 2026 Zene Studio contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LMMS_TEST_MIXER_MODE_VISIBILITY_H
#define LMMS_TEST_MIXER_MODE_VISIBILITY_H
#include <QAbstractButton>
#include <QComboBox>
#include <QString>
#include <QWidget>

namespace lmms::test
{
inline bool mixerModeInteractive(QWidget* control)
{
    return qobject_cast<QAbstractButton*>(control) || qobject_cast<QComboBox*>(control);
}
inline QString mixerModeControlProblem(QWidget* control, QWidget* bar, QWidget* window)
{
    const QRect inBar(control->mapTo(bar, QPoint()), control->size());
    const QRect inWindow(control->mapTo(window, QPoint()), control->size());
    if (!bar->rect().contains(inBar) || !window->rect().contains(inWindow))
    {
        return QStringLiteral("mode control clipped: %1").arg(control->objectName());
    }
    QWidget* hit = window->childAt(inWindow.center());
    if (hit == control) { return {}; }
    if (hit && control->isAncestorOf(hit)) { return {}; }
    return QStringLiteral("mode control obscured: %1").arg(control->objectName());
}
inline QString mixerModeVisibilityProblem(QWidget* bar, QWidget* window)
{
    if (!bar || !window || !bar->isVisibleTo(window)) { return QStringLiteral("mode bar is not visible"); }
    int controls = 0;
    for (QWidget* control : bar->findChildren<QWidget*>())
    {
        if (!mixerModeInteractive(control) || !control->isVisibleTo(bar)) { continue; }
        ++controls;
        const QString problem = mixerModeControlProblem(control, bar, window);
        if (!problem.isEmpty()) { return problem; }
    }
    if (controls < 12) { return QStringLiteral("expected all mode controls, saw %1").arg(controls); }
    return {};
}
}
#endif
