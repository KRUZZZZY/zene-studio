/*
 * Xpressive.h - Instrument which uses a mathematical formula parser
 *
 * Copyright (c) 2016-2017 Orr Dvori
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


#ifndef XPRESSIVE_H
#define XPRESSIVE_H


#include <QTextEdit>
#include <QTimer>

#include <atomic>
#include <vector>

#include "AutomatableModel.h"
#include "Graph.h"
#include "Instrument.h"
#include "InstrumentView.h"

#include "ExprSynth.h"

class QPlainTextEdit;

namespace lmms
{


const int	W1_EXPR = 0;
const int	W2_EXPR = 1;
const int	W3_EXPR = 2;
const int	O1_EXPR = 3;
const int	O2_EXPR = 4;
const int	NUM_EXPRS = 5;


namespace gui
{
class AutomatableButtonGroup;
class Knob;
class LedCheckBox;
class PixmapButton;
}


class Xpressive : public Instrument
{
	Q_OBJECT
public:
	/*! An output expression text the audio thread is allowed to read.
	 *
	 * m_outputExpression is GUI-thread storage: every keystroke in the expression
	 * editor rewrites it in place, and a QByteArray assignment releases the buffer
	 * it replaces. A note that starts while such a rewrite is in flight creates its
	 * synthesiser from the text (playNoteImpl), so reading that member there races
	 * the editor and can parse freed storage.
	 *
	 * The GUI thread publishes an immutable copy instead. A publication fills a
	 * text nobody can see yet and hands it over with one atomic store; a published
	 * text is never written again, so the audio thread parses one whole expression
	 * however many publications pass while it does.
	 *
	 * The audio thread announces the text it took, so the GUI thread sees which
	 * published text it must not reuse: the announced one stays out of the free
	 * list until the reader takes another one (the reader parses the text before it
	 * takes another one). While the reader is between taking a text and announcing
	 * it, the GUI thread cannot tell which one it took, so it reuses none of them.
	 * The two flags are read and written with sequentially consistent ordering,
	 * which is what makes that hand-over race free: the reader's "I am reading"
	 * store and the writer's "is it reading" load cannot both miss each other.
	 *
	 * One writer (the GUI thread), one reader (the audio thread), no lock, no
	 * allocation and no logging on the audio thread: it takes the published text
	 * with one atomic load and parses it exactly as before.
	 *
	 * The class is public so the regression test can drive the hand-off itself;
	 * nothing outside the plug-in uses it.
	 */
	class ExpressionSnapshot
	{
	public:
		ExpressionSnapshot();
		~ExpressionSnapshot();
		ExpressionSnapshot(const ExpressionSnapshot&) = delete;
		ExpressionSnapshot& operator=(const ExpressionSnapshot&) = delete;

		//! The published text: the audio thread takes it and parses it.
		const char* text();
		//! Hands off @a expression as the current text.
		void publish(const QByteArray& expression);

	private:
		//! One immutable expression text.
		struct Expression
		{
			QByteArray text;
		};

		//! A text nobody can be reading: a free one, or a new one.
		Expression* takeFree();
		//! Return the published texts the reader cannot be using any more.
		void reclaim();

		//! The text the audio thread reads.
		std::atomic<Expression*> m_published{nullptr};
		//! The text the reader last took: it may still be parsing it.
		std::atomic<Expression*> m_readerExpression{nullptr};
		//! True while the reader is between taking a text and announcing it.
		std::atomic_bool m_readerReading{false};
		//! Published texts waiting to be reused. GUI thread only.
		std::vector<Expression*> m_retired;
		//! Texts that may be written into. GUI thread only.
		std::vector<Expression*> m_free;
	};

	Xpressive(InstrumentTrack* instrument_track );

	void deleteNotePluginData( NotePlayHandle* nph ) override;

	void saveSettings( QDomDocument& _doc,
							QDomElement& _this ) override;
	void loadSettings( const QDomElement& _this ) override;

	QString nodeName() const override;

	gui::PluginView* instantiateView( QWidget * parent ) override;

	graphModel& graphO1() { return m_graphO1; }
	graphModel& graphO2() { return m_graphO2; }
	graphModel& graphW1() { return m_graphW1; }
	graphModel& graphW2() { return m_graphW2; }
	graphModel& graphW3() { return m_graphW3; }
	graphModel& rawgraphW1() { return m_rawgraphW1; }
	graphModel& rawgraphW2() { return m_rawgraphW2; }
	graphModel& rawgraphW3() { return m_rawgraphW3; }
	IntModel& selectedGraph() { return m_selectedGraph; }
	QByteArray& wavesExpression(int i) { return m_wavesExpression[i]; }
	QByteArray& outputExpression(int i) { return m_outputExpression[i]; }
	//! Stores an output expression and hands it to the audio thread, which reads
	//! only the published copy. Every writer of m_outputExpression goes through
	//! here so that no edit can be left unpublished.
	void setOutputExpression(int i, const QByteArray& expression);

	FloatModel& parameterA1() { return m_parameterA1; }
	FloatModel& parameterA2() { return m_parameterA2; }
	FloatModel& parameterA3() { return m_parameterA3; }
	FloatModel& smoothW1() { return m_smoothW1; }
	FloatModel& smoothW2() { return m_smoothW2; }
	FloatModel& smoothW3() { return m_smoothW3; }
	BoolModel& interpolateW1() { return m_interpolateW1; }
	BoolModel& interpolateW2() { return m_interpolateW2; }
	BoolModel& interpolateW3() { return m_interpolateW3; }
	FloatModel& panning1() { return m_panning1; }
	FloatModel& panning2() { return m_panning2; }
	FloatModel& relTransition() { return m_relTransition; }
	WaveSample& W1() { return m_W1; }
	WaveSample& W2() { return m_W2; }
	WaveSample& W3() { return m_W3; }
	BoolModel& exprValid() { return m_exprValid; }
	static void smooth(float smoothness,const graphModel* in,graphModel* out);
protected:
	
protected slots:


private:
	void playNoteImpl(NotePlayHandle* nph, std::span<SampleFrame> out) override;
	//! Audio thread: records a render error, the main thread reports it.
	void recordPendingError(const char* message);
	//! Main thread: reports the render error the audio thread recorded.
	void reportPendingError();

	graphModel  m_graphO1;
	graphModel  m_graphO2;
	graphModel  m_graphW1;
	graphModel  m_graphW2;
	graphModel  m_graphW3;
	graphModel  m_rawgraphW1;
	graphModel  m_rawgraphW2;
	graphModel  m_rawgraphW3;
	IntModel m_selectedGraph;
	QByteArray m_wavesExpression[3];
	QByteArray m_outputExpression[2];
	//! What the audio thread parses: the published copy of each output expression.
	ExpressionSnapshot m_outputExpressionSnapshot[2];
	FloatModel m_parameterA1;
	FloatModel m_parameterA2;
	FloatModel m_parameterA3;
	FloatModel m_smoothW1;
	FloatModel m_smoothW2;
	FloatModel m_smoothW3;
	BoolModel m_interpolateW1;
	BoolModel m_interpolateW2;
	BoolModel m_interpolateW3;
	FloatModel m_panning1;
	FloatModel m_panning2;
	FloatModel m_relTransition;
	float m_A1,m_A2,m_A3;
	WaveSample m_W1, m_W2, m_W3;

	BoolModel m_exprValid;
	//! Main thread pump that reports the render errors the audio thread recorded.
	QTimer m_errorReportTimer;
	//! Render error waiting to be reported. Fixed size, so that recording it on the
	//! audio thread never allocates.
	char m_pendingError[128] = {};
	std::atomic_bool m_pendingErrorSet{false};

} ;

namespace gui
{


class XpressiveView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	XpressiveView( Instrument* _instrument,
					QWidget* _parent );

protected:


protected slots:
	void updateLayout();

	void sinWaveClicked();
	void triangleWaveClicked();
	void sqrWaveClicked();
	void sawWaveClicked();
	void noiseWaveClicked();
	void moogSawWaveClicked();
	void expWaveClicked();
	void usrWaveClicked();
	void helpClicked();
	void expressionChanged();
	void smoothChanged();
	void graphDrawn();

private:
	void modelChanged() override;

	Knob *m_generalPurposeKnob[3];
	Knob *m_panningKnob[2];
	Knob *m_relKnob;
	Knob *m_smoothKnob;
	QPlainTextEdit * m_expressionEditor;

	AutomatableButtonGroup *m_selectedGraphGroup;
	PixmapButton *m_w1Btn;
	PixmapButton *m_w2Btn;
	PixmapButton *m_w3Btn;
	PixmapButton *m_o1Btn;
	PixmapButton *m_o2Btn;
	PixmapButton *m_helpBtn;
	PixmapButton *m_sinWaveBtn;
	PixmapButton *m_triangleWaveBtn;
	PixmapButton *m_sqrWaveBtn;
	PixmapButton *m_sawWaveBtn;
	PixmapButton *m_whiteNoiseWaveBtn;
	PixmapButton *m_usrWaveBtn;
	PixmapButton *m_moogWaveBtn;
	PixmapButton *m_expWaveBtn;

	static QPixmap *s_artwork;

	Graph *m_graph;
	graphModel *m_raw_graph;
	LedCheckBox *m_expressionValidToggle;
	LedCheckBox *m_waveInterpolate;
	bool m_output_expr;
	bool m_wave_expr;
} ;

class XpressiveHelpView: public QTextEdit
{
	Q_OBJECT
public:
	static XpressiveHelpView* getInstance()
	{
		static XpressiveHelpView* instance = new XpressiveHelpView;
		return instance;
	}
	static void finalize()
	{
	}

private:
	XpressiveHelpView();
	static QString s_helpText;
};


} // namespace gui

} // namespace lmms

#endif
