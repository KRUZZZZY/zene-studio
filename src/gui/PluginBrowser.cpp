/*
 * PluginBrowser.cpp - implementation of the plugin-browser
 *
 * Copyright (c) 2005-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "PluginBrowser.h"

#include <algorithm>
#include <utility>

#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyleOption>
#include <QTimer>
#include <QTreeWidget>

#include "EffectPickerOrganiser.h"
#include "PluginOrganiser.h"
#include "embed.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "TrackContainerView.h"
#include "PluginFactory.h"

namespace lmms::gui
{

namespace
{

//! The organiser's folders that hold a listed (not hidden) plug-in, by name as the plug-ins are sorted.
QStringList listedFolders(const QMap<QString, pluginorganiser::Entry>& organised)
{
	QStringList names;
	for (const pluginorganiser::Entry& entry : organised)
	{
		if (!entry.hidden && !entry.folder.isEmpty() && !names.contains(entry.folder)) { names << entry.folder; }
	}
	std::sort(names.begin(), names.end(), [](const QString& l, const QString& r) { return QString::compare(l, r, Qt::CaseInsensitive) < 0; });
	return names;
}

} // namespace


PluginBrowser::PluginBrowser( QWidget * _parent ) :
	SideBarWidget( tr( "Instrument Plugins" ),
				embed::getIconPixmap( "plugins" ).transformed( QTransform().rotate( 90 ) ), _parent )
{
	setWindowTitle( tr( "Instrument browser" ) );
	// 0.5.0: the list follows organiser changes made elsewhere (the effect picker, a command, a script).
	connect(pluginorganiser::notifier(), &pluginorganiser::Notifier::changed, this,
		[this] { QTimer::singleShot(0, this, [this] { rebuildKeepingView(); }); });
	m_view = new QWidget( contentParent() );
	//m_view->setFrameShape( QFrame::NoFrame );

	addContentWidget( m_view );

	auto view_layout = new QVBoxLayout(m_view);
	view_layout->setContentsMargins(5, 5, 5, 5);
	view_layout->setSpacing( 5 );


	auto hint = new QLabel( tr( "Drag an instrument "
					"into either the Song Editor, the "
					"Pattern Editor or an "
					"existing instrument track." ),
								m_view );
	hint->setWordWrap( true );

	auto searchBar = new QLineEdit(m_view);
	searchBar->setPlaceholderText(tr("Search"));
	searchBar->setMaxLength(64);
	searchBar->setClearButtonEnabled(true);
	searchBar->addAction(embed::getIconPixmap("zoom"), QLineEdit::LeadingPosition);
	searchBar->setObjectName(QStringLiteral("browserSearch"));  // 0.5.0 theme 3: the Browser panel shows it first

	m_descTree = new QTreeWidget( m_view );
	m_descTree->setColumnCount( 1 );
	m_descTree->header()->setVisible( false );
	m_descTree->setIndentation( 10 );
	m_descTree->setSelectionMode( QAbstractItemView::NoSelection );
	// 0.5.0 theme 3: the panel's search field steps through the tree (the current row is drawn highlighted)
	// and Enter loads the current instrument onto a new track, as its menu's "Send to new instrument track" does.
	m_descTree->setProperty("browserResults", true);
	connect(m_descTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* now, QTreeWidgetItem* was) {
		for (auto [item, current] : {std::pair{was, false}, std::pair{now, true}})
		{
			if (auto* row = item != nullptr ? qobject_cast<PluginDescWidget*>(m_descTree->itemWidget(item, 0)) : nullptr) { row->setCurrent(current); }
		}
	});
	connect(m_descTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
		if (auto* row = qobject_cast<PluginDescWidget*>(m_descTree->itemWidget(item, 0))) { row->openInNewTrack(); }
	});

	connect( searchBar, SIGNAL( textEdited( const QString& ) ),
			this, SLOT( onFilterChanged( const QString& ) ) );

	view_layout->addWidget( hint );
	view_layout->addWidget( searchBar );
	view_layout->addWidget( m_descTree );

	// Plugins are added on first show, see showEvent().
	// Resize
	m_descTree->header()->setSectionResizeMode( QHeaderView::ResizeToContents );

	// Hide empty roots
	updateRootVisibilities();
}


void PluginBrowser::showEvent( QShowEvent * event )
{
	SideBarWidget::showEvent( event );

	if( m_pluginsAdded ) { return; }
	m_pluginsAdded = true;

	addPlugins();
	updateRootVisibilities();
}


void PluginBrowser::updateRootVisibility( int rootIndex )
{
	QTreeWidgetItem * root = m_descTree->topLevelItem( rootIndex );
	root->setHidden( !root->childCount() );
}


void PluginBrowser::updateRootVisibilities()
{
	int rootCount = m_descTree->topLevelItemCount();
	for (int rootIndex = 0; rootIndex < rootCount; ++rootIndex)
	{
		updateRootVisibility( rootIndex );
	}
}


void PluginBrowser::onFilterChanged( const QString & filter )
{
	m_filter = filter;
	int rootCount = m_descTree->topLevelItemCount();
	for (int rootIndex = 0; rootIndex < rootCount; ++rootIndex)
	{
		QTreeWidgetItem * root = m_descTree->topLevelItem( rootIndex );

		int itemCount = root->childCount();
		for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
		{
			QTreeWidgetItem * item = root->child( itemIndex );
			auto descWidget = static_cast<PluginDescWidget*>(m_descTree->itemWidget(item, 0));
			if (descWidget->name().contains(filter, Qt::CaseInsensitive))
			{
				item->setHidden( false );
			}
			else
			{
				item->setHidden( true );
			}
		}
	}
}


void PluginBrowser::rebuildKeepingView()
{
	QHash<QString, bool> expanded;
	for (int rootIndex = 0; rootIndex < m_descTree->topLevelItemCount(); ++rootIndex)
	{
		const QTreeWidgetItem* root = m_descTree->topLevelItem(rootIndex);
		expanded.insert(root->text(0), root->isExpanded());
	}
	const int scroll = m_descTree->verticalScrollBar()->value();
	addPlugins();
	updateRootVisibilities();
	onFilterChanged(m_filter);
	for (int rootIndex = 0; rootIndex < m_descTree->topLevelItemCount(); ++rootIndex)
	{
		QTreeWidgetItem* root = m_descTree->topLevelItem(rootIndex);
		root->setExpanded(expanded.value(root->text(0), root->isExpanded()));
	}
	m_descTree->doItemsLayout();
	m_descTree->verticalScrollBar()->setValue(scroll);
}


void PluginBrowser::addPlugins()
{
	// Add a root node to the plugin tree with the specified `label` and return it
	const auto addRoot = [this](auto label)
	{
		const auto root = new QTreeWidgetItem();
		root->setText(0, label);
		m_descTree->addTopLevelItem(root);
		return root;
	};

	// Add the plugin identified by `key` to the tree under the root node `root`
	const auto addOne = [this](const auto& key, auto root)
	{
		const auto item = new QTreeWidgetItem();
		root->addChild(item);
		auto* widget = new PluginDescWidget(key, m_descTree);
		item->setData(0, Qt::AccessibleTextRole, widget->name());
		m_descTree->setItemWidget(item, 0, widget);
		// The tree is rebuilt once the menu that changed it has closed (it belongs to this row), the view kept.
		connect(widget, &PluginDescWidget::organised, this, [this] {
			QTimer::singleShot(0, this, [this] { rebuildKeepingView(); });
		});
	};

	// 0.5.0 L10 (MX-166): hidden plug-ins are left out; a favourite is listed under Favourites too,
	// and a filed one under its folder.
	QTreeWidgetItem* favourites = nullptr;
	QHash<QString, QTreeWidgetItem*> folders;
	const auto addPlugin = [&](const auto& key, auto root)
	{
		const pluginorganiser::Entry entry = pluginorganiser::entry(pickerorganiser::keyOf(key));
		if (entry.hidden) { return; }
		addOne(key, root);
		if (entry.favourite && favourites != nullptr) { addOne(key, favourites); }
		if (QTreeWidgetItem* folder = folders.value(entry.folder)) { addOne(key, folder); }
	};

	// Remove any existing plugins from the tree
	m_descTree->clear();

	// Fetch and sort all instrument plugin descriptors
	auto descs = getPluginFactory()->descriptors(Plugin::Type::Instrument);
	std::sort(descs.begin(), descs.end(),
		[](auto d1, auto d2)
		{
			return qstricmp(d1->displayName, d2->displayName) < 0;
		}
	);

	// Add a root node to the tree for native plugins
	// Favourites first, when there are any (the tree's widgets cannot be moved once placed).
	const QMap<QString, pluginorganiser::Entry> organised = pluginorganiser::entries();
	if (std::any_of(organised.begin(), organised.end(), [](const pluginorganiser::Entry& e) { return e.favourite && !e.hidden; }))
	{
		favourites = addRoot(tr("Favourites"));
		favourites->setExpanded(true);
	}
	const auto nativeRoot = addRoot("Zene Studio");
	nativeRoot->setExpanded(true);
	// The folders next, sorted by name (a root cannot be moved once its plug-ins' widgets are placed).
	for (const QString& folder : listedFolders(organised)) { folders.insert(folder, addRoot(folder)); }

	// Add all of the descriptors to the tree
	for (const auto desc : descs)
	{
		if (desc->subPluginFeatures)
		{
			// Fetch and sort all subplugins for this plugin descriptor
			auto subPluginKeys = Plugin::Descriptor::SubPluginFeatures::KeyList{};
			desc->subPluginFeatures->listSubPluginKeys(desc, subPluginKeys);
			std::sort(subPluginKeys.begin(), subPluginKeys.end(),
				[](const auto& l, const auto& r)
				{
					return QString::compare(l.displayName(), r.displayName(), Qt::CaseInsensitive) < 0;
				}
			);

			// Create a root node for this plugin and add the subplugins under it
			const auto root = addRoot(desc->displayName);
			for (const auto& key : subPluginKeys) { addPlugin(key, root); }
		}
		else
		{
			addPlugin(Plugin::Descriptor::SubPluginFeatures::Key(desc, desc->name), nativeRoot);
		}
	}
}




PluginDescWidget::PluginDescWidget(const PluginKey &_pk,
							QWidget * _parent ) :
	QWidget( _parent ),
	m_pluginKey( _pk ),
	m_logo( _pk.logo()->pixmap() ),
	m_mouseOver( false )
{
	setFixedHeight( DEFAULT_HEIGHT );
	setMouseTracking( true );
	setCursor( Qt::PointingHandCursor );
	setAccessibleName(_pk.displayName());
	setToolTip(_pk.desc->subPluginFeatures
		? _pk.description()
		: tr(_pk.desc->description));
}




QString PluginDescWidget::name() const
{
	return m_pluginKey.displayName();
}




void PluginDescWidget::paintEvent( QPaintEvent * )
{

	QPainter p( this );

	// Paint everything according to the style sheet
	QStyleOption o;
	o.initFrom( this );
	style()->drawPrimitive( QStyle::PE_Widget, &o, &p, this );

	// Draw the rest
	const int s = 16 + ( 32 * ( qBound( 24, height(), 60 ) - 24 ) ) /
								( 60 - 24 );
	const QSize logo_size( s, s );
	QPixmap logo = m_logo.scaled( logo_size, Qt::KeepAspectRatio,
						Qt::SmoothTransformation );
	p.drawPixmap( 4, 4, logo );

	QFont f = p.font();
	if (m_mouseOver || m_current)
	{
		f.setBold( true );
	}
	if (m_current)  // 0.5.0 theme 3: the row Enter loads
	{
		p.setPen(palette().color(QPalette::Highlight));
		p.drawRect(rect().adjusted(0, 0, -1, -1));
		p.setPen(palette().color(QPalette::WindowText));
	}

	p.setFont( f );
	p.drawText( 10 + logo_size.width(), 15, m_pluginKey.displayName());
}


#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
void PluginDescWidget::enterEvent(QEnterEvent* event)
#else
void PluginDescWidget::enterEvent(QEvent* event)
#endif
{
	m_mouseOver = true;

	QWidget::enterEvent(event);
}




void PluginDescWidget::leaveEvent( QEvent * _e )
{
	m_mouseOver = false;

	QWidget::leaveEvent( _e );
}




void PluginDescWidget::mousePressEvent( QMouseEvent * _me )
{
	Engine::setDndPluginKey(&m_pluginKey);
	if ( _me->button() == Qt::LeftButton )
	{
		new StringPairDrag("instrument",
			QString::fromUtf8(m_pluginKey.desc->name), m_logo, this);
		leaveEvent( _me );
	}
}


void PluginDescWidget::contextMenuEvent(QContextMenuEvent* e)
{
	QMenu contextMenu(this);
	contextMenu.addAction(
		tr("Send to new instrument track"),
		[=, this]{ openInNewInstrumentTrack(m_pluginKey.desc->name); }
	);
	contextMenu.addSeparator();
	pickerorganiser::fillKeyMenu(&contextMenu, pickerorganiser::keyOf(m_pluginKey), this, [this] { emit organised(); });
	contextMenu.exec(e->globalPos());
}


void PluginDescWidget::setCurrent(bool current)
{
	m_current = current;
	update();
}


void PluginDescWidget::openInNewTrack()
{
	Engine::setDndPluginKey(&m_pluginKey);  // the loader picks a sub-plugin's key from here, as a drag leaves it
	openInNewInstrumentTrack(m_pluginKey.desc->name);
}


void PluginDescWidget::openInNewInstrumentTrack(QString value)
{
	TrackContainer* tc = Engine::getSong();
	auto it = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, tc));
	auto ilt = new InstrumentLoaderThread(this, it, value);
	ilt->start();
}


} // namespace lmms::gui
