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

#include <array>
#include <atomic>

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
	/*! An output expression text the audio thread may read (cluster
	 * new-p9-530-port).
	 *
	 * m_outputExpression is GUI-thread storage: every keystroke in the expression
	 * editor rewrites it in place, and a QByteArray assignment releases the buffer
	 * it replaces. A note that starts while such a rewrite is in flight creates its
	 * synthesiser from the text (playNoteImpl), so reading that member there races
	 * the editor and can parse freed storage.
	 *
	 * The GUI thread publishes immutable copies instead and a note reads one through
	 * a PIN: four fixed slots, one reader count per slot and one published slot. This
	 * is the repo's multi-reader publication (PublishedTransients,
	 * include/TransientRepeat.h), and it has to be multi-reader here: the audio engine
	 * runs one note play handle per worker (AudioEngine::renderStageInstruments fills
	 * the job queue, AudioEngineWorkerThread::startAndWaitForJobs drains it on the
	 * pool) and every one of them calls the track's single instrument, so several
	 * notes of one track initialise at the same time. One announcement per expression
	 * would not do: the second note's read would replace the first note's
	 * announcement, and the next publication could then recycle the text the first
	 * note is still copying. A pin keeps its own slot out of the writer's reach for as
	 * long as its reader holds it.
	 *
	 * A reader pins the published slot (increments its count), checks the slot is
	 * still the published one, and only then copies; it unpins afterwards. The single
	 * writer writes only a slot that is neither published nor pinned, then publishes
	 * it. All the atomics are sequentially consistent: the reader's pin-then-check and
	 * the writer's publish-then-scan are a Dekker pair, which needs the single total
	 * order. The reader never waits - it makes at most ReadAttempts tries (each one
	 * lost only to a publication that happened in between) and otherwise reports no
	 * text, so a note started at that instant renders silence instead of blocking the
	 * audio thread. It never allocates either. The writer, which is never on the audio
	 * thread, yields until a slot is free; with four slots that needs three other notes
	 * copying at that instant.
	 *
	 * The class is public so the regression test can drive the hand-off itself;
	 * nothing outside the plug-in uses it.
	 */
	class ExpressionSnapshot
	{
	public:
		static constexpr int Slots = 4;
		static constexpr int ReadAttempts = 16;

		//! The text one note worker holds while it copies it. The slot stays pinned
		//! until this object dies, so no publication can recycle the text in between.
		class Read
		{
		public:
			Read() = default;
			Read(const ExpressionSnapshot* snapshot, int slot)
				: m_snapshot(snapshot), m_slot(slot)
			{
			}
			Read(const Read&) = delete;
			Read& operator=(const Read&) = delete;
			Read(Read&& other) noexcept
				: m_snapshot(other.m_snapshot), m_slot(other.m_slot)
			{
				other.m_snapshot = nullptr;
				other.m_slot = -1;
			}
			~Read();
			//! The pinned text, or "" when no slot could be pinned.
			const char* text() const;
			//! The slot this read pinned, or -1.
			int slot() const { return m_slot; }

		private:
			const ExpressionSnapshot* m_snapshot = nullptr;
			int m_slot = -1;
		};

		ExpressionSnapshot() = default;
		ExpressionSnapshot(const ExpressionSnapshot&) = delete;
		ExpressionSnapshot& operator=(const ExpressionSnapshot&) = delete;

		//! Control thread (the one writer): publish @a expression.
		void publish(const QByteArray& expression);
		//! Any thread, lock-free and non-waiting: a pin on the published text, or one
		//! that holds nothing when every attempt raced a publication.
		Read read() const;
		//! Test seam: the slot currently published.
		int publishedSlot() const { return m_published.load(); }

	private:
		friend class Read;
		void unpin(int slot) const;

		//! The immutable texts: written only by publish(), and only into a slot that
		//! is neither published nor pinned.
		std::array<QByteArray, Slots> m_texts{};
		//! One count per slot: the note workers copying that slot.
		mutable std::array<std::atomic<int>, Slots> m_readers{};
		//! The slot a new reader starts from.
		std::atomic<int> m_published{0};
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
	//! Audio thread: the published text pinned for the whole ExprFront copy. Several
	//! note workers start notes at once, so each note holds its own pin and no
	//! publication can recycle the text it is copying. Non-waiting, allocation-free.
	static ExprFront* makeExprFront(const ExpressionSnapshot& snapshot);
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
