#pragma once

#include <functional>
#include <optional>

#include <QDomElement>

#include "Plugin.h"

namespace lmms::legacyvst2
{
using Attributes = Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap;
using Resolver = std::function<std::optional<Attributes>(const QString& absoluteFile, Plugin::Type type)>;

LMMS_EXPORT std::optional<Attributes> resolveFromFactory(const QString& absoluteFile, Plugin::Type type);
LMMS_EXPORT QDomElement convertInstrument(const QDomElement& instrumentNode, const Resolver& resolve = resolveFromFactory);
LMMS_EXPORT QDomElement convertEffect(const QDomElement& effectNode, const Resolver& resolve = resolveFromFactory);
LMMS_EXPORT QString legacyStateTagName();
LMMS_EXPORT bool conversionEnabled();
LMMS_EXPORT void setConversionEnabled(bool enabled);
}
