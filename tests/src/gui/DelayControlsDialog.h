#ifndef DELAYCONTROLSDIALOG_H
#define DELAYCONTROLSDIALOG_H

#include "EffectControlDialog.h"

namespace lmms
{

class DelayControls;

namespace gui
{

class DelayControlsDialog : public EffectControlDialog
{
	Q_OBJECT
};

class XyPad : public QWidget
{
	Q_OBJECT
};

} // namespace gui

} // namespace lmms

#endif // DELAYCONTROLSDIALOG_H
