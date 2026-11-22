/*
 * LinkedModelGroups.cpp - base classes for groups of linked models
 *
 * Copyright (c) 2019-2019 Johannes Lorenz <j.git$$$lorenz-ho.me, $$$=@>
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

#include <QDebug>
#include <QDomDocument>
#include <QDomElement>

#include "LinkedModelGroups.h"


#include "AutomatableModel.h"



namespace lmms
{

/*
	LinkedModelGroup
*/


void LinkedModelGroup::linkControls(LinkedModelGroup *other)
{
	foreach_model([&other](const std::string& id, ModelInfo& inf)
	{
		auto itr2 = other->m_models.find(id);
		Q_ASSERT(itr2 != other->m_models.end());
		inf.m_model->linkToModel(itr2->second.m_model);
	});
}




void LinkedModelGroup::saveValues(QDomDocument &doc, QDomElement &that)
{
	foreach_model([&doc, &that](const std::string& , ModelInfo& inf)
	{
		inf.m_model->saveSettings(doc, that, /*m_models[idx].m_name*/ inf.m_name); /* TODO: m_name useful */
	});
}




void LinkedModelGroup::loadValues(const QDomElement &that)
{
	foreach_model([&that](const std::string& , ModelInfo& inf)
	{
		// try to load, if it fails, this will load a sane initial value
		inf.m_model->loadSettings(that, /*m_models()[idx].m_name*/ inf.m_name); /* TODO: m_name useful */
	});
}




void LinkedModelGroup::addModel(AutomatableModel *model, const QString &name)
{
	// Models are keyed by name. A second model under a taken name cannot be stored, so it is
	// refused before anything is connected or announced; otherwise its destruction would erase
	// the first model from the group.
	if(containsModel(name))
	{
		qWarning() << "LinkedModelGroup: refusing a second model named" << name;
		return;
	}
	model->setObjectName(name);
	// the name the model is registered under; a later rename must not change it
	const std::string key(name.toUtf8().data());
	m_models.emplace(key, ModelInfo(name, model));
	connect(model, &AutomatableModel::destroyed,
				this, [this, model, key](jo_id_t){
					eraseRegisteredModel(key, model);
				},
				Qt::DirectConnection);

	// View needs to create another child view, e.g. a new knob:
	emit modelAdded(model);
	emit dataChanged();
}




void LinkedModelGroup::removeControl(AutomatableModel* mdl)
{
	eraseRegisteredModel(mdl->objectName().toStdString(), mdl);
}




void LinkedModelGroup::eraseRegisteredModel(const std::string& key, AutomatableModel* model)
{
	const auto itr = m_models.find(key);
	if (itr == m_models.end() || itr->second.m_model != model) { return; }

	emit modelRemoved(model);
	m_models.erase(key);
}




bool LinkedModelGroup::eraseModel(const QString& name)
{
	return m_models.erase(name.toStdString()) > 0;
}




void LinkedModelGroup::clearModels()
{
	m_models.clear();
}




bool LinkedModelGroup::containsModel(const QString &name) const
{
	return m_models.find(name.toStdString()) != m_models.end();
}




/*
	LinkedModelGroups
*/



void LinkedModelGroups::linkAllModels()
{
	LinkedModelGroup* first = getGroup(0);
	for (size_t i = 1; auto cur = getGroup(i); ++i)
	{
		first->linkControls(cur);
	}
}




void LinkedModelGroups::saveSettings(QDomDocument& doc, QDomElement& that)
{
	LinkedModelGroup* grp0 = getGroup(0);
	if (grp0)
	{
		QDomElement models = doc.createElement("models");
		that.appendChild(models);
		grp0->saveValues(doc, models);
	}
	else { /* don't even add a "models" node */ }
}




void LinkedModelGroups::loadSettings(const QDomElement& that)
{
	QDomElement models = that.firstChildElement("models");
	if (auto grp0 = getGroup(0); !models.isNull() && grp0)
	{
		// only load the first group, the others are linked to the first
		grp0->loadValues(models);
	}
}


} // namespace lmms
