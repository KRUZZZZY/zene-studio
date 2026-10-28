/* eqspectrumview.h - defination of EqSpectrumView class.
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
#ifndef EQSPECTRUMVIEW_H
#define EQSPECTRUMVIEW_H

#include <QPainterPath>
#include <QVector>
#include <QWidget>

#include <atomic>

#include "AudioBufferView.h"
#include "fft_helpers.h"
#include "LmmsTypes.h"

namespace lmms
{

class SampleFrame;

const int MAX_BANDS = 2048;
class EqAnalyser
{
public:
	EqAnalyser();
	virtual ~EqAnalyser();

	//! The band magnitudes of the last analysis. The audio thread writes them, so a
	//! reader on another thread must use copySpectrum() instead of reading them here.
	float m_bands[MAX_BANDS];
	bool getInProgress() const;
	void clear();

	void analyze(InterleavedBufferView<const float, 2> buf);

	//! Copy the published spectrum into the caller's buffers: @a bands receives the
	//! MAX_BANDS band magnitudes, @a energy the energy they were measured with. The copy
	//! is always a whole spectrum: while the analyser publishes the next one, the last
	//! published spectrum is handed out instead of a half written one. The return value
	//! says whether the copy is the spectrum that is published now.
	bool copySpectrum(float* bands, float& energy) const;

	float getEnergy() const;
	int getSampleRate() const;
	bool getActive() const;

	void setActive(bool active);

	//! Test seam: called while the analyser publishes its spectrum, so a test can read
	//! the spectrum in the middle of the update. It is never set in production.
	using PublishHook = void (*)(void* context, int bandsPublished);
	void setPublishHook(PublishHook hook, void* context);

private:
	//! Hand the finished spectrum over to the reader (the GUI).
	void publish();

	fftwf_plan m_fftPlan;
	fftwf_complex * m_specBuf;
	float m_absSpecBuf[FFT_BUFFER_SIZE+1];
	float m_buffer[FFT_BUFFER_SIZE*2];
	//! The merged samples of the frames that did not fit into the last FFT window. They
	//! start the next one, so a block that crosses the window boundary loses nothing.
	float m_pending[FFT_BUFFER_SIZE];
	int m_pendingFrames;
	std::atomic<float> m_energy;
	std::atomic<int> m_sampleRate;
	std::atomic<bool> m_active;
	std::atomic<bool> m_inProgress;
	float m_fftWindow[FFT_BUFFER_SIZE];
	//! The published spectrum. Every band is an atomic, so a reader never races the
	//! audio thread on a plain float, and the sequence number tells the reader whether
	//! the copy it took is a whole spectrum or a half written one.
	std::atomic<float> m_publishedBands[MAX_BANDS];
	std::atomic<float> m_publishedEnergy;
	std::atomic<unsigned int> m_publishSequence;
	//! The last whole spectrum a reader copied, handed out again when a publication is
	//! in flight.
	mutable float m_lastSpectrum[MAX_BANDS]{};
	mutable float m_lastEnergy{0.f};
	PublishHook m_publishHook;
	void* m_publishHookContext;
};


namespace gui
{

class EqSpectrumView : public QWidget
{
	Q_OBJECT
public:
	explicit EqSpectrumView( EqAnalyser *b, QWidget *_parent = 0 );
	~EqSpectrumView() override = default;

	QColor getColor() const;
	void setColor( const QColor &value );

protected:
	void paintEvent( QPaintEvent *event ) override;

private slots:
	void periodicalUpdate();

private:
	QColor m_color;
	EqAnalyser *m_analyser;
	QPainterPath m_path;
	float m_peakSum;
	float m_pixelsPerUnitWidth;
	float m_scale;
	int m_skipBands;
	bool m_periodicalUpdate;
	QVector<float> m_bandHeight;
	//! The spectrum of the analyser, copied as a whole snapshot before it is painted.
	QVector<float> m_spectrumBands;

	float bandToFreq ( int index );
};


} // namespace gui

} // namespace lmms

#endif // EQSPECTRUMVIEW_H
