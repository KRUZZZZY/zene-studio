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

#include <array>
#include <cmath>
#include <cstddef>
#include <new>
#include <QObject>
#include <QtTest>

using namespace lmms;

class BasicFiltersSampleRateTest : public QObject
{
    Q_OBJECT
private slots:
    void zeroSampleRatePoisonsCoeffs();
    void zeroCutoffLowpassStaysFinite();
    void zeroCutoffHighpassStaysFinite();
    void unsetTypeDefaultsToLowpass();
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

// Out of line so the compiler cannot see that the type was never written
// (it would otherwise reject the read at compile time under -Werror).
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void impulseResponse( BasicFilters<2>& filter, std::array<sample_t, 16>& out )
{
    filter.calcFilterCoeffs( 1000.0f, 1.0f );
    for ( std::size_t i = 0; i < out.size(); ++i )
    {
        out[i] = filter.update( i == 0 ? 1.0f : 0.0f, 0 );
    }
}

void BasicFiltersSampleRateTest::unsetTypeDefaultsToLowpass()
{
    // A filter whose type was never set must not read an indeterminate
    // FilterType. Stale memory is simulated by a 0x000000FF word in the
    // storage, which matches no enumerator; construction in place must then
    // give the type the documented default (LowPass) before any use.
    alignas( BasicFilters<2> ) std::array<unsigned char, sizeof( BasicFilters<2> )> storage{};
    for ( std::size_t i = 0; i < storage.size(); i += 4 )
    {
        storage[i] = 0xFF;
    }

    auto* filter = new ( storage.data() ) BasicFilters<2>( 44100 );
    std::array<sample_t, 16> actual{};
    impulseResponse( *filter, actual );
    filter->~BasicFilters();

    BasicFilters<2> reference( 44100 );
    reference.setFilterType( BasicFilters<2>::FilterType::LowPass );
    std::array<sample_t, 16> expected{};
    impulseResponse( reference, expected );

    bool produced = false;
    for ( std::size_t i = 0; i < actual.size(); ++i )
    {
        QVERIFY( std::isfinite( actual[i] ) );
        QVERIFY( actual[i] == expected[i] );
        produced = produced || actual[i] != 0.0f;
    }
    QVERIFY( produced );
}

QTEST_GUILESS_MAIN(BasicFiltersSampleRateTest)
#include "BasicFiltersSampleRateTest.moc"
