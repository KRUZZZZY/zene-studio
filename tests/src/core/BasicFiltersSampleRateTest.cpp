/*
 * BasicFiltersSampleRateTest.cpp - zero-sample-rate refusal proof for BasicFilters
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor
 * Boston, MA 02110-1301 USA.
 *
 */

#include "BasicFilters.h"

#include <cmath>
#include <QObject>
#include <QtTest>

using namespace lmms;

class BasicFiltersSampleRateTest : public QObject
{
    Q_OBJECT
private slots:
    void zeroSampleRatePoisonsCoeffs();
    void negativeSampleRateSetterIsRejected();
    void negativeSampleRateConstructorFallsBack();
    void zeroCutoffLowpassStaysFinite();
    void zeroCutoffHighpassStaysFinite();
};

void BasicFiltersSampleRateTest::zeroSampleRatePoisonsCoeffs()
{
    // A zero rate must not poison the sample ratio in the constructor:
    // coefficients and processed audio must stay finite.
    BasicFilters<2> zeroFilter( 0 );
    zeroFilter.setFilterType( BasicFilters<2>::FilterType::LowPass );
    zeroFilter.calcFilterCoeffs( 1000.0f, 1.0f );
    for ( int i = 0; i < 16; ++i )
    {
        QVERIFY( std::isfinite( zeroFilter.update( 1.0f, 0 ) ) );
    }

    // setSampleRate(0) must be rejected: the filter must keep processing
    // exactly like a filter whose rate was never touched.
    BasicFilters<2> filter( 44100 );
    filter.setSampleRate( 0 );
    BasicFilters<2> reference( 44100 );
    filter.setFilterType( BasicFilters<2>::FilterType::LowPass );
    filter.calcFilterCoeffs( 1000.0f, 1.0f );
    reference.setFilterType( BasicFilters<2>::FilterType::LowPass );
    reference.calcFilterCoeffs( 1000.0f, 1.0f );
    for ( int i = 0; i < 16; ++i )
    {
        const sample_t out = filter.update( 1.0f, 0 );
        QVERIFY( std::isfinite( out ) );
        QVERIFY( out == reference.update( 1.0f, 0 ) );
    }
}

void BasicFiltersSampleRateTest::negativeSampleRateSetterIsRejected()
{
    // A signed -1 must not wrap to UINT32_MAX at the unsigned setter: the
    // rate must stay at 44100, so the filter processes exactly like an
    // untouched 44100 Hz filter.
    BasicFilters<2> filter( 44100 );
    filter.setSampleRate( -1 );
    BasicFilters<2> reference( 44100 );
    filter.setFilterType( BasicFilters<2>::FilterType::LowPass );
    filter.calcFilterCoeffs( 1000.0f, 1.0f );
    reference.setFilterType( BasicFilters<2>::FilterType::LowPass );
    reference.calcFilterCoeffs( 1000.0f, 1.0f );
    for ( int i = 0; i < 16; ++i )
    {
        QVERIFY( filter.update( 1.0f, 0 ) == reference.update( 1.0f, 0 ) );
    }
}

void BasicFiltersSampleRateTest::negativeSampleRateConstructorFallsBack()
{
    // A negative rate must take the same fallback as a zero rate in the
    // constructor, not wrap to UINT32_MAX first. The fallback rate is 1 Hz,
    // where a whole-number cutoff gives a degenerate response (omega is a
    // multiple of 2*pi), so 1000.5 Hz is used to make the two paths differ.
    BasicFilters<2> negative( -1 );
    BasicFilters<2> zero( sample_rate_t{ 0 } );
    negative.setFilterType( BasicFilters<2>::FilterType::LowPass );
    zero.setFilterType( BasicFilters<2>::FilterType::LowPass );
    negative.calcFilterCoeffs( 1000.5f, 1.0f );
    zero.calcFilterCoeffs( 1000.5f, 1.0f );
    const sample_t expected = zero.update( 1.0f, 0 );
    QVERIFY( expected != 0.0f );
    QVERIFY( negative.update( 1.0f, 0 ) == expected );
    for ( int i = 1; i < 16; ++i )
    {
        const sample_t out = negative.update( 1.0f, 0 );
        QVERIFY( std::isfinite( out ) );
        QVERIFY( out == zero.update( 1.0f, 0 ) );
    }
}

void BasicFiltersSampleRateTest::zeroCutoffLowpassStaysFinite()
{
    LinkwitzRiley<2> filter( 44100.0f );
    filter.setLowpass( 0.0f );

    QVERIFY( std::isfinite( filter.update( 1.0f, 0 ) ) );
}

void BasicFiltersSampleRateTest::zeroCutoffHighpassStaysFinite()
{
    LinkwitzRiley<2> filter( 44100.0f );
    filter.setHighpass( 0.0f );

    QVERIFY( std::isfinite( filter.update( 1.0f, 0 ) ) );
}

QTEST_GUILESS_MAIN(BasicFiltersSampleRateTest)
#include "BasicFiltersSampleRateTest.moc"
