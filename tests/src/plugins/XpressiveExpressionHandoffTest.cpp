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
 * of the text and the audio thread parses what was published, and it keeps the
 * text it publishes alive for every note worker that is copying it. The audio
 * engine runs one play handle per worker (`AudioEngine::renderStageInstruments`
 * fills the job queue, `startAndWaitForJobs` drains it on the pool) and they all
 * call the same track instrument, so several notes of one track initialise at
 * once and each of them holds its own text. The proof is a product-level
 * contract, not a source check:
 *
 *  - a text a note holds is not rewritten by later publications, so a note that
 *    starts mid-edit parses one whole expression;
 *  - a second note reading a newer publication does not free the text the first
 *    note is still copying;
 *  - a publication reaches the next note;
 *  - an edit of the GUI thread's own copy reaches no note at all.
 *
 * The plug-in's own sources are compiled into this test (the Vibed shape), so
 * the note-start path under test is the shipping one, and every step is a
 * direct call: no threads, no sleeps, no timing. The pin interleaving is
 * reported on stdout ("pin sequence: ...") so the run itself records which text
 * each note held while the editor kept publishing.
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

/*! Whether the hand-off hands a note worker a PINNED read.
 *
 * The audio engine runs each note play handle on a worker (`AudioEngine::
 * renderStageInstruments` fills the job queue, `startAndWaitForJobs` drains it
 * on the pool) and every one of them calls the same track instrument, so
 * several notes of one track initialise at the same time. A single
 * announcement per expression cannot cover that: the second note would replace
 * the first note's announcement, and the next publication could recycle the
 * text the first note is still copying. The fixed hand-off therefore keeps a
 * per-slot reader count and hands out a pin, and this trait is how the file
 * compiles against the reverted product (a borrowed pointer and no pin).
 */
template <typename Snapshot, typename = void>
struct HasPinnedRead : std::false_type
{
};

template <typename Snapshot>
struct HasPinnedRead<Snapshot,
	std::void_t<decltype(std::declval<const Snapshot&>().read().text())>> : std::true_type
{
};

//! The text one note worker holds while it parses it.
template <typename Snapshot, bool Pinned = HasPinnedRead<Snapshot>::value>
struct HeldText;

//! The fixed hand-off: an RAII pin, released when the copy is done.
template <typename Snapshot>
struct HeldText<Snapshot, true>
{
	explicit HeldText(Snapshot& snapshot) : m_read(static_cast<const Snapshot&>(snapshot).read()) {}
	const char* text() const { return m_read.text(); }

	typename Snapshot::Read m_read;
};

//! The reverted product: a borrowed pointer that nothing protects.
template <typename Snapshot>
struct HeldText<Snapshot, false>
{
	explicit HeldText(Snapshot& snapshot) : m_borrowed(snapshot.text()) {}
	const char* text() const { return m_borrowed; }

	const char* m_borrowed;
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
	HeldText<typename Instrument::ExpressionSnapshot> held{snapshot};

	// The GUI thread publishes three more texts meanwhile.
	snapshot.publish(QByteArray{"0.25"});
	snapshot.publish(QByteArray{"0.125"});
	snapshot.publish(QByteArray{"sinew(integrate(f))"});

	QVERIFY2(std::strcmp(held.text(), "0.5") == 0,
		"a text the reader was holding was rewritten by a later publication: "
		"the reader would parse a half-written expression");

	// The control: the publications did reach the next reader, so the check
	// above has the power to fail.
	HeldText<typename Instrument::ExpressionSnapshot> next{snapshot};
	QVERIFY2(std::strcmp(next.text(), "sinew(integrate(f))") == 0,
		"the last publication did not reach the reader");
}

/*! The concurrency the audio engine really produces: two note workers hold a
 *  text each, the second one after a newer publication, and the editor keeps
 *  publishing under both of them.
 *
 *  This is the deterministic form of the reported race. Nothing here races: the
 *  first note's pin is held across every later publication, exactly as a note
 *  worker holds it while `ExprFront` copies the bytes, and the second note's
 *  read is what used to replace the first note's announcement. The text the
 *  first note holds must stay whole; a hand-off that tracks one announcement
 *  instead of one pin per reader lets the third publication recycle it.
 */
template <typename Snapshot>
void checkTwoConcurrentNotes()
{
	Snapshot snapshot;
	snapshot.publish(QByteArray{"0.5"});
	HeldText<Snapshot> first{snapshot};          // note A starts, and copies
	qInfo("pin sequence: first note holds \"%s\"", first.text());

	snapshot.publish(QByteArray{"0.25"});
	HeldText<Snapshot> second{snapshot};         // note B starts mid-copy
	qInfo("pin sequence: second note holds \"%s\", first note still holds \"%s\"",
		second.text(), first.text());

	// The editor keeps typing while both notes copy.
	snapshot.publish(QByteArray{"0.125"});
	snapshot.publish(QByteArray{"sinew(integrate(f))"});
	snapshot.publish(QByteArray{"0.75"});
	HeldText<Snapshot> next{snapshot};
	qInfo("pin sequence: after 3 more publications first=\"%s\" second=\"%s\" published=\"%s\"",
		first.text(), second.text(), next.text());

	QVERIFY2(std::strcmp(first.text(), "0.5") == 0,
		"the text a note was copying was rewritten by a publication that the "
		"second note's read made look free: a note that starts while another "
		"copies gets freed storage");
	QVERIFY2(std::strcmp(second.text(), "0.25") == 0,
		"the second note's text was rewritten while it copied");
	QVERIFY2(std::strcmp(next.text(), "0.75") == 0,
		"the last publication did not reach the next note");
}

//! Runs the two-concurrent-notes check against the plug-in's hand-off.
template <typename T>
void runTwoConcurrentNotesCheck()
{
	if constexpr (HasExpressionHandOff<T>::value)
	{
		checkTwoConcurrentNotes<typename T::ExpressionSnapshot>();
	}
	else
	{
		QFAIL("the plug-in publishes no immutable expression: a note that "
			"starts while the expression editor rewrites the text parses the "
			"buffer the GUI thread is freeing");
	}
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

	/*! Two notes of one track initialise at the same time (the audio engine runs
	 *  each play handle on a worker), so the second note's read must not make the
	 *  first note's text recyclable. This is the multi-reader half of the
	 *  hand-off, held across every later publication. */
	void everyConcurrentNoteKeepsItsOwnText()
	{
		runTwoConcurrentNotesCheck<Xpressive>();
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
