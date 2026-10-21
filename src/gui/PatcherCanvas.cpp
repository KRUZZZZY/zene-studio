/*
 * PatcherCanvas.cpp - the device-chain patcher canvas: a chain as nodes and cables (Q050-087)
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "PatcherCanvas.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>
#include <cmath>

#include "CommandForm.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePicker.h"
#include "Effect.h"
#include "EffectChain.h"

namespace lmms::gui
{

namespace
{

constexpr double Margin = 28.0;
constexpr double NodeWidth = 112.0;
constexpr double NodeHeight = 56.0;
constexpr double NodeGap = 64.0;
constexpr double Pitch = NodeWidth + NodeGap;
constexpr double TopY = 52.0;
constexpr double PortRadius = 6.0;
constexpr double PortGrab = 10.0;
constexpr double PortSpacing = 16.0;
constexpr double CableGrab = 6.0;
constexpr double DragThreshold = 16.0;
constexpr int CableSamples = 28;

} // namespace

PatcherCanvas::PatcherCanvas(const QString& target, QWidget* parent)
	: QWidget(parent)
	, m_target(target)
{
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAccessibleName(tr("Chain canvas of %1").arg(target));
	refresh();
}

QSize PatcherCanvas::sizeHint() const
{
	return QSize(static_cast<int>(2 * Margin + m_graph.nodes.size() * Pitch), 190);
}

void PatcherCanvas::refresh()
{
	m_deviceNames.clear();
	ControlTarget resolved;
	ControlResult ignored;
	if (resolveControlTarget(m_target, &resolved, &ignored))
	{
		for (const Effect* effect : resolved.chain->effects()) { m_deviceNames.append(effect->displayName()); }
	}
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("patcher.get_state"),
		QJsonObject{{QStringLiteral("target"), m_target}});
	m_graph = state.ok ? PatcherGraph::fromState(state.result, m_deviceNames) : PatcherGraph();
	m_selectedNode = std::min(m_selectedNode, static_cast<int>(m_graph.nodes.size()) - 1);
	m_selectedEdge = std::min(m_selectedEdge, static_cast<int>(m_graph.edges.size()) - 1);
	updateGeometry();
	update();
}

// ------------------------------------------------------------------ geometry

QRectF PatcherCanvas::nodeRect(int node) const
{
	const int ports = node >= 0 && node < m_graph.nodes.size()
		? std::max(m_graph.nodes.at(node).inputs, m_graph.nodes.at(node).outputs) : 1;
	return QRectF(Margin + node * Pitch, TopY, NodeWidth, NodeHeight + (ports - 1) * PortSpacing);
}

QPointF PatcherCanvas::outputPort(int node, int port) const
{
	const QRectF rect = nodeRect(node);
	return QPointF(rect.right(), rect.top() + NodeHeight / 2 + port * PortSpacing);
}

QPointF PatcherCanvas::inputPort(int node, int port) const
{
	const QRectF rect = nodeRect(node);
	return QPointF(rect.left(), rect.top() + NodeHeight / 2 + port * PortSpacing);
}

QPointF PatcherCanvas::cablePoint(const PatcherEdge& edge, double t) const
{
	const QPointF start = outputPort(m_graph.indexOfNode(edge.from), edge.fromPort);
	const QPointF end = inputPort(m_graph.indexOfNode(edge.to), edge.toPort);
	const double reach = std::max(40.0, std::abs(end.x() - start.x()) / 2);
	const QPointF c1(start.x() + reach, start.y());
	const QPointF c2(end.x() - reach, end.y());
	const double u = 1.0 - t;
	return u * u * u * start + 3 * u * u * t * c1 + 3 * u * t * t * c2 + t * t * t * end;
}

QPointF PatcherCanvas::cableMiddle(int edge) const
{
	return edge >= 0 && edge < m_graph.edges.size() ? cablePoint(m_graph.edges.at(edge), 0.5) : QPointF();
}

int PatcherCanvas::nodeAt(const QPointF& point) const
{
	for (int i = 0; i < m_graph.nodes.size(); ++i)
	{
		if (nodeRect(i).contains(point)) { return i; }
	}
	return -1;
}

int PatcherCanvas::outputPortAt(const QPointF& point, int* port) const
{
	for (int i = 0; i < m_graph.nodes.size(); ++i)
	{
		for (int p = 0; p < m_graph.nodes.at(i).outputs; ++p)
		{
			if (QLineF(point, outputPort(i, p)).length() <= PortGrab) { *port = p; return i; }
		}
	}
	return -1;
}

int PatcherCanvas::inputPortAt(const QPointF& point, int* port) const
{
	for (int i = 1; i < m_graph.nodes.size(); ++i)  // the chain's input node takes no cable
	{
		for (int p = 0; p < m_graph.nodes.at(i).inputs; ++p)
		{
			if (QLineF(point, inputPort(i, p)).length() <= PortGrab) { *port = p; return i; }
		}
	}
	return -1;
}

int PatcherCanvas::edgeAt(const QPointF& point) const
{
	for (int e = 0; e < m_graph.edges.size(); ++e)
	{
		QPointF previous = cablePoint(m_graph.edges.at(e), 0.0);
		for (int s = 1; s <= CableSamples; ++s)
		{
			const QPointF next = cablePoint(m_graph.edges.at(e), static_cast<double>(s) / CableSamples);
			const QLineF segment(previous, next);
			const QPointF toPoint = point - previous;
			const double along = segment.length() > 0 ? QPointF::dotProduct(toPoint, next - previous) / (segment.length() * segment.length()) : 0.0;
			const QPointF nearest = previous + std::clamp(along, 0.0, 1.0) * (next - previous);
			if (QLineF(point, nearest).length() <= CableGrab) { return e; }
			previous = next;
		}
	}
	return -1;
}

// ------------------------------------------------------------------ painting

void PatcherCanvas::paintCables(QPainter& painter) const
{
	for (int e = 0; e < m_graph.edges.size(); ++e)
	{
		const PatcherEdge& edge = m_graph.edges.at(e);
		QPainterPath path(cablePoint(edge, 0.0));
		for (int s = 1; s <= CableSamples; ++s) { path.lineTo(cablePoint(edge, static_cast<double>(s) / CableSamples)); }
		painter.setPen(QPen(e == m_selectedEdge ? QColor(255, 200, 80) : QColor(120, 200, 160), e == m_selectedEdge ? 3.5 : 2.5));
		painter.drawPath(path);
	}
}

void PatcherCanvas::paintDraggedCable(QPainter& painter) const
{
	if (m_drag != Drag::Cable) { return; }
	const int source = m_graph.indexOfNode(m_cableSource);
	if (source < 0) { return; }
	painter.setPen(QPen(QColor(255, 200, 80), 2.0, Qt::DashLine));
	painter.drawLine(outputPort(source, m_cableSourcePort), m_cursor);
}

void PatcherCanvas::paintNodes(QPainter& painter) const
{
	for (int i = 0; i < m_graph.nodes.size(); ++i)
	{
		const PatcherNode& node = m_graph.nodes.at(i);
		const QRectF rect = nodeRect(i);
		painter.setPen(QPen(i == m_selectedNode ? QColor(255, 200, 80) : QColor(150, 150, 150), i == m_selectedNode ? 2.5 : 1.0));
		painter.setBrush(node.isInput() ? QColor(52, 60, 70) : QColor(44, 48, 54));
		painter.drawRoundedRect(rect, 6, 6);
		painter.setPen(QColor(235, 235, 235));
		painter.drawText(rect.adjusted(14, 4, -14, -4), Qt::AlignCenter | Qt::TextWordWrap,
			painter.fontMetrics().elidedText(node.label, Qt::ElideRight, static_cast<int>(rect.width()) - 28));
		painter.setBrush(QColor(120, 200, 160));
		painter.setPen(Qt::NoPen);
		for (int p = 0; p < node.outputs; ++p) { painter.drawEllipse(outputPort(i, p), PortRadius, PortRadius); }
		painter.setBrush(QColor(110, 160, 220));
		for (int p = 0; !node.isInput() && p < node.inputs; ++p) { painter.drawEllipse(inputPort(i, p), PortRadius, PortRadius); }
		if (node.ref == m_graph.output)
		{
			painter.setPen(QColor(200, 200, 200));
			painter.drawText(QPointF(rect.right() + 8, rect.top() + NodeHeight / 2 + 4), tr("OUT"));
		}
	}
}

void PatcherCanvas::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.fillRect(rect(), QColor(30, 33, 37));
	paintCables(painter);
	paintNodes(painter);
	paintDraggedCable(painter);
}

// ------------------------------------------------------------------ editing (every edit is a registered command)

bool PatcherCanvas::dispatch(const QString& command, const QJsonObject& args)
{
	const bool ok = runCommand(this, CommandCall{command, args, {}, QString(), false}).ok;
	refresh();
	emit changed();
	return ok;
}

QString PatcherCanvas::effectId(int node) const
{
	ControlTarget resolved;
	ControlResult ignored;
	if (node < 1 || !resolveControlTarget(m_target, &resolved, &ignored)) { return {}; }
	const std::vector<Effect*>& effects = resolved.chain->effects();
	return node - 1 < static_cast<int>(effects.size()) ? control::effectIdOf(effects[static_cast<std::size_t>(node - 1)]) : QString();
}

void PatcherCanvas::applyWiring(const PatcherGraph& wiring)
{
	dispatch(QStringLiteral("patcher.set_wiring"), QJsonObject{{QStringLiteral("target"), m_target},
		{QStringLiteral("edges"), wiring.edgesJson()}, {QStringLiteral("output"), wiring.output}});
}

void PatcherCanvas::addDevice()
{
	const QString device = DevicePicker::pick(this, DevicePicker::Kind::Effect, tr("Add a device to %1").arg(m_target));
	if (device.isEmpty()) { return; }
	dispatch(QStringLiteral("plugin.load"), QJsonObject{{QStringLiteral("target"), m_target}, {QStringLiteral("device"), device}});
}

void PatcherCanvas::moveSelected(int delta)
{
	const QString id = effectId(m_selectedNode);
	if (id.isEmpty()) { return; }
	const int count = static_cast<int>(m_graph.nodes.size()) - 1;
	const int position = std::clamp(m_selectedNode - 1 + delta, 0, count - 1);
	const int selected = position + 1;
	if (dispatch(QStringLiteral("plugin.move"), QJsonObject{{QStringLiteral("target"), m_target}, {QStringLiteral("plugin"), id},
			{QStringLiteral("position"), position}}))
	{
		m_selectedNode = selected;
		update();
	}
}

void PatcherCanvas::removeSelected()
{
	const QString id = effectId(m_selectedNode);
	if (id.isEmpty()) { return; }
	m_selectedNode = -1;
	dispatch(QStringLiteral("plugin.unload"), QJsonObject{{QStringLiteral("target"), m_target}, {QStringLiteral("plugin"), id}});
}

void PatcherCanvas::disconnectSelected()
{
	if (m_selectedEdge < 0) { return; }
	const PatcherGraph wiring = m_graph.disconnected(m_selectedEdge);
	m_selectedEdge = -1;
	applyWiring(wiring);
}

void PatcherCanvas::resetWiring()
{
	dispatch(QStringLiteral("patcher.set_wiring"), QJsonObject{{QStringLiteral("target"), m_target}});
}

void PatcherCanvas::useSelectedAsOutput()
{
	if (m_selectedNode < 1 || m_selectedNode >= m_graph.nodes.size()) { return; }
	applyWiring(m_graph.withOutput(m_graph.nodes.at(m_selectedNode).ref));
}

// ------------------------------------------------------------------ mouse and keys

void PatcherCanvas::beginCable(const QString& sourceRef, int sourcePort)
{
	m_drag = Drag::Cable;
	m_cableSource = sourceRef;
	m_cableSourcePort = sourcePort;
}

void PatcherCanvas::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }
	setFocus();
	m_pressed = m_cursor = event->localPos();
	m_drag = Drag::None;
	int port = 0;
	if (const int source = outputPortAt(m_pressed, &port); source >= 0)
	{
		beginCable(m_graph.nodes.at(source).ref, port);
	}
	else if (const int edge = edgeAt(m_pressed); edge >= 0)
	{
		m_selectedEdge = edge;
		m_selectedNode = -1;
		// Shift on a cable: a second cable from the same output, the first one stays.
		if (event->modifiers() & Qt::ShiftModifier) { beginCable(m_graph.edges.at(edge).from, m_graph.edges.at(edge).fromPort); }
	}
	else
	{
		m_selectedNode = nodeAt(m_pressed);
		m_selectedEdge = -1;
		if (m_selectedNode > 0) { m_drag = Drag::Node; }
	}
	update();
}

void PatcherCanvas::mouseMoveEvent(QMouseEvent* event)
{
	m_cursor = event->localPos();
	if (m_drag != Drag::None) { update(); }
}

void PatcherCanvas::finishCable(const QPointF& point)
{
	int port = 0;
	const int target = inputPortAt(point, &port);
	const int source = m_graph.indexOfNode(m_cableSource);
	if (target < 0 || source < 0 || !m_graph.editable) { return; }
	PatcherEdge edge;
	edge.from = m_cableSource;
	edge.fromPort = m_cableSourcePort;
	edge.to = m_graph.nodes.at(target).ref;
	edge.toPort = port;
	if (m_graph.indexOfEdge(edge) < 0) { applyWiring(m_graph.connected(edge)); }
}

void PatcherCanvas::finishNodeDrag(const QPointF& point)
{
	if (std::abs(point.x() - m_pressed.x()) < DragThreshold || m_selectedNode < 1) { return; }
	const int slot = static_cast<int>(std::lround((point.x() - Margin - NodeWidth / 2) / Pitch));
	moveSelected(std::clamp(slot, 1, static_cast<int>(m_graph.nodes.size()) - 1) - m_selectedNode);
}

void PatcherCanvas::mouseReleaseEvent(QMouseEvent* event)
{
	const Drag drag = m_drag;
	m_drag = Drag::None;
	if (event->button() == Qt::LeftButton && drag == Drag::Cable) { finishCable(event->localPos()); }
	else if (event->button() == Qt::LeftButton && drag == Drag::Node) { finishNodeDrag(event->localPos()); }
	update();
}

void PatcherCanvas::keyPressEvent(QKeyEvent* event)
{
	const bool remove = event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace;
	if (remove && m_selectedEdge >= 0) { disconnectSelected(); }
	else if (remove && m_selectedNode > 0) { removeSelected(); }
	else { QWidget::keyPressEvent(event); }
}

void PatcherCanvas::contextMenuEvent(QContextMenuEvent* event)
{
	const QPointF point = event->pos();
	QMenu menu(this);
	if (const int edge = edgeAt(point); edge >= 0)
	{
		m_selectedEdge = edge;
		QAction* disconnect = menu.addAction(tr("Disconnect"));
		disconnect->setProperty("controlCommand", QStringLiteral("patcher.set_wiring"));
		disconnect->setEnabled(m_graph.editable);
		connect(disconnect, &QAction::triggered, this, &PatcherCanvas::disconnectSelected);
	}
	else if (const int node = nodeAt(point); node > 0)
	{
		m_selectedNode = node;
		const auto add = [&](const QString& text, const char* command, auto slot, bool enabled) {
			QAction* action = menu.addAction(text);
			action->setProperty("controlCommand", QString::fromLatin1(command));
			action->setEnabled(enabled);
			connect(action, &QAction::triggered, this, slot);
		};
		add(tr("Use as Output"), "patcher.set_wiring", [this] { useSelectedAsOutput(); }, m_graph.editable);
		add(tr("Move Left"), "plugin.move", [this] { moveSelected(-1); }, node > 1);
		add(tr("Move Right"), "plugin.move", [this] { moveSelected(1); }, node < m_graph.nodes.size() - 1);
		add(tr("Remove"), "plugin.unload", [this] { removeSelected(); }, true);
	}
	if (!menu.isEmpty()) { menu.exec(event->globalPos()); }
	update();
}

// ------------------------------------------------------------------ the window

PatcherCanvasWindow::PatcherCanvasWindow(const QString& target, QWidget* parent)
	: QWidget(parent, Qt::Window)
	, m_canvas(new PatcherCanvas(target, this))
	, m_note(new QLabel(this))
{
	setWindowTitle(tr("Chain canvas - %1").arg(target));
	auto* buttons = new QHBoxLayout;
	const auto button = [&](const QString& text, const QString& name, auto slot) {
		auto* b = new QPushButton(text, this);
		b->setObjectName(name);
		connect(b, &QPushButton::clicked, m_canvas, slot);
		buttons->addWidget(b);
	};
	button(tr("Add device..."), QStringLiteral("patcherAddDevice"), &PatcherCanvas::addDevice);
	button(tr("Move Left"), QStringLiteral("patcherMoveLeft"), [this] { m_canvas->moveSelected(-1); });
	button(tr("Move Right"), QStringLiteral("patcherMoveRight"), [this] { m_canvas->moveSelected(1); });
	button(tr("Remove"), QStringLiteral("patcherRemove"), &PatcherCanvas::removeSelected);
	button(tr("Reset Wiring"), QStringLiteral("patcherResetWiring"), &PatcherCanvas::resetWiring);
	buttons->addStretch();

	m_note->setWordWrap(true);
	m_note->setObjectName(QStringLiteral("patcherNote"));
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(buttons);
	layout->addWidget(m_canvas, 1);
	layout->addWidget(m_note);
	const auto showNote = [this] {
		const PatcherGraph& graph = m_canvas->graph();
		m_note->setText(graph.editable ? tr("Drag from an output port to an input port to connect. Shift-click a cable to give it a second destination.")
			: tr("The cables are shown but cannot be changed here: %1").arg(graph.reason));
	};
	connect(m_canvas, &PatcherCanvas::changed, this, showNote);
	showNote();
	resize(sizeHint().expandedTo(QSize(560, 300)));
}

PatcherCanvasWindow* showPatcherCanvas(const QString& target, QWidget* parent)
{
	const QString name = QStringLiteral("patcherCanvas-") + target;
	QWidget* root = parent != nullptr ? parent->window() : nullptr;
	auto* window = root != nullptr ? root->findChild<PatcherCanvasWindow*>(name) : nullptr;
	if (window == nullptr)
	{
		window = new PatcherCanvasWindow(target, root);
		window->setObjectName(name);
		window->setAttribute(Qt::WA_DeleteOnClose);
	}
	window->show();
	window->raise();
	return window;
}

} // namespace lmms::gui
