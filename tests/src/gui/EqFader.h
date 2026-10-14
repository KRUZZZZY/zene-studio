#ifndef EQFADER_H
#define EQFADER_H

#include "Fader.h"

namespace lmms::gui
{

class EqFader : public Fader
{
	Q_OBJECT
private slots:
	void updateVuMeters();
};

} // namespace lmms::gui

#endif // EQFADER_H
