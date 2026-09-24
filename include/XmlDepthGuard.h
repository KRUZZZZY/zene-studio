/* XML nesting guard shared by project parse entry points. */
#ifndef LMMS_XML_DEPTH_GUARD_H
#define LMMS_XML_DEPTH_GUARD_H

#include <QByteArray>
#include <QString>
#include <QXmlStreamReader>

namespace lmms
{

constexpr int MaxProjectXmlDepth = 1024;

inline bool projectXmlDepthWithinLimit(const QByteArray& bytes, QString* error = nullptr)
{
	QXmlStreamReader reader(bytes);
	reader.setNamespaceProcessing(false);
	int depth = 0;
	while (!reader.atEnd())
	{
		const auto token = reader.readNext();
		if (token == QXmlStreamReader::StartElement)
		{
			if (++depth > MaxProjectXmlDepth)
			{
				if (error) { *error = QStringLiteral("XML nesting exceeds the project safety limit"); }
				return false;
			}
		}
		else if (token == QXmlStreamReader::EndElement)
		{
			--depth;
		}
	}
	if (reader.hasError())
	{
		if (error) { *error = reader.errorString(); }
		return false;
	}
	return true;
}

} // namespace lmms

#endif
