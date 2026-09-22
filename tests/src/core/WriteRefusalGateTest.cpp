/* WriteRefusalGateTest - ARCH-4 slice S4's write-refusal gate.
 *
 * SPEC-ARCH-4-DOCUMENT-MODEL-DRAFT.md 1.7 Requirement 6 ("explicit about
 * derived vs authoritative data") and the S4 row it belongs to, which owes the
 * proof "the write-refusal gate + the existing checkpoint-restore tests":
 * "Every persisted value names one writer. The check is mechanical: a test
 * that greps the write path for a setter on a value it does not own (the
 * shape tests/rt-safety-scope.txt and the file-length ratchets already use)."
 * This is that test.
 *
 * What it reads: the WRITE PATH - every C/C++ source file (suffixes .cpp,
 * .c, .h, .hpp) under <root>/src except src/3rdparty - as TEXT with comments
 * stripped. It never starts the
 * engine (QTEST_GUILESS_MAIN, no Engine::init): a gate over source text needs
 * no binary state, exactly like RtSafetySweep.
 *
 * The two claims:
 *   1. oneWriterPerPersistedValue(): every SETTER rule below names its owning
 *      file; a call to that setter anywhere else in the write path is a
 *      violation, and a setter that appears NOWHERE is a dead rule - a ledger
 *      line that no longer resolves FAILS (rt-safety-scope.txt's rule). The
 *      attribute-side of the same claim: `srcin`/`srcout` (the clip window)
 *      may be written only by SampleClip.cpp, the `frozen*` take record only
 *      by Track.cpp.
 *   2. derivedValuesNeverReachTheFile(): a value marked derived-and-not-
 *      written has no setAttribute of its name anywhere in the write path
 *      (measured on this tree: the Sample render frame fields are re-derived
 *      from `srcin`/`srcout` at load - src/core/SampleClip.cpp:397-398 and
 *      :417-418 - and are never persisted), and every manifest row carries a
 *      non-empty marking. The marking column IS slice S4's item 1: a derived
 *      value is either not written at all (below) or, once the v2 writer
 *      starts carrying such values, carries `z:derived`. S4 must not change a
 *      byte of an existing project, so no `z:` attribute is emitted yet.
 *
 * Bounds stated, not hidden (the rt-safety declaration style):
 *   - the scope is DECLARED, not discovered: only the rules below are checked;
 *     a new derived value nobody adds here is seen by nothing;
 *   - comment stripping is textual (block comments, then //-to-end-of-line),
 *     so a setter behind a preprocessor branch or a line whose // sits inside
 *     a string literal is outside what this scan can see;
 *   - an owner is a FILE, not a function: the file that owns a value may call
 *     its own setter anywhere inside itself.
 *
 * Negative control (recorded in docs/s4-logs/, 2026-09-22): planting
 * `sClip->setSampleStartFrame(0);` in src/tracks/SampleTrack.cpp makes
 * oneWriterPerPersistedValue() FAIL naming that file and line; reverting the
 * plant turns it green again. A gate never seen to fail is not a gate.
 */

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QtTest>

namespace {

//! One persisted value, its slice-S4 marking, and the ONE file that owns its
//! setter(s). Adding a row WIDENS the gate; the scope is this table.
struct SetterRule
{
	const char* value;   // the persisted value the setter writes
	const char* marking; // SPEC-ARCH-4 1.7 R6 marking (item 1), never empty
	const char* owners;  // comma-separated repo-relative files allowed to call
	const char* setters; // comma-separated setter names, matched as `name\s*\(`
};

const SetterRule SETTER_RULES[] = {
	{ "clip sample window (srcin/srcout)",
		"AUTHORED, not derived: written to the file only when a window is edited "
		"(an untrimmed clip writes neither attribute - ClipSerialisationTest I9)",
		"src/core/SampleClip.cpp",
		"setSampleWindow,setSampleStartFrame,setSamplePlayLength" },
	{ "frozen take record (frozenAudio/frozenStart/frozenEnd/frozenMuted)",
		"DERIVED from the bounce output: written only when a take is installed, "
		"cleared on absence at load (TRACK_RESTORE_SCHEMA)",
		"src/core/Track.cpp",
		"clearFrozenTake,loadFrozenTake" },
	{ "clip take-lane tag (lane)",
		"AUTHORITATIVE, owned by the comp lane command; the loader assigns the "
		"member itself with reset-on-absence (CLIP_RESTORE_SCHEMA), never a setter",
		"src/core/ControlCommandsComp.cpp",
		"setLaneIndex" },
};

//! One attribute name in the file and who may write it. `nobodyMayWrite` is
//! the derived-and-not-written marking: no setAttribute of that name may exist
//! anywhere in the write path.
struct AttributeRule
{
	const char* value;
	const char* marking;
	const char* owners; // ignored when nobodyMayWrite is set
	const char* attributes; // comma-separated, matched inside setAttribute(
	bool nobodyMayWrite;
};

const AttributeRule ATTRIBUTE_RULES[] = {
	{ "clip sample window",
		"AUTHORED: srcin/srcout carry the window, written only by its owner",
		"src/core/SampleClip.cpp", "srcin,srcout", false },
	{ "frozen take record",
		"DERIVED from the bounce output, written only when a take is installed",
		"src/core/Track.cpp", "frozenAudio,frozenStart,frozenEnd,frozenMuted", false },
	{ "Sample render frame fields",
		"DERIVED, NOT WRITTEN: re-derived from srcin/srcout at load "
		"(src/core/SampleClip.cpp:397-398, :417-418) - no setAttribute may name them",
		"", "startframe,endframe", true },
};

//! The repository root, located by walking up from this file. Empty when the
//! walk fails - callers must refuse to pass rather than scan nothing.
QString repoRoot()
{
	QDir dir{QFileInfo{QString::fromUtf8(__FILE__)}.absolutePath()};
	while (!dir.isRoot())
	{
		if (dir.exists("src") && dir.exists("include") && dir.exists("tests"))
		{
			return dir.absolutePath();
		}
		dir.cdUp();
	}
	return {};
}

//! Textual comment stripping: block comments first (keeping their newlines,
//! so reported line numbers stay true), then //-to-end-of-line. The bound
//! this leaves is stated in the file header.
QString stripComments(QString text)
{
	static const QRegularExpression block{QStringLiteral(R"(/\*.*?\*/)"),
		QRegularExpression::DotMatchesEverythingOption};
	QString out;
	out.reserve(text.size());
	int last = 0;
	for (auto it = block.globalMatch(text); it.hasNext();)
	{
		const auto match = it.next();
		out += text.mid(last, match.capturedStart() - last);
		out += QString(match.captured().count(QLatin1Char('\n')), QLatin1Char('\n'));
		last = match.capturedEnd();
	}
	out += text.mid(last);
	static const QRegularExpression line{QStringLiteral(R"(//[^\n]*)")};
	return out.replace(line, QStringLiteral(" "));
}

//! Read one file with comments stripped; an unreadable file is a violation,
//! never a silent skip.
QString readStripped(const QString& file, QStringList& violations)
{
	QFile handle{file};
	if (!handle.open(QIODevice::ReadOnly))
	{
		violations << QStringLiteral("cannot read %1 - a scan that cannot read "
			"is not a pass").arg(file);
		return {};
	}
	return stripComments(QString::fromUtf8(handle.readAll()));
}

//! Every source file on the write path, repo-relative, sorted.
QStringList writePathFiles(const QString& root)
{
	QStringList files;
	QDirIterator it{root + QStringLiteral("/src"),
		QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.c"),
			QStringLiteral("*.h"), QStringLiteral("*.hpp")},
		QDir::Files, QDirIterator::Subdirectories};
	while (it.hasNext())
	{
		const QString path = it.next();
		if (!path.contains(QStringLiteral("/src/3rdparty/")))
		{
			files.append(path);
		}
	}
	std::sort(files.begin(), files.end());
	return files;
}

QStringList splitList(const char* csv)
{
	return QString::fromUtf8(csv).split(QLatin1Char(','), Qt::SkipEmptyParts);
}

int lineNumberAt(const QString& text, int offset)
{
	return static_cast<int>(text.left(offset).count(QLatin1Char('\n'))) + 1;
}

//! Every hit of rule's setters in `text`; a hit outside the owning file is
//! recorded in violations. Returns the hit count (for the dead-rule check).
int countSetterHits(const QString& text, const QString& relative,
	const SetterRule& rule, QStringList& violations)
{
	int hits = 0;
	const QStringList owners = splitList(rule.owners);
	for (const QString& setter : splitList(rule.setters))
	{
		const QRegularExpression pattern{
			QRegularExpression::escape(setter) + QStringLiteral(R"(\s*\()")};
		for (auto it = pattern.globalMatch(text); it.hasNext();)
		{
			const auto match = it.next();
			++hits;
			if (!owners.contains(relative))
			{
				violations << QStringLiteral("%1:%2: calls %3() - the value "
					"\"%4\" is owned by %5")
					.arg(relative)
					.arg(lineNumberAt(text, match.capturedStart()))
					.arg(setter, QString::fromUtf8(rule.value),
						QString::fromUtf8(rule.owners));
			}
		}
	}
	return hits;
}

struct SetterScan
{
	QStringList violations;
	QHash<QString, int> hits; // value -> hits on the whole write path
};

//! The one-writer scan over the whole write path, plus the dead-rule check.
SetterScan scanSetters(const QString& root, const QStringList& files)
{
	SetterScan scan;
	for (const QString& file : files)
	{
		const QString text = readStripped(file, scan.violations);
		if (text.isEmpty())
		{
			continue;
		}
		const QString relative = file.mid(root.length() + 1);
		for (const SetterRule& rule : SETTER_RULES)
		{
			scan.hits[QString::fromUtf8(rule.value)] +=
				countSetterHits(text, relative, rule, scan.violations);
		}
	}
	for (const SetterRule& rule : SETTER_RULES)
	{
		if (scan.hits[QString::fromUtf8(rule.value)] == 0)
		{
			scan.violations << QStringLiteral("dead rule: no setter for \"%1\" "
				"appears in the write path - a ledger line that no longer "
				"resolves FAILS (rt-safety-scope.txt's rule)")
				.arg(QString::fromUtf8(rule.value));
		}
	}
	return scan;
}

QStringList attributeHits(const QString& text, const QString& relative,
	const AttributeRule& rule)
{
	QStringList violations;
	const QStringList owners = splitList(rule.owners);
	for (const QString& attribute : splitList(rule.attributes))
	{
		// The derived-not-written half matches case-insensitively (no spelling
		// of a name that must not exist may slip past); the owned half matches
		// the attribute exactly as the file writes it.
		const QString name = rule.nobodyMayWrite
			? QStringLiteral("(?i:%1)").arg(QRegularExpression::escape(attribute))
			: QRegularExpression::escape(attribute);
		const QRegularExpression pattern{QStringLiteral(
			R"(setAttribute\s*\(\s*(?:QStringLiteral\s*\(\s*)?")")
			+ name + QStringLiteral("\"")};
		for (auto it = pattern.globalMatch(text); it.hasNext();)
		{
			const auto match = it.next();
			if (rule.nobodyMayWrite)
			{
				violations << QStringLiteral("%1:%2: setAttribute(\"%3\") - the "
					"value \"%4\" is marked DERIVED and must not reach the file "
					"(%5)")
					.arg(relative)
					.arg(lineNumberAt(text, match.capturedStart()))
					.arg(attribute,
						QString::fromUtf8(rule.value),
						QString::fromUtf8(rule.marking));
			}
			else if (!owners.contains(relative))
			{
				violations << QStringLiteral("%1:%2: writes attribute \"%3\" of "
					"\"%4\" - owned by %5")
					.arg(relative)
					.arg(lineNumberAt(text, match.capturedStart()))
					.arg(attribute,
						QString::fromUtf8(rule.value),
						QString::fromUtf8(rule.owners));
			}
		}
	}
	return violations;
}

//! Attribute-side scan. onlyNobodyMayWrite selects the derived-not-written
//! half (any hit is a violation); otherwise every hit must sit in an owner.
QStringList attributeViolations(const QString& root, const QStringList& files,
	bool onlyNobodyMayWrite)
{
	QStringList violations;
	for (const QString& file : files)
	{
		const QString text = readStripped(file, violations);
		if (text.isEmpty())
		{
			continue;
		}
		const QString relative = file.mid(root.length() + 1);
		for (const AttributeRule& rule : ATTRIBUTE_RULES)
		{
			if (rule.nobodyMayWrite != onlyNobodyMayWrite)
			{
				continue;
			}
			violations << attributeHits(text, relative, rule);
		}
	}
	return violations;
}

//! The manifest checks itself: every row carries a non-empty marking (item 1's
//! declaration), an owner where owners are required, and files that exist.
QStringList manifestViolations()
{
	QStringList violations;
	for (const SetterRule& rule : SETTER_RULES)
	{
		const QString marking = QString::fromUtf8(rule.marking);
		if (marking.isEmpty() || splitList(rule.setters).isEmpty()
			|| splitList(rule.owners).isEmpty())
		{
			violations << QStringLiteral("manifest row \"%1\" is incomplete "
				"(marking, owners and setters are all mandatory)")
				.arg(QString::fromUtf8(rule.value));
		}
	}
	for (const AttributeRule& rule : ATTRIBUTE_RULES)
	{
		if (QString::fromUtf8(rule.marking).isEmpty()
			|| splitList(rule.attributes).isEmpty())
		{
			violations << QStringLiteral("manifest row \"%1\" is incomplete "
				"(marking and attributes are mandatory)")
				.arg(QString::fromUtf8(rule.value));
		}
		if (!rule.nobodyMayWrite && splitList(rule.owners).isEmpty())
		{
			violations << QStringLiteral("attribute row \"%1\" names no owner")
				.arg(QString::fromUtf8(rule.value));
		}
	}
	return violations;
}

} // namespace

class WriteRefusalGateTest : public QObject
{
	Q_OBJECT

private slots:
	void oneWriterPerPersistedValue();
	void derivedValuesNeverReachTheFile();
};

//! The setter half and the attribute-ownership half of the one-writer rule,
//! plus the manifest's own integrity.
void WriteRefusalGateTest::oneWriterPerPersistedValue()
{
	const QString root = repoRoot();
	QVERIFY2(!root.isEmpty(),
		"cannot locate the repository root from __FILE__ - refusing to pass vacuously");
	const QStringList files = writePathFiles(root);
	QVERIFY2(files.size() > 100,
		"the write-path scan found too few files - refusing to pass vacuously");
	const SetterScan scan = scanSetters(root, files);
	QStringList violations = scan.violations;
	violations << attributeViolations(root, files, false);
	violations << manifestViolations();
	QVERIFY2(violations.isEmpty(),
		qPrintable(violations.join(QLatin1Char('\n'))));
}

//! Item 1's mechanical half: a value marked derived-and-not-written has no
//! writer in the write path at all.
void WriteRefusalGateTest::derivedValuesNeverReachTheFile()
{
	const QString root = repoRoot();
	QVERIFY2(!root.isEmpty(),
		"cannot locate the repository root from __FILE__ - refusing to pass vacuously");
	const QStringList files = writePathFiles(root);
	QVERIFY2(files.size() > 100,
		"the write-path scan found too few files - refusing to pass vacuously");
	const QStringList violations = attributeViolations(root, files, true);
	QVERIFY2(violations.isEmpty(),
		qPrintable(violations.join(QLatin1Char('\n'))));
}

QTEST_GUILESS_MAIN(WriteRefusalGateTest)
#include "WriteRefusalGateTest.moc"
