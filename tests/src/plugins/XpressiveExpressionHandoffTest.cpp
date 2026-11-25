/*
 * XpressiveExpressionHandoffTest.cpp - a note that starts while the expression
 * editor is rewriting an output expression must parse a whole, already
 * published expression, never the QByteArray the GUI thread is freeing.
 *
 * cluster new-p9-530-port. Xpressive::playNoteImpl() creates a note's
 * synthesiser (ExprFront, which copies the text) from
 * m_outputExpression[index].constData() while XpressiveView::expressionChanged()
 * assigns a fresh QByteArray to that very member on the GUI thread. A
 * QByteArray assignment releases the buffer it replaces, so a note that starts
 * during an edit parsed storage the GUI thread had freed.
 *
 * The contract this test pins down: the GUI thread PUBLISHES an immutable copy
 * of the text and the audio thread parses what was published. The proof is a
 * product-level contract, not a source check:
 *
 *  - a text a reader took is not rewritten by later publications, so a note
 *    that starts mid-edit parses one whole expression;
 *  - a publication reaches the next note;
 *  - an edit of the GUI thread's own copy reaches no note at all.
 *
 * The plug-in's own sources are compiled into this test (the Vibed shape), so
 * the note-start path under test is the shipping one, and every step is a
 * direct call: no threads, no sleeps, no timing.
 */

#include <QByteArray>
#include <QTest>

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

#include "Xpressive.h"

namespace lmms
{

namespace
{

//! One period of one note, both channels.
using Block = std::array<SampleFrame, 64>;

//! One expression index of the plug-in: 0 is O1, 1 is O2.
constexpr int Output1 = 0;

bool identical(const Block& lhs, const Block& rhs)
{
	return std::memcmp(lhs.data(), rhs.data(), sizeof(Block)) == 0;
}

/*! Whether the plug-in has the immutable expression hand-off.
 *
 * The mechanical negative control reverts the WHOLE product change, so in that
 * state there is no hand-off at all. Every check below is compiled through this
 * trait, so this file still builds there and the control sees the behavioural
 * failure the reverted note path produces instead of a build error.
 */
template <typename T, typename = void>
struct HasExpressionHandOff : std::false_type
{
};

template <typename T>
struct HasExpressionHandOff<T, std::void_t<typename T::ExpressionSnapshot>> : std::true_type
{
};

/*! Hand `text` over as the output expression, the way the editor does.
 *
 * Only the fixed plug-in has the publishing setter; without it the text is
 * written into the GUI thread's own copy, which is all the reverted product
 * offers.
 * @returns false when the plug-in has no hand-off.
 */
template <typename Instrument>
bool publishExpression(Instrument& instrument, int index, const char* text)
{
	if constexpr (HasExpressionHandOff<Instrument>::value)
	{
		instrument.setOutputExpression(index, QByteArray{text});
		return true;
	}
	else
	{
		instrument.outputExpression(index) = text;
		return false;
	}
}

/*! The held-expression check, in a template so that it is only compiled where
 *  the hand-off exists. */
template <typename Instrument>
void checkHeldExpression()
{
	typename Instrument::ExpressionSnapshot snapshot;
	snapshot.publish(QByteArray{"0.5"});

	// The audio thread takes the published text and keeps it while it parses it.
	const char* held = snapshot.text();

	// The GUI thread publishes three more texts meanwhile.
	snapshot.publish(QByteArray{"0.25"});
	snapshot.publish(QByteArray{"0.125"});
	snapshot.publish(QByteArray{"sinew(integrate(f))"});

	QVERIFY2(std::strcmp(held, "0.5") == 0,
		"a text the reader was holding was rewritten by a later publication: "
		"the reader would parse a half-written expression");

	// The control: the publications did reach the next reader, so the check
	// above has the power to fail.
	QVERIFY2(std::strcmp(snapshot.text(), "sinew(integrate(f))") == 0,
		"the last publication did not reach the reader");
}

//! Runs the held-expression check against the plug-in's hand-off.
template <typename T>
void runHeldExpressionCheck()
{
	if constexpr (HasExpressionHandOff<T>::value)
	{
		checkHeldExpression<T>();
	}
	else
	{
		QFAIL("the plug-in publishes no immutable expression: a note that "
			"starts while the expression editor rewrites the text parses the "
			"buffer the GUI thread is freeing");
	}
}

} // namespace

class XpressiveExpressionHandoffTest : public QObject
{
	Q_OBJECT

	//! One fresh note, one period, through the engine's own entry point into
	//! the plug-in (Instrument::playNote -> playNoteImpl).
	Block renderNote()
	{
		const Note note(TimePos(1), TimePos(0));
		NotePlayHandle handle(m_track, 0, 64, note);
		Block block;
		block.fill(SampleFrame(0.f, 0.f));
		m_instrument->playNote(&handle, std::span<SampleFrame>(block));
		// The handle's destructor asks the TRACK's instrument for the note data;
		// the instrument under test is not installed on that track, so the test
		// makes that hand-over itself.
		m_instrument->deleteNotePluginData(&handle);
		handle.m_pluginData = nullptr;
		return block;
	}

	//! A fresh instrument on a track of the song.
	std::unique_ptr<Xpressive> startInstrument()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		if (track == nullptr) { return nullptr; }
		m_track = track;
		return std::make_unique<Xpressive>(track);
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// This test renders its blocks itself; nothing may pull on the notes in
		// the background.
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
		NotePlayHandleManager::free();
	}

	/*! The hand-off itself: a text the audio thread took must stay whole while
	 *  the GUI thread publishes again. This is the deterministic form of the
	 *  reported race - the reader here is paused on the text it took while three
	 *  further publications go past, which is exactly what every keystroke in
	 *  the expression editor does. */
	void aHeldExpressionSurvivesLaterPublications()
	{
		runHeldExpressionCheck<Xpressive>();
	}

	/*! The reported race. Notes keep starting while the editor is rewriting the
	 *  output expression; a note must parse the last published text, so a rewrite
	 *  of the GUI thread's own copy reaches no note at all. */
	void anEditThatIsNotPublishedNeverReachesANewNote()
	{
		m_instrument = startInstrument();
		QVERIFY(m_instrument != nullptr);

		// The element writes the expression editor performs, on the GUI thread's
		// own copy, with no publication of either.
		m_instrument->outputExpression(Output1) = "0.5";
		const Block firstEdit = renderNote();
		m_instrument->outputExpression(Output1) = "0.25";
		const Block secondEdit = renderNote();

		QVERIFY2(identical(firstEdit, secondEdit),
			"a note started after an edit that was never published parsed a "
			"half-applied GUI copy instead of the last published expression");

		// The control: a publication DOES reach the next note, so the check above
		// has the power to fail.
		QVERIFY2(publishExpression(*m_instrument, Output1, "0.5"),
			"the plug-in has no expression hand-off: the note path reads the "
			"storage the GUI thread rewrites");
		const Block publishedHalf = renderNote();
		QVERIFY(publishExpression(*m_instrument, Output1, "0.25"));
		const Block publishedQuarter = renderNote();

		QVERIFY2(!identical(publishedHalf, publishedQuarter),
			"the published and the edited expression render the same block, so "
			"this check cannot tell them apart");
		QVERIFY2(!identical(firstEdit, publishedHalf),
			"the published expression rendered the block the unpublished edit did, "
			"so the two cannot be told apart");

		m_instrument.reset();
	}

	/*! Every publication must reach the NEXT note: the hand-off may not drop,
	 *  delay or mangle one. */
	void aPublicationReachesTheNextNote()
	{
		m_instrument = startInstrument();
		QVERIFY(m_instrument != nullptr);
		QVERIFY2(publishExpression(*m_instrument, Output1, "0.5"),
			"the plug-in has no expression hand-off");
		const Block half = renderNote();

		QVERIFY(publishExpression(*m_instrument, Output1, "-0.5"));
		const Block inverted = renderNote();

		QVERIFY(publishExpression(*m_instrument, Output1, "0.25"));
		const Block quarter = renderNote();

		QVERIFY2(!identical(half, inverted),
			"a publication did not reach the next note");
		QVERIFY2(!identical(half, quarter),
			"a second publication did not reach the next note");

		m_instrument.reset();
	}

private:
	InstrumentTrack* m_track = nullptr;
	std::unique_ptr<Xpressive> m_instrument;
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::XpressiveExpressionHandoffTest)
#include "XpressiveExpressionHandoffTest.moc"
