/*
 * Lv2ControlBase.cpp - Lv2 control base class
 *
 * Copyright (c) 2018-2023 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include "Lv2ControlBase.h"

#include <QDomDocument>
#include <QDomElement>

#ifdef LMMS_HAVE_LV2

#include <algorithm>
#include <QDebug>
#include <QtGlobal>

#include "Engine.h"
#include "lmms_constants.h"
#include "Lv2Manager.h"
#include "Lv2Proc.h"
#include "Song.h"


namespace lmms
{

namespace
{

const LilvPlugin* pluginForUri(const QString& uri)
{
	Lv2Manager* manager = Engine::getLv2Manager();
	return manager != nullptr ? manager->getPlugin(uri) : nullptr;
}

//! The identity of an LV2 plug-in in a load report: its URI - the stable name an LV2
//! plug-in is known by, the same name the catalogue and the project file use.
QString lv2PluginUri(const LilvPlugin* plugin)
{
	if (plugin == nullptr) { return QString(); }
	const LilvNode* node = lilv_plugin_get_uri(plugin);
	const char* uri = node != nullptr ? lilv_node_as_uri(node) : nullptr;
	return uri != nullptr ? QString::fromUtf8(uri) : QString();
}

} // namespace


Plugin::Type Lv2ControlBase::check(const LilvPlugin *plugin,
	std::vector<PluginIssue> &issues)
{
	// for some reason, all checks can be done by one processor...
	return Lv2Proc::check(plugin, issues);
}




Lv2ControlBase::Lv2ControlBase(Model* that, const QString &uri) :
	m_plugin(pluginForUri(uri))
{
	if (m_plugin)
	{
		init(that);
	}
	else
	{
		qCritical() << "No Lv2 plugin found for URI" << uri;
		throw std::runtime_error("No Lv2 plugin found for given URI");
	}
}




Lv2ControlBase::~Lv2ControlBase() = default;




void Lv2ControlBase::init(Model* meAsModel)
{
	int channelsLeft = DEFAULT_CHANNELS; // LMMS plugins are stereo
	while (channelsLeft > 0)
	{
		std::unique_ptr<Lv2Proc> newOne = std::make_unique<Lv2Proc>(m_plugin, meAsModel);
		channelsLeft -= std::max(
			1 + static_cast<bool>(newOne->inPorts().m_right),
			1 + static_cast<bool>(newOne->outPorts().m_right));
		Q_ASSERT(channelsLeft >= 0);
		m_procs.push_back(std::move(newOne));
	}
	m_channelsPerProc = DEFAULT_CHANNELS / m_procs.size();
	linkAllModels();
}




void Lv2ControlBase::shutdown()
{
	// currently nothing to do here
}




void Lv2ControlBase::reload()
{
	for (const auto& c : m_procs) { c->reload(); }
}




LinkedModelGroup *Lv2ControlBase::getGroup(std::size_t idx)
{
	return (m_procs.size() > idx) ? m_procs[idx].get() : nullptr;
}




const LinkedModelGroup *Lv2ControlBase::getGroup(std::size_t idx) const
{
	return (m_procs.size() > idx) ? m_procs[idx].get() : nullptr;
}




void Lv2ControlBase::copyModelsFromLmms() {
	for (const auto& c : m_procs) { c->copyModelsFromCore(); }
}




void Lv2ControlBase::copyModelsToLmms() const
{
	for (const auto& c : m_procs) { c->copyModelsToCore(); }
}




void Lv2ControlBase::copyBuffersFromLmms(const SampleFrame* buf, f_cnt_t frames) {
	unsigned firstChan = 0; // tell the procs which channels they shall read from
	for (const auto& c : m_procs) 
	{
		c->copyBuffersFromCore(buf, firstChan, m_channelsPerProc, frames);
		firstChan += m_channelsPerProc;
	}
}




void Lv2ControlBase::copyBuffersToLmms(SampleFrame* buf, f_cnt_t frames) const {
	unsigned firstChan = 0; // tell the procs which channels they shall write to
	for (const auto& c : m_procs) {
		c->copyBuffersToCore(buf, firstChan, m_channelsPerProc, frames);
		firstChan += m_channelsPerProc;
	}
}




void Lv2ControlBase::run(f_cnt_t frames) {
	for (const auto& c : m_procs) { c->run(frames); }
}




void Lv2ControlBase::saveSettings(QDomDocument &doc, QDomElement &that)
{
	LinkedModelGroups::saveSettings(doc, that);

	// 0.5.0 (engine audit E13): the plug-in's own state, one element per instance, beside the
	// control ports - an older build ignores the element and keeps the ports.
	for (std::size_t i = 0; i < m_procs.size(); ++i)
	{
		const QString state = m_procs[i]->saveState();
		if (state.isEmpty()) { continue; }
		QDomElement element = doc.createElement(QStringLiteral("lv2state"));
		element.setAttribute(QStringLiteral("instance"), static_cast<int>(i));
		element.appendChild(doc.createCDATASection(state));
		that.appendChild(element);
	}
}




//! The processor instance a <lv2state> element names, or -1 when the element is malformed
//! (missing or non-numeric instance attribute) or names an instance beyond @param procCount.
int lv2StateInstanceIndex(const QDomElement& element, std::size_t procCount)
{
	bool ok = false;
	const int instance = element.attribute(QStringLiteral("instance")).toInt(&ok);
	// A missing or non-numeric attribute is malformed state, not instance zero (BUG 9).
	if (!ok || instance < 0 || static_cast<std::size_t>(instance) >= procCount) { return -1; }
	return instance;
}

void Lv2ControlBase::loadSettings(const QDomElement &that)
{
	LinkedModelGroups::loadSettings(that);

	for (QDomElement element = that.firstChildElement(QStringLiteral("lv2state")); !element.isNull();
		element = element.nextSiblingElement(QStringLiteral("lv2state")))
	{
		const int instance = lv2StateInstanceIndex(element, m_procs.size());
		if (instance >= 0)
		{
			const QString document = element.text();
			// An empty element carries no state, so nothing can be lost and nothing
			// is reported; every other document is restored, and a document the
			// plug-in refuses is reported.
			if (!document.isEmpty()
				&& !m_procs[static_cast<std::size_t>(instance)]->restoreState(document))
			{
				// The state is lost: the processor stays at its default. A load that
				// says nothing about it drops what the project saved in silence
				// (cluster new-p17-83), so the failure is reported through the
				// project load's error path, naming the plug-in it belongs to.
				reportStateRestoreFailure(instance);
			}
		}
	}
}




void Lv2ControlBase::reportStateRestoreFailure(int instance)
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	song->collectError(QObject::tr("LV2 plug-in %1: its state could not be restored (instance %2), "
		"so the plug-in keeps its default state.")
		.arg(lv2PluginUri(m_plugin)).arg(instance));
}




void Lv2ControlBase::loadFile(const QString &file)
{
	(void)file;
}




int Lv2ControlBase::latencyFrames() const
{
	int frames = 0;
	for (const auto& proc : m_procs) { frames = std::max(frames, proc->latencyFrames()); }
	return frames;
}

std::size_t Lv2ControlBase::controlCount() const {
	std::size_t res = 0;
	for (const auto& c : m_procs) { res += c->controlCount(); }
	return res;
}




bool Lv2ControlBase::hasNoteInput() const
{
	return std::any_of(m_procs.begin(), m_procs.end(),
		[](const auto& c) { return c->hasNoteInput(); });
}




void Lv2ControlBase::handleMidiInputEvent(const MidiEvent &event,
	const TimePos &time, f_cnt_t offset)
{
	for (const auto& c : m_procs) { c->handleMidiInputEvent(event, time, offset); }
}


} // namespace lmms


#endif // LMMS_HAVE_LV2
