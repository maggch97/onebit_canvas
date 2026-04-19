#pragma once

/*
 * Adapted from Cairo 1.18.4:
 *   - src/cairo-fixed-private.h
 *   - src/cairo-fixed-type-private.h
 *   - src/cairo-polygon.c
 *   - src/cairo-rectangle.c
 *   - src/cairo-mono-scan-converter.c
 *
 * Original Cairo license notice:
 *
 * Copyright (c) 2011 Intel Corporation
 * Copyright (c) 2002 University of Southern California
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * This header keeps Cairo's mono scan conversion math and active-edge stepping,
 * but strips it down to the minimum needed for onebit_canvas: convert a filled
 * polygon into opaque half-open spans for a 1-bit top-down bitmap.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace onebit::detail::cairo_mono
{

using fixed_t = std::int64_t;

constexpr int FIXED_FRAC_BITS = 8;
constexpr fixed_t FIXED_ONE = static_cast<fixed_t>( 1LL << FIXED_FRAC_BITS );
constexpr fixed_t FIXED_FRAC_MASK = FIXED_ONE - 1;


struct FixedPoint
{
    fixed_t x = 0;
    fixed_t y = 0;
};


struct FixedLine
{
    FixedPoint p1;
    FixedPoint p2;
};


struct EdgeInput
{
    FixedLine line;
    fixed_t top = 0;
    fixed_t bottom = 0;
    int dir = 0;
};


struct HalfOpenSpan
{
    int x = 0;
    std::uint8_t coverage = 0;
};


inline int clamp_to_int( fixed_t value ) noexcept
{
    if( value <= static_cast<fixed_t>( std::numeric_limits<int>::min() ) )
        return std::numeric_limits<int>::min();

    if( value >= static_cast<fixed_t>( std::numeric_limits<int>::max() ) )
        return std::numeric_limits<int>::max();

    return static_cast<int>( value );
}


inline fixed_t fixed_from_int( int value ) noexcept
{
    return static_cast<fixed_t>( value ) << FIXED_FRAC_BITS;
}


inline fixed_t fixed_from_double( double value ) noexcept
{
    // Cairo uses bankers rounding for double -> fixed conversion.
    const double scaled = std::nearbyint( value * static_cast<double>( FIXED_ONE ) );

    if( scaled <= static_cast<long double>( std::numeric_limits<fixed_t>::min() ) )
        return std::numeric_limits<fixed_t>::min();

    if( scaled >= static_cast<long double>( std::numeric_limits<fixed_t>::max() ) )
        return std::numeric_limits<fixed_t>::max();

    return static_cast<fixed_t>( scaled );
}


inline int fixed_integer_part( fixed_t value ) noexcept
{
    return clamp_to_int( value >> FIXED_FRAC_BITS );
}


inline int fixed_integer_floor( fixed_t value ) noexcept
{
    if( value >= 0 )
        return fixed_integer_part( value );

    return clamp_to_int( -( ( ( -value ) - 1 ) >> FIXED_FRAC_BITS ) - 1 );
}


inline int fixed_integer_ceil( fixed_t value ) noexcept
{
    if( value > 0 )
        return clamp_to_int( ( ( value - 1 ) >> FIXED_FRAC_BITS ) + 1 );

    return clamp_to_int( -( static_cast<fixed_t>( static_cast<std::uint64_t>( -value ) >> FIXED_FRAC_BITS ) ) );
}


inline int fixed_integer_round_down( fixed_t value ) noexcept
{
    return fixed_integer_part( value + FIXED_FRAC_MASK / 2 );
}


struct Quorem
{
    fixed_t quo = 0;
    fixed_t rem = 0;
};


#if defined(__SIZEOF_INT128__)
inline Quorem floored_muldivrem( fixed_t x, fixed_t a, fixed_t b ) noexcept
{
    using wide_t = __int128_t;

    const wide_t xa = static_cast<wide_t>( x ) * static_cast<wide_t>( a );
    Quorem qr;
    qr.quo = static_cast<fixed_t>( xa / static_cast<wide_t>( b ) );
    qr.rem = static_cast<fixed_t>( xa % static_cast<wide_t>( b ) );

    if( ( xa >= 0 ) != ( b >= 0 ) && qr.rem )
    {
        qr.quo -= 1;
        qr.rem += b;
    }

    return qr;
}
#elif defined(_MSC_VER) && defined(_M_X64)
inline std::uint64_t unsigned_abs64( fixed_t value ) noexcept
{
    const std::uint64_t bits = static_cast<std::uint64_t>( value );
    return value < 0 ? ( ~bits + 1u ) : bits;
}


inline Quorem floored_muldivrem( fixed_t x, fixed_t a, fixed_t b ) noexcept
{
    const bool negativeDividend = ( x < 0 ) != ( a < 0 );
    const bool negativeDivisor = b < 0;
    const std::uint64_t ux = unsigned_abs64( x );
    const std::uint64_t ua = unsigned_abs64( a );
    const std::uint64_t ub = unsigned_abs64( b );
    std::uint64_t high = 0;
    std::uint64_t remainderUnsigned = 0;
    const std::uint64_t low = _umul128( ux, ua, &high );
    const std::uint64_t quotientUnsigned = _udiv128( high, low, ub, &remainderUnsigned );
    Quorem qr;
    qr.quo = negativeDividend != negativeDivisor
             ? -static_cast<fixed_t>( quotientUnsigned )
             : static_cast<fixed_t>( quotientUnsigned );
    qr.rem = negativeDividend
             ? -static_cast<fixed_t>( remainderUnsigned )
             : static_cast<fixed_t>( remainderUnsigned );

    if( negativeDividend != negativeDivisor && qr.rem )
    {
        qr.quo -= 1;
        qr.rem += b;
    }

    return qr;
}
#else
#error "onebit_canvas_cairo_mono.hpp needs either __int128 or MSVC x64 _umul128/_udiv128 support"
#endif

class MonoScanConverter
{
public:
    MonoScanConverter( int xmin, int ymin, int xmax, int ymax, std::size_t edgeCapacity ) :
            m_xmin( xmin ),
            m_xmax( xmax ),
            m_ymin( ymin ),
            m_ymax( ymax ),
            m_edges( edgeCapacity )
    {
        const int bucketCount = ( m_ymax - m_ymin ) + 1;
        m_yBuckets.assign( static_cast<std::size_t>( bucketCount ), nullptr );
        m_yBuckets.back() = &m_bucketSentinel;

        const int maxNumSpans = std::max( 0, m_xmax - m_xmin + 1 );
        m_spans.resize( static_cast<std::size_t>( maxNumSpans ) );

        m_head.vertical = true;
        m_head.height_left = std::numeric_limits<int>::max();
        m_head.x.quo = fixed_from_int( std::numeric_limits<int>::min() / 2 );
        m_head.prev = nullptr;
        m_head.next = &m_tail;

        m_tail.prev = &m_head;
        m_tail.next = nullptr;
        m_tail.x.quo = fixed_from_int( std::numeric_limits<int>::max() / 2 );
        m_tail.height_left = std::numeric_limits<int>::max();
        m_tail.vertical = true;
    }

    void AddEdge( const EdgeInput& edge ) noexcept
    {
        int y = fixed_integer_round_down( edge.top );
        const int ytop = std::max( y, m_ymin );

        y = fixed_integer_round_down( edge.bottom );
        const int ybot = std::min( y, m_ymax );

        if( ybot <= ytop )
            return;

        ScanEdge* e = &m_edges[m_edgeCount++];
        e->height_left = ybot - ytop;
        e->dir = edge.dir;

        const fixed_t dx = edge.line.p2.x - edge.line.p1.x;
        const fixed_t dy = edge.line.p2.y - edge.line.p1.y;

        if( dx == 0 )
        {
            e->vertical = true;
            e->x.quo = edge.line.p1.x;
            e->x.rem = 0;
            e->dxdy.quo = 0;
            e->dxdy.rem = 0;
            e->dy = 0;
        }
        else
        {
            e->vertical = false;
            e->dxdy = floored_muldivrem( dx, FIXED_ONE, dy );
            e->dy = dy;

            e->x = floored_muldivrem(
                    static_cast<fixed_t>( ytop ) * FIXED_ONE + FIXED_FRAC_MASK / 2 - edge.line.p1.y,
                    dx, dy );
            e->x.quo += edge.line.p1.x;
        }

        e->x.rem -= dy;
        InsertEdgeIntoBucket( e, ytop );
    }

    template <typename EmitSpan>
    bool Generate( EmitSpan&& emitSpan, unsigned windingMask ) noexcept
    {
        const int h = m_ymax - m_ymin;

        for( int i = 0, j = 0; i < h; i = j )
        {
            j = i + 1;

            if( m_yBuckets[static_cast<std::size_t>( i )] )
                ActiveListMergeEdges( m_yBuckets[static_cast<std::size_t>( i )] );

            if( m_isVertical )
            {
                int minHeight = m_head.next->height_left;

                for( ScanEdge* e = m_head.next; e != &m_tail; e = e->next )
                    minHeight = std::min( minHeight, e->height_left );

                while( --minHeight >= 1 && m_yBuckets[static_cast<std::size_t>( j )] == nullptr )
                    ++j;

                if( j != i + 1 )
                    StepEdges( j - ( i + 1 ) );
            }

            Row( windingMask );

            if( m_numSpans != 0 )
            {
                for( int yy = m_ymin + i; yy < m_ymin + j; ++yy )
                {
                    for( int s = 0; s + 1 < m_numSpans; ++s )
                    {
                        if( m_spans[static_cast<std::size_t>( s )].coverage )
                        {
                            emitSpan( yy,
                                      m_spans[static_cast<std::size_t>( s )].x,
                                      m_spans[static_cast<std::size_t>( s + 1 )].x );
                        }
                    }
                }
            }

            if( m_head.next == &m_tail )
                m_isVertical = true;
        }

        return true;
    }

private:
    struct ScanEdge
    {
        ScanEdge* next = nullptr;
        ScanEdge* prev = nullptr;
        int height_left = 0;
        int dir = 0;
        bool vertical = false;
        fixed_t dy = 0;
        Quorem x;
        Quorem dxdy;
    };

    void InsertEdgeIntoBucket( ScanEdge* edge, int y ) noexcept
    {
        ScanEdge*& bucket = m_yBuckets[static_cast<std::size_t>( y - m_ymin )];

        if( bucket != nullptr )
            bucket->prev = edge;

        edge->next = bucket;
        edge->prev = nullptr;
        bucket = edge;
    }

    static ScanEdge* MergeSortedEdges( ScanEdge* headA, ScanEdge* headB ) noexcept
    {
        ScanEdge* head;
        ScanEdge** next;
        ScanEdge* prev;
        fixed_t x;

        prev = headA->prev;
        next = &head;

        if( headA->x.quo <= headB->x.quo )
        {
            head = headA;
        }
        else
        {
            head = headB;
            headB->prev = prev;
            goto start_with_b;
        }

        do
        {
            x = headB->x.quo;

            while( headA != nullptr && headA->x.quo <= x )
            {
                prev = headA;
                next = &headA->next;
                headA = headA->next;
            }

            headB->prev = prev;
            *next = headB;

            if( headA == nullptr )
                return head;

        start_with_b:
            x = headA->x.quo;

            while( headB != nullptr && headB->x.quo <= x )
            {
                prev = headB;
                next = &headB->next;
                headB = headB->next;
            }

            headA->prev = prev;
            *next = headA;

            if( headB == nullptr )
                return head;
        }
        while( true );
    }

    static ScanEdge* SortEdges( ScanEdge* list, unsigned level, ScanEdge** headOut ) noexcept
    {
        ScanEdge* headOther = list->next;

        if( headOther == nullptr )
        {
            *headOut = list;
            return nullptr;
        }

        ScanEdge* remaining = headOther->next;

        if( list->x.quo <= headOther->x.quo )
        {
            *headOut = list;
            headOther->next = nullptr;
        }
        else
        {
            *headOut = headOther;
            headOther->prev = list->prev;
            headOther->next = list;
            list->prev = headOther;
            list->next = nullptr;
        }

        for( unsigned i = 0; i < level && remaining != nullptr; ++i )
        {
            remaining = SortEdges( remaining, i, &headOther );
            *headOut = MergeSortedEdges( *headOut, headOther );
        }

        return remaining;
    }

    static ScanEdge* MergeUnsortedEdges( ScanEdge* head, ScanEdge* unsorted ) noexcept
    {
        SortEdges( unsorted, std::numeric_limits<unsigned>::max(), &unsorted );
        return MergeSortedEdges( head, unsorted );
    }

    void ActiveListMergeEdges( ScanEdge* edges ) noexcept
    {
        for( ScanEdge* e = edges; m_isVertical && e != nullptr; e = e->next )
            m_isVertical = e->vertical;

        m_head.next = MergeUnsortedEdges( m_head.next, edges );
    }

    void AddSpan( int x1, int x2 ) noexcept
    {
        x1 = std::max( x1, m_xmin );
        x2 = std::min( x2, m_xmax );

        if( x2 <= x1 )
            return;

        int n = m_numSpans++;
        m_spans[static_cast<std::size_t>( n )].x = x1;
        m_spans[static_cast<std::size_t>( n )].coverage = 255;

        n = m_numSpans++;
        m_spans[static_cast<std::size_t>( n )].x = x2;
        m_spans[static_cast<std::size_t>( n )].coverage = 0;
    }

    void Row( unsigned mask ) noexcept
    {
        ScanEdge* edge = m_head.next;
        int xstart = std::numeric_limits<int>::min();
        fixed_t prevX = std::numeric_limits<fixed_t>::min();
        int winding = 0;

        m_numSpans = 0;

        while( edge != &m_tail )
        {
            ScanEdge* next = edge->next;
            const int xend = fixed_integer_round_down( edge->x.quo );

            if( --edge->height_left )
            {
                if( !edge->vertical )
                {
                    edge->x.quo += edge->dxdy.quo;
                    edge->x.rem += edge->dxdy.rem;

                    if( edge->x.rem >= 0 )
                    {
                        ++edge->x.quo;
                        edge->x.rem -= edge->dy;
                    }
                }

                if( edge->x.quo < prevX )
                {
                    ScanEdge* pos = edge->prev;
                    pos->next = next;
                    next->prev = pos;

                    do
                    {
                        pos = pos->prev;
                    }
                    while( edge->x.quo < pos->x.quo );

                    pos->next->prev = edge;
                    edge->next = pos->next;
                    edge->prev = pos;
                    pos->next = edge;
                }
                else
                {
                    prevX = edge->x.quo;
                }
            }
            else
            {
                edge->prev->next = next;
                next->prev = edge->prev;
            }

            winding += edge->dir;

            if( ( winding & static_cast<int>( mask ) ) == 0 )
            {
                if( fixed_integer_round_down( next->x.quo ) > xend + 1 )
                {
                    AddSpan( xstart, xend );
                    xstart = std::numeric_limits<int>::min();
                }
            }
            else if( xstart == std::numeric_limits<int>::min() )
            {
                xstart = xend;
            }

            edge = next;
        }
    }

    void StepEdges( int count ) noexcept
    {
        for( ScanEdge* edge = m_head.next; edge != &m_tail; edge = edge->next )
        {
            edge->height_left -= count;

            if( edge->height_left == 0 )
            {
                edge->prev->next = edge->next;
                edge->next->prev = edge->prev;
            }
        }
    }

    int m_xmin = 0;
    int m_xmax = 0;
    int m_ymin = 0;
    int m_ymax = 0;
    bool m_isVertical = true;
    std::vector<ScanEdge> m_edges;
    std::size_t m_edgeCount = 0;
    std::vector<ScanEdge*> m_yBuckets;
    ScanEdge m_bucketSentinel;
    ScanEdge m_head;
    ScanEdge m_tail;
    std::vector<HalfOpenSpan> m_spans;
    int m_numSpans = 0;
};


template <typename EmitSpan>
bool RasterizePolygon( const PointD* points, std::size_t count, int width, int height,
                       EmitSpan&& emitSpan )
{
    if( points == nullptr || count < 3 || width <= 0 || height <= 0 )
        return false;

    std::vector<FixedPoint> fixedPoints;
    fixedPoints.reserve( count );

    fixed_t minX = std::numeric_limits<fixed_t>::max();
    fixed_t minY = std::numeric_limits<fixed_t>::max();
    fixed_t maxX = std::numeric_limits<fixed_t>::min();
    fixed_t maxY = std::numeric_limits<fixed_t>::min();

    for( std::size_t i = 0; i < count; ++i )
    {
        FixedPoint pt{
            fixed_from_double( points[i].x ),
            fixed_from_double( points[i].y )
        };

        fixedPoints.push_back( pt );
        minX = std::min( minX, pt.x );
        minY = std::min( minY, pt.y );
        maxX = std::max( maxX, pt.x );
        maxY = std::max( maxY, pt.y );
    }

    const int xmin = std::max( 0, fixed_integer_floor( minX ) );
    const int ymin = std::max( 0, fixed_integer_floor( minY ) );
    const int xmax = std::min( width, fixed_integer_ceil( maxX ) );
    const int ymax = std::min( height, fixed_integer_ceil( maxY ) );

    if( xmax <= xmin || ymax <= ymin )
        return false;

    std::vector<EdgeInput> edges;
    edges.reserve( count );

    for( std::size_t i = 0; i < count; ++i )
    {
        FixedPoint p1 = fixedPoints[i];
        FixedPoint p2 = fixedPoints[( i + 1 ) % count];

        if( p1.y == p2.y )
            continue;

        int dir = 1;

        if( p1.y > p2.y )
        {
            std::swap( p1, p2 );
            dir = -1;
        }

        edges.push_back( EdgeInput{
            FixedLine{ p1, p2 },
            p1.y,
            p2.y,
            dir
        } );
    }

    if( edges.empty() )
        return false;

    MonoScanConverter converter( xmin, ymin, xmax, ymax, edges.size() );

    for( const EdgeInput& edge : edges )
        converter.AddEdge( edge );

    return converter.Generate( std::forward<EmitSpan>( emitSpan ), ~0u );
}

} // namespace detail::cairo_mono
