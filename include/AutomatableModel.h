/*
 * AutomatableModel.h - declaration of class AutomatableModel
 *
 * Copyright (c) 2007-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_AUTOMATABLE_MODEL_H
#define LMMS_AUTOMATABLE_MODEL_H

#include <atomic>
#include <cmath>
#include <QMap>
#include <QMutex>

#include "JournallingObject.h"
#include "Model.h"
#include "TimePos.h"
#include "ValueBuffer.h"
#include "ModelVisitor.h"


namespace lmms
{

// simple way to map a property of a view to a model
#define mapPropertyFromModelPtr(type,getfunc,setfunc,modelname)	\
		public:													\
			type getfunc() const								\
			{													\
				return (type) modelname->value();				\
			}													\
		public slots:											\
			void setfunc( const type val )						\
			{													\
				modelname->setValue( val );						\
			}

#define mapPropertyFromModel(type,getfunc,setfunc,modelname)	\
		public:													\
			type getfunc() const								\
			{													\
				return (type) modelname.value();				\
			}													\
		public slots:											\
			void setfunc( const type val )						\
			{													\
				modelname.setValue( (float) val );				\
			}

// use this to make subclasses visitable
#define MODEL_IS_VISITABLE \
	void accept(ModelVisitor& v) override { v.visit(*this); } \
	void accept(ConstModelVisitor& v) const override { v.visit(*this); }



class ControllerConnection;

class LMMS_EXPORT AutomatableModel : public Model, public JournallingObject
{
	Q_OBJECT
public:
	enum class ScaleType
	{
		Linear,
		Logarithmic,
		Decibel
	};


	~AutomatableModel() override;

	// Implement those by using the MODEL_IS_VISITABLE macro
	virtual void accept(ModelVisitor& v) = 0;
	virtual void accept(ConstModelVisitor& v) const = 0;

public:
	/**
	   @brief Return this class casted to Target
	   @test AutomatableModelTest.cpp
	   @param doThrow throw an assertion if the cast fails, instead of
	     returning a nullptr
	   @return the casted class if Target is the exact or a base class of
	     *this, nullptr otherwise
	*/
	template<class Target>
	Target* dynamicCast(bool doThrow = false)
	{
		DCastVisitor<Target> vis; accept(vis);
		if (doThrow && !vis.result) { Q_ASSERT(false); }
		return vis.result;
	}

	//! const overload, see overloaded function
	template<class Target>
	const Target* dynamicCast(bool doThrow = false) const
	{
		ConstDCastVisitor<Target> vis; accept(vis);
		if (doThrow && !vis.result) { Q_ASSERT(false); }
		return vis.result;
	}

	bool isAutomated() const;
	bool isAutomatedOrControlled() const
	{
		return isAutomated() || m_controllerConnection != nullptr;
	}

	ControllerConnection* controllerConnection() const
	{
		return m_controllerConnection;
	}


	void setControllerConnection( ControllerConnection* c );


	template<class T>
	static T castValue( const float v )
	{
		return (T)( v );
	}

	template<bool>
	static bool castValue( const float v )
	{
		return (std::round(v) != 0);
	}


	template<class T>
	inline T value( int frameOffset = 0 ) const
	{
		// TODO
		// The `m_value` should only be updated whenever the Controller value changes,
		// instead of the Model calling `controller->currentValue()` every time.
		// This becomes even worse in the case of linked Models, where it has to
		// loop through the list of all links.

		if (m_useControllerValue)
		{
			if (m_controllerConnection)
			{
				return castValue<T>(controllerValue(frameOffset));
			}
			for (auto next = m_nextLink; next != this; next = next->m_nextLink)
			{
				if (next->controllerConnection() && next->useControllerValue())
				{
					return castValue<T>(fittedValue(next->controllerValue(frameOffset)));
				}
			}
		}
		return castValue<T>( m_value );
	}

	float controllerValue( int frameOffset ) const;

	//! @brief Function that returns sample-exact data as a ValueBuffer
	//! @return pointer to model's valueBuffer when s.ex.data exists, NULL otherwise
	ValueBuffer * valueBuffer();

	template<class T>
	T initValue() const
	{
		return castValue<T>( m_initValue );
	}

	bool isAtInitValue() const
	{
		return m_value == m_initValue;
	}

	template<class T>
	T minValue() const
	{
		return castValue<T>( m_minValue );
	}

	template<class T>
	T maxValue() const
	{
		return castValue<T>( m_maxValue );
	}

	template<class T>
	T step() const
	{
		return castValue<T>( m_step );
	}

	//! @brief Returns value scaled with the scale type and min/max values of this model
	float scaledValue( float value ) const;
	//! @brief Returns value applied with the inverse of this model's scale type
	float inverseScaledValue( float value ) const;

	void setInitValue( const float value );

	void setValue(const float value, const bool isAutomated = false);

	void incValue( int steps )
	{
		setValue( m_value + steps * m_step );
	}

	float range() const
	{
		return m_range;
	}

	void setRange( const float min, const float max, const float step = 1 );
	void setScaleType( ScaleType sc ) {
		m_scaleType = sc;
	}
	void setScaleLogarithmic( bool setToTrue = true )
	{
		setScaleType( setToTrue ? ScaleType::Logarithmic : ScaleType::Linear );
	}
	bool isScaleLogarithmic() const
	{
		return m_scaleType == ScaleType::Logarithmic;
	}

	void setStep( const float step );

	float centerValue() const
	{
		return m_centerValue;
	}

	void setCenterValue( const float centerVal )
	{
		m_centerValue = centerVal;
	}

	//! link this to @p model, copying the value from @p model
	void linkToModel(AutomatableModel* model);
	//! @return number of other models linked to this
	size_t countLinks() const;

	/**
	 * @brief Saves settings (value, automation links and controller connections) of AutomatableModel into
	 *  specified DOM element using <name> as attribute/node name
	 * @param doc TODO
	 * @param element Where this option shall be saved.
	 *  Depending on the model, this can be done in an attribute or in a subnode.
	 * @param name Name to store this model as.
	 */
	virtual void saveSettings( QDomDocument& doc, QDomElement& element, const QString& name );

	/*! \brief Loads settings (value, automation links and controller connections) of AutomatableModel from
				specified DOM element using <name> as attribute/node name */
	virtual void loadSettings( const QDomElement& element, const QString& name );

	QString nodeName() const override
	{
		return "automatablemodel";
	}

	virtual QString displayValue( const float val ) const = 0;

	bool isLinked() const
	{
		return m_nextLink != this;
	}

	// a way to track changed values in the model and avoid using signals/slots - useful for speed-critical code.
	// note that this method should only be called once per period since it resets the state of the variable - so if your model
	// has to be accessed by more than one object, then this function shouldn't be used.
	bool isValueChanged()
	{
		if( m_valueChanged || valueBuffer() )
		{
			m_valueChanged = false;
			return true;
		}
		return false;
	}

	float globalAutomationValueAt( const TimePos& time );

	void setStrictStepSize( const bool b )
	{
		m_hasStrictStepSize = b;
	}

	static void incrementPeriodCounter()
	{
		++s_periodCounter;
	}

	static void resetPeriodCounter()
	{
		s_periodCounter = 0;
	}

	bool useControllerValue() const
	{
		return m_useControllerValue;
	}

	static bool mustQuoteName(const QString &name);

	// -----------------------------------------------------------------------
	// Automation modes (post-alpha/automation-modes).
	//
	// Read is the status quo: the control follows its written automation and
	// never writes it. Touch writes while the control is held and returns to
	// reading when it is released. Latch writes from the first touch until the
	// transport run it was made in ends. Write overwrites the pass for as long
	// as the transport runs, with no touch needed. Read is the default because
	// it is what every existing project does and what an engineer expects of a
	// control they have not armed - the alpha behaves as Read today.
	//
	// THREAD OWNERSHIP. setAutomationMode(), noteAutomationTouchStart()/End()
	// and setTrimOffset() are called from the GUI thread. The audio thread only
	// reads the relaxed atomics they publish and makes the write decision itself
	// in automationWantsWrite(); nothing on that path allocates, locks or grows,
	// and AutomationModesTest asserts the atomics are lock-free, so a mode
	// change can never make the audio thread block. The transport run token is
	// published by the transport observer (Song::processNextBuffer, which runs
	// on the render thread).
	// -----------------------------------------------------------------------
	enum class AutomationMode
	{
		Read,   //!< follow written automation, never write (default)
		Touch,  //!< write while touched, then return to reading
		Latch,  //!< write from the first touch until the transport run ends
		Write   //!< overwrite the pass while the transport runs
	};

	static_assert(std::atomic<AutomationMode>::is_always_lock_free,
		"the audio thread reads the automation mode without a lock; a locked "
		"atomic would let a GUI mode change block the render thread");

	AutomationMode automationMode() const;
	void setAutomationMode( AutomationMode mode );

	//! The transport was observed running (@p active) or not. Called once per
	//! rendered period from Song::processNextBuffer(); a false -> true edge
	//! hands out a new run token, which is what ends every Latch engagement
	//! from the previous run. Idempotent for an unchanged state, so a run keeps
	//! one token for its whole length.
	static void observeAutomationTransport( bool active );
	//! Token of the transport run being rendered; 0 while the transport is not
	//! running. A Latch engagement is bound to the token it was made under.
	static quint64 automationTransportRun();

	//! Monotone wall clock in nanoseconds (std::chrono::steady_clock): ~25 ns
	//! and lock-free. Call it once per Song::processAutomations(), not once per
	//! model.
	static qint64 automationClockNs();

	//! GUI thread: the control was grabbed or moved. @p nowNs must come from
	//! automationClockNs() unless the caller has its own monotone clock, and
	//! @p transportRun is the run the engagement is armed against.
	void noteAutomationTouchStart( qint64 nowNs, quint64 transportRun );
	void noteAutomationTouchStart( qint64 nowNs )
	{
		noteAutomationTouchStart( nowNs, automationTransportRun() );
	}
	void noteAutomationTouchStart() { noteAutomationTouchStart( automationClockNs() ); }
	//! GUI thread: the control was released. This ends a Touch gesture
	//! immediately; a Latch engagement survives it - that difference is what
	//! Latch is for.
	void noteAutomationTouchEnd();

	//! THE decision, made on the audio thread: may this control write into its
	//! automation clip at this instant? @p transportRun is the run token of the
	//! period being rendered (0 = not running, which includes an offline render)
	//! and @p nowNs a monotone timestamp. Pure - no allocation, no locking, no
	//! mutation - so it is safe to call per model per tick.
	bool automationWantsWrite( quint64 transportRun, qint64 nowNs ) const;

	//! How long a gesture keeps its write authority after its last touch event.
	static qint64 automationTouchTimeoutNs();
	static void setAutomationTouchTimeoutNs( qint64 ns );

	//! A non-destructive offset applied on top of the written automation. It is
	//! applied where the automation is *read out* to the control (Song's apply
	//! pass) and is never written back into the clip, so it can be changed or
	//! removed without touching the recorded data. It applies to automated
	//! controls: a control with no automation has nothing to trim.
	void setTrimOffset( float offset );
	float trimOffset() const;
	float effectiveAutomationValue( float writtenValue ) const;

public slots:
	virtual void reset();
	void unlink();
	void unlinkControllerConnection();
	void setUseControllerValue(bool b = true);


protected:
	AutomatableModel(
						const float val = 0,
						const float min = 0,
						const float max = 0,
						const float step = 0,
						Model* parent = nullptr,
						const QString& displayName = QString(),
						bool defaultConstructed = false );
	//! returns a value which is in range between min() and
	//! max() and aligned according to the step size (step size 0.05 -> value
	//! 0.12345 becomes 0.10 etc.). You should always call it at the end after
	//! doing your own calculations.
	float fittedValue( float value ) const;

private:
	// dynamicCast implementation
	template<class Target>
	struct DCastVisitor : public ModelVisitor
	{
		Target* result = nullptr;
		void visit(Target& tar) { result = &tar; }
	};

	// dynamicCast implementation
	template<class Target>
	struct ConstDCastVisitor : public ConstModelVisitor
	{
		const Target* result = nullptr;
		void visit(const Target& tar) { result = &tar; }
	};

	void saveSettings( QDomDocument& doc, QDomElement& element ) override
	{
		saveSettings( doc, element, "value" );
	}

	void loadSettings( const QDomElement& element ) override
	{
		loadSettings( element, "value" );
	}

	void setValueInternal(const float value);

	//! linking is stored in a linked list ring
	//! @return the model whose `m_nextLink` is `this`,
	//! or `this` if there are no linked models
	AutomatableModel* getLastLinkedModel() const;
	//! @return true if the `model` is in the linked list
	bool isLinkedToModel(AutomatableModel* model) const;
	
	//! @brief Scales @value from linear to logarithmic.
	//! Value should be within [0,1]
	template<class T> T logToLinearScale( T value ) const;

	//! rounds @a value to @a where if it is close to it
	//! @param value will be modified to rounded value
	template<class T> void roundAt( T &value, const T &where ) const;


	ScaleType m_scaleType; //!< scale type, linear by default
	float m_value;
	float m_initValue;
	float m_minValue;
	float m_maxValue;
	float m_step;
	float m_range;
	float m_centerValue;

	bool m_valueChanged;
	float m_oldValue; //!< used by valueBuffer for interpolation

	// used to determine if step size should be applied strictly (ie. always)
	// or only when value set from gui (default)
	bool m_hasStrictStepSize;

	//! an `AutomatableModel` can be linked together with others in a linked list
	//! the list has no end, the last model is connected to the first forming a ring
	AutomatableModel* m_nextLink;


	//! NULL if not appended to controller, otherwise connection info
	ControllerConnection* m_controllerConnection;


	ValueBuffer m_valueBuffer;
	long m_lastUpdatedPeriod;
	static long s_periodCounter;

	bool m_hasSampleExactData;

	// prevent several threads from attempting to write the same vb at the same time
	QMutex m_valueBufferMutex;

	bool m_useControllerValue;

	// -----------------------------------------------------------------------
	// Automation mode state (post-alpha/automation-modes). Written by the GUI
	// thread, read by the audio thread; relaxed atomics throughout, never a
	// mutex, so the render thread cannot be made to wait on a mode change.
	// -----------------------------------------------------------------------
	std::atomic<AutomationMode> m_automationMode{ AutomationMode::Read };
	//! Timestamp of the last touch event, in automationClockNs() units.
	std::atomic<qint64> m_touchStampNs{ 0 };
	std::atomic<bool> m_touching{ false };
	//! The transport run this control's Latch was armed under (0 = not armed).
	//! Storing the run rather than a flag is what makes a latch self-clearing:
	//! no later run can ever match it, so nothing has to sweep the models when
	//! the transport stops.
	std::atomic<quint64> m_latchRun{ 0 };
	std::atomic<float> m_trimOffset{ 0.0f };

	//! Automation-mode statics. s_transportRun is the token of the run being
	//! rendered (0 = not running) and s_transportActive the state it was handed
	//! out for, so only the transport's edges change anything.
	static std::atomic<quint64> s_transportRun;
	static std::atomic<quint64> s_transportRunsStarted;
	static std::atomic<bool> s_transportActive;
	static std::atomic<qint64> s_touchTimeoutNs;

signals:
	void initValueChanged( float val );
	void destroyed( lmms::jo_id_t id );

} ;




template <typename T> class LMMS_EXPORT TypedAutomatableModel : public AutomatableModel
{
public:
	using AutomatableModel::AutomatableModel;
	T value( int frameOffset = 0 ) const
	{
		return AutomatableModel::value<T>( frameOffset );
	}

	T initValue() const
	{
		return AutomatableModel::initValue<T>();
	}

	T minValue() const
	{
		return AutomatableModel::minValue<T>();
	}

	T maxValue() const
	{
		return AutomatableModel::maxValue<T>();
	}
};


// some typed AutomatableModel-definitions

class LMMS_EXPORT FloatModel : public TypedAutomatableModel<float>
{
	Q_OBJECT
	MODEL_IS_VISITABLE
public:
	FloatModel( float val = 0, float min = 0, float max = 0, float step = 0,
				Model * parent = nullptr,
				const QString& displayName = QString(),
				bool defaultConstructed = false ) :
		TypedAutomatableModel( val, min, max, step, parent, displayName, defaultConstructed )
	{
	}
	float getRoundedValue() const;
	int getDigitCount() const;
	QString displayValue( const float val ) const override;
} ;


class LMMS_EXPORT IntModel : public TypedAutomatableModel<int>
{
	Q_OBJECT
	MODEL_IS_VISITABLE
public:
	IntModel( int val = 0, int min = 0, int max = 0,
				Model* parent = nullptr,
				const QString& displayName = QString(),
				bool defaultConstructed = false ) :
		TypedAutomatableModel( val, min, max, 1, parent, displayName, defaultConstructed )
	{
	}
	QString displayValue( const float val ) const override;
} ;


class LMMS_EXPORT BoolModel : public TypedAutomatableModel<bool>
{
	Q_OBJECT
	MODEL_IS_VISITABLE
public:
	BoolModel( const bool val = false,
				Model* parent = nullptr,
				const QString& displayName = QString(),
				bool defaultConstructed = false ) :
		TypedAutomatableModel( val, false, true, 1, parent, displayName, defaultConstructed )
	{
	}
	QString displayValue( const float val ) const override;
} ;

using AutomatedValueMap = QMap<AutomatableModel*, float>;

} // namespace lmms

#endif // LMMS_AUTOMATABLE_MODEL_H
