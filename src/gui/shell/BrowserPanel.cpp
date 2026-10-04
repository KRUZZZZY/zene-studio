/*
 * BrowserPanel.cpp - the shell's Browser: devices, presets, samples and projects in one panel
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

#include "BrowserPanel.h"

#include <utility>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QShortcut>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Accessibility.h"
#include "BrowserCatalog.h"
#include "BrowserSearchView.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "embed.h"
#include "FileBrowser.h"
#include "PluginBrowser.h"
#include "Shell.h"
#include "ShellLayout.h"
#include "WaveformPreview.h"

namespace lmms::gui
{

namespace
{

//! @a path when it names an audio file, else nothing: what the waveform shows for a search result or tile.
QString audioOrNone(const QString& path)
{
	return browserKindForPath(path) == BrowserKind::Audio ? path : QString();
}

//! The sample a list row names: a file list's sample, or a search result's audio file (its path is the row's data).
QString samplePathOf(const QTreeWidgetItem* item)
{
	if (item == nullptr) { return QString(); }
	if (const auto* file = dynamic_cast<const FileItem*>(item))
	{
		return file->type() == FileItem::FileType::Sample ? file->fullName() : QString();
	}
	return audioOrNone(item->data(0, Qt::UserRole).toString());
}

//! The keys the search field passes to the list it filters: the highlight's steps, and Enter.
bool isStepKey(int key) { return key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown; }
bool isEnterKey(int key) { return key == Qt::Key_Return || key == Qt::Key_Enter; }

//! @a view's first row that is not filtered out (a hidden row is one the search left out).
QModelIndex firstRow(const QAbstractItemView* view)
{
	const auto* tree = qobject_cast<const QTreeView*>(view);
	for (int row = 0; row < view->model()->rowCount(); ++row)
	{
		if (tree == nullptr || !tree->isRowHidden(row, QModelIndex())) { return view->model()->index(row, 0); }
	}
	return QModelIndex();
}

//! A source's "settle the search typing left pending" (BrowserSearchView::settleSearch), when it has one.
void settleSearch(QObject* source)
{
	if (source != nullptr && source->metaObject()->indexOfMethod("settleSearch()") >= 0) { QMetaObject::invokeMethod(source, "settleSearch"); }
}

} // namespace

BrowserPanel::BrowserPanel(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("browserPanel"));
	auto* box = new QVBoxLayout(this);
	box->setContentsMargins(tokens::space::sm, tokens::space::sm, tokens::space::sm, 0);
	box->setSpacing(tokens::space::xs);

	// Theme 3: search is the first control - the current source's own field, filtering only that source.
	m_searchSlot = new QStackedWidget(this);
	m_searchSlot->setObjectName(QStringLiteral("browserSearchSlot"));
	m_searchSlot->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	box->addWidget(m_searchSlot);

	m_selector = new QComboBox(this);
	m_selector->setMinimumHeight(tokens::size::target);
	a11y::announce(m_selector, tr("Browser source"),
		tr("Which content the browser lists: devices, presets, samples, projects or folders."));
	box->addWidget(m_selector);

	m_stack = new QStackedWidget(this);
	box->addWidget(m_stack, 1);
	m_wave = new WaveformPreview(this);
	box->addWidget(m_wave);
	connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged), m_stack, &QStackedWidget::setCurrentIndex);
	// WF-010: the waveform belongs to the source it was picked in; another source starts without one.
	connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged), m_wave, [this] { m_wave->setPath(QString()); });
	connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
		if (m_restoring || m_context.isEmpty()) { return; }
		ControlRegistry::instance()->invoke(QStringLiteral("browser.remember_source"),
			{{QStringLiteral("context"), m_context}, {QStringLiteral("source"), currentSource()}});
	});
	connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged), this, &BrowserPanel::showSource);
	// Search takes focus when asked to (Ctrl+F anywhere in the Browser), never by itself.
	auto* find = new QShortcut(QKeySequence(QKeySequence::Find), this);
	find->setContext(Qt::WidgetWithChildrenShortcut);
	connect(find, &QShortcut::activated, this, &BrowserPanel::focusSearch);
	// Theme 3 (owner decision): the preview stops when focus leaves the Browser - for another panel, a menu
	// of the application, another application. A menu or dialog the Browser opens is still the Browser.
	m_watches << connect(qApp, &QApplication::focusChanged, this, [this](QWidget* old, QWidget* now) {
		if (insideBrowser(old) && !insideBrowser(now)) { stopPreviews(); }
	});
}

BrowserPanel::~BrowserPanel()
{
	// The sources' lists emit as QWidget tears them down (a model reset, a focus change), after this panel's
	// members are gone: nothing may reach stopPreviews() from here on.
	for (const QMetaObject::Connection& watch : std::as_const(m_watches)) { disconnect(watch); }
}

QLineEdit* BrowserPanel::searchField() const { return qobject_cast<QLineEdit*>(m_searchSlot->currentWidget()); }

void BrowserPanel::focusSearch()
{
	if (QLineEdit* field = searchField())
	{
		field->setFocus(Qt::ShortcutFocusReason);
		field->selectAll();
	}
}

void BrowserPanel::stopPreviews()
{
	for (const QPointer<QObject>& previewer : m_previewers)
	{
		if (!previewer.isNull()) { QMetaObject::invokeMethod(previewer.data(), "stopPreview"); }
	}
}

bool BrowserPanel::insideBrowser(const QWidget* widget) const
{
	for (const QWidget* each = widget; each != nullptr; each = each->parentWidget())  // across windows: a popup has its opener as parent
	{
		if (each == this) { return true; }
	}
	return false;
}

void BrowserPanel::takeSearchField(SideBarWidget* source)
{
	auto* field = source->findChild<QLineEdit*>(QStringLiteral("browserSearch"));
	if (field == nullptr)
	{
		m_searchSlot->addWidget(new QWidget(m_searchSlot));  // the pages stay in step with the sources
		return;
	}
	if (!field->property("browserSearchHint").toBool())  // a field that teaches its syntax keeps its own words
	{
		field->setPlaceholderText(tr("Search %1").arg(source->title()));
		a11y::announce(field, tr("Search %1").arg(source->title()),
			tr("Filters this list as you type. The arrow keys move the highlight; Enter loads the highlighted result."));
	}
	field->setMinimumHeight(tokens::size::target);
	for (auto* own : source->findChildren<QShortcut*>())  // a source's own Ctrl+F would make the panel's ambiguous
	{
		if (own->key() == QKeySequence(QKeySequence::Find)) { own->setEnabled(false); }
	}
	m_searchSlot->addWidget(field);  // reparented only: the source still filters with it as before
	field->installEventFilter(this);
	m_watches << connect(field, &QLineEdit::textEdited, this, &BrowserPanel::stopPreviews);  // a changed query stops the preview
}

void BrowserPanel::watchResults(SideBarWidget* source)
{
	QList<QObject*> objects = source->findChildren<QObject*>();
	objects.prepend(source);
	for (QObject* object : objects)
	{
		if (object->metaObject()->indexOfSlot("stopPreview()") >= 0) { m_previewers.append(object); }
		auto* view = qobject_cast<QAbstractItemView*>(object);
		if (view == nullptr || !view->property("browserResults").toBool()) { continue; }
		// Another result, or the list refilled (a new query, a folder reloaded, another location): the preview stops.
		m_watches << connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &BrowserPanel::stopPreviews);
		m_watches << connect(view->model(), &QAbstractItemModel::modelAboutToBeReset, this, &BrowserPanel::stopPreviews);
	}
}

void BrowserPanel::showSource(int index)
{
	stopPreviews();  // another tab stops the preview
	m_searchSlot->setCurrentIndex(index);
	m_searchSlot->setVisible(searchField() != nullptr);
	orderFocus();
}

void BrowserPanel::orderFocus()
{
	QLineEdit* field = searchField();
	if (field == nullptr) { return; }
	// The widget Tab leaves for the Browser from: the first one before the field that is neither in the Browser
	// nor holding it (setTabOrder places a widget after ALL of the first one's focus children).
	QWidget* before = field->previousInFocusChain();
	while (before != field && (insideBrowser(before) || before->isAncestorOf(this))) { before = before->previousInFocusChain(); }
	if (before != field) { QWidget::setTabOrder(before, field); }
	QWidget::setTabOrder(field, m_selector);
}

QAbstractItemView* BrowserPanel::currentResults() const
{
	QWidget* source = m_stack->currentWidget();
	if (source == nullptr) { return nullptr; }
	for (auto* view : source->findChildren<QAbstractItemView*>())
	{
		if (view->property("browserResults").toBool() && view->isVisibleTo(source)) { return view; }
	}
	return nullptr;
}

bool BrowserPanel::searchKey(const QKeyEvent* key)
{
	if (!isStepKey(key->key()) && !isEnterKey(key->key())) { return false; }
	settleSearch(m_stack->currentWidget());  // the list the keys act on is the one for what is typed
	QAbstractItemView* list = currentResults();
	if (list == nullptr) { return false; }
	if (isEnterKey(key->key()) && !list->currentIndex().isValid())
	{
		list->setCurrentIndex(firstRow(list));  // nothing highlighted yet: Enter highlights, the next Enter loads
		return true;
	}
	QKeyEvent forwarded(QEvent::KeyPress, key->key(), key->modifiers(), key->text(), key->isAutoRepeat());
	QCoreApplication::sendEvent(list, &forwarded);
	return true;
}

bool BrowserPanel::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::KeyPress && watched == searchField()) { return searchKey(static_cast<QKeyEvent*>(event)); }
	return QWidget::eventFilter(watched, event);
}

void BrowserPanel::setContext(const QString& context)
{
	if (context == m_context) { return; }
	m_context = context;
	const QString remembered = ControlRegistry::instance()->invoke(QStringLiteral("browser.sources")).result
		.value(QStringLiteral("sources")).toObject().value(context).toString();
	m_restoring = true;
	if (!remembered.isEmpty()) { selectSource(remembered); }
	m_restoring = false;
}

void BrowserPanel::addSource(SideBarWidget* source)
{
	source->setChromeless(true);
	takeSearchField(source);
	watchResults(source);
	m_stack->addWidget(source);
	// WF-010: a sample selected in any file list - search results and gallery tiles included - shows its
	// waveform (browser.peaks) below.
	for (auto* tree : source->findChildren<QTreeWidget*>())
	{
		connect(tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) { m_wave->setPath(samplePathOf(item)); });
	}
	for (auto* tiles : source->findChildren<QListWidget*>())
	{
		connect(tiles, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* tile) {
			m_wave->setPath(tile != nullptr ? audioOrNone(tile->data(Qt::UserRole).toString()) : QString());
		});
	}
	m_selector->addItem(source->title());  // the first one selects it: showSource(0)
}

int BrowserPanel::sourceCount() const { return m_stack->count(); }

QString BrowserPanel::currentSource() const { return m_selector->currentText(); }

bool BrowserPanel::selectSource(const QString& title)
{
	const int index = m_selector->findText(title);
	if (index < 0) { return false; }
	m_selector->setCurrentIndex(index);
	return true;
}

BrowserPanel* createStandardBrowser(QWidget* parent)
{
	auto* panel = new BrowserPanel(parent);
	ConfigManager* conf = ConfigManager::inst();
	auto icon = [](const char* name) { return embed::getIconPixmap(name); };

	panel->addSource(new PluginBrowser(panel));
	panel->addSource(new BrowserSearchView(panel));  // 0.5.0 L11: search, collections, columns, project media
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, conf->userPresetsDir() + "*" + conf->factoryPresetsDir(),
		"*.xpf *.cs.xml *.xiz *.lv2", BrowserPanel::tr("Presets"), icon("preset_file"), panel, false,
		conf->userPresetsDir(), conf->factoryPresetsDir()));
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, conf->userSamplesDir() + "*" + conf->factorySamplesDir(),
		FileItem::defaultFilters(), BrowserPanel::tr("Samples"), icon("sample_file"), panel, false,
		conf->userSamplesDir(), conf->factorySamplesDir()));
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, conf->userProjectsDir() + "*" + conf->factoryProjectsDir(),
		"*.mmp *.mmpz *.xml *.mid *.mpt", BrowserPanel::tr("Projects"), icon("project_file"), panel, false,
		conf->userProjectsDir(), conf->factoryProjectsDir()));
	panel->addSource(new FileBrowser(FileBrowser::Type::Favorites, conf->favoriteItems().join("*"),
		FileItem::defaultFilters(), BrowserPanel::tr("Favorites"), icon("star"), panel, false, "", ""));
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, QDir::homePath(), FileItem::defaultFilters(),
		BrowserPanel::tr("Home"), icon("home"), panel, false));

	QStringList roots;
	QString title = BrowserPanel::tr("Root Directory");
	bool dirsAsItems = false;
#ifdef LMMS_BUILD_APPLE
	title = BrowserPanel::tr("Volumes");
	roots += "/Volumes";
#elif defined(LMMS_BUILD_WIN32)
	title = BrowserPanel::tr("My Computer");
	dirsAsItems = true;
#endif
#if !defined(LMMS_BUILD_APPLE)
	for (const QFileInfo& drive : QDir::drives()) { roots += drive.absolutePath(); }
#endif
	panel->addSource(new FileBrowser(FileBrowser::Type::Normal, roots.join("*"), FileItem::defaultFilters(), title,
		icon("computer"), panel, dirsAsItems));
	// WF-023: the browser follows the shell's view, showing the source remembered for it.
	if (auto* shell = qobject_cast<Shell*>(parent))
	{
		const auto follow = [panel, shell] { panel->setContext(ShellLayout::viewName(shell->model().view())); };
		QObject::connect(shell, &Shell::layoutChanged, panel, follow);
		follow();
	}
	return panel;
}

} // namespace lmms::gui
