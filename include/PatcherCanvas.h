/*
 * PatcherCanvas.h - the device-chain patcher canvas: a chain as nodes and cables (Q050-087)
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_PATCHER_CANVAS_H
#define LMMS_GUI_PATCHER_CANVAS_H

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

#include "PatcherGraph.h"
#include "lmms_export.h"

class QLabel;
class QPainter;

namespace lmms::gui
{

/*! The chain of one track or mixer channel drawn as its patcher graph: the input, one node per device in chain order,
 *  the cables in force, and the node the chain leaves through. It draws what patcher.get_state reports and edits
 *  through the registered commands only - patcher.set_wiring for cables, plugin.load / plugin.move / plugin.unload
 *  for devices - each one undo step, refreshing from patcher.get_state after every edit, so what is on screen is
 *  always what an agent reads.
 *
 *  Mouse: drag from a node's output port to another node's input port to connect; Shift-click a cable and drag to
 *  give its source a second destination (the first cable stays); click a cable and press Delete to disconnect it;
 *  drag a node sideways to reorder the chain; right-click a node or a cable for its menu. The wiring can only be
 *  edited where the engine routes the chain through its graph (patcher.get_state's `editable`); on any other chain
 *  the canvas shows the cables read-only and says why, while adding, reordering and removing devices work as ever. */
class LMMS_EXPORT PatcherCanvas : public QWidget
{
	Q_OBJECT
public:
	explicit PatcherCanvas(const QString& target, QWidget* parent = nullptr);

	const QString& target() const { return m_target; }
	const PatcherGraph& graph() const { return m_graph; }
	//! Read patcher.get_state again and redraw.
	void refresh();

	//! Where a node and its ports are drawn (widget coordinates), for the mouse and for the tests.
	QRectF nodeRect(int node) const;
	QPointF outputPort(int node, int port = 0) const;
	QPointF inputPort(int node, int port = 0) const;
	//! The point of cable \a edge nearest to its middle, where a click lands on it.
	QPointF cableMiddle(int edge) const;
	int selectedEdge() const { return m_selectedEdge; }
	int selectedNode() const { return m_selectedNode; }

public slots:
	//! The device picker, then plugin.load at the end of the chain.
	void addDevice();
	//! plugin.move of the selected device by \a delta slots.
	void moveSelected(int delta);
	//! plugin.unload of the selected device.
	void removeSelected();
	//! patcher.set_wiring without the selected cable.
	void disconnectSelected();
	//! patcher.set_wiring with no edges: the derived, linear wiring.
	void resetWiring();
	//! The selected device becomes the node the chain leaves through.
	void useSelectedAsOutput();

signals:
	void changed();

protected:
	QSize sizeHint() const override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	enum class Drag { None, Cable, Node };

	int nodeAt(const QPointF& point) const;
	int outputPortAt(const QPointF& point, int* port) const;
	int inputPortAt(const QPointF& point, int* port) const;
	int edgeAt(const QPointF& point) const;
	QPointF cablePoint(const PatcherEdge& edge, double t) const;
	void beginCable(const QString& sourceRef, int sourcePort);
	void finishCable(const QPointF& point);
	void finishNodeDrag(const QPointF& point);
	void applyWiring(const PatcherGraph& wiring);
	bool dispatch(const QString& command, const QJsonObject& args);
	QString effectId(int node) const;
	void paintCables(QPainter& painter) const;
	void paintNodes(QPainter& painter) const;
	void paintDraggedCable(QPainter& painter) const;

	QString m_target;
	PatcherGraph m_graph;
	QStringList m_deviceNames;
	Drag m_drag = Drag::None;
	QString m_cableSource;
	int m_cableSourcePort = 0;
	QPointF m_cursor;
	QPointF m_pressed;
	int m_selectedNode = -1;
	int m_selectedEdge = -1;
};

//! The window around a canvas: its target's name, an Add device button, Move Left / Move Right / Remove for the
//! selected device, Reset Wiring, and the reason when the wiring cannot be edited.
class LMMS_EXPORT PatcherCanvasWindow : public QWidget
{
	Q_OBJECT
public:
	explicit PatcherCanvasWindow(const QString& target, QWidget* parent = nullptr);
	PatcherCanvas* canvas() const { return m_canvas; }

private:
	PatcherCanvas* m_canvas;
	QLabel* m_note;
};

//! Opens (or raises) the canvas window of \a target ("trk-<n>" or "ch-<n>"); one window per target.
LMMS_EXPORT PatcherCanvasWindow* showPatcherCanvas(const QString& target, QWidget* parent = nullptr);

} // namespace lmms::gui

#endif // LMMS_GUI_PATCHER_CANVAS_H
