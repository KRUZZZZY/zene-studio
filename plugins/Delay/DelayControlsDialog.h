/*
 * delaycontrolsdialog.h - declaration of DelayControlsDialog class.
 *
 * Copyright (c) 2014 David French <dave/dot/french3/at/googlemail/dot/com>
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

#ifndef DELAYCONTROLSDIALOG_H
#define DELAYCONTROLSDIALOG_H

#include <algorithm>

#include "EffectControlDialog.h"

namespace lmms
{


class DelayControls;
class FloatModel;

namespace gui
{

namespace xyPadCoordinates
{

//! The pixel a value is drawn at. The range is mapped over the drawable
//! extent (extent - 1), so the last pixel is the end of the range: mapping it
//! over the pixel COUNT instead would put the maximum one pixel outside the
//! widget and leave the last in-bounds pixel short of the maximum.
inline int pixelForValue(float value, float minimum, float maximum, int extent)
{
	if (extent <= 1 || maximum <= minimum) { return 0; }
	const float normalized = std::clamp((value - minimum) / (maximum - minimum), 0.f, 1.f);
	return static_cast<int>(normalized * (extent - 1));
}

//! The value a pixel stands for: the same mapping, inverted.
inline float valueForPixel(int pixel, float minimum, float maximum, int extent)
{
	if (extent <= 1 || maximum <= minimum) { return minimum; }
	const int boundedPixel = std::clamp(pixel, 0, extent - 1);
	if (boundedPixel == extent - 1) { return maximum; }
	return minimum + (static_cast<float>(boundedPixel) / (extent - 1)) * (maximum - minimum);
}

} // namespace xyPadCoordinates

class DelayControlsDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	DelayControlsDialog( DelayControls* controls );
	~DelayControlsDialog() override = default;
};

class XyPad : public QWidget
{
	Q_OBJECT
public:
	//! Both models are required. A null model leaves the pad inert: it neither
	//! paints nor takes input, so nothing is dereferenced.
	XyPad( QWidget *parent, FloatModel *xModel, FloatModel *yModel );
	~XyPad() override = default;

protected:
	void paintEvent ( QPaintEvent * event ) override;
	void mousePressEvent(QMouseEvent * event ) override;
	void mouseReleaseEvent(QMouseEvent * event) override;
	void mouseMoveEvent(QMouseEvent * event) override;

private:
	FloatModel *m_xModel;
	FloatModel *m_yModel;
	bool m_acceptInput;
};


} // namespace gui

} // namespace lmms

#endif // DELAYCONTROLSDIALOG_H
