#include "onebit_canvas.hpp"

#include <cairo.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{

int g_failures = 0;
std::string g_case_filter;


void expect_true( bool condition, const std::string& message )
{
    if( condition )
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
}


bool should_run_case( const std::string& label )
{
    return g_case_filter.empty() || label.find( g_case_filter ) != std::string::npos;
}


struct Lcg
{
    std::uint32_t state = 0x12345678u;

    double next_unit()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>( state ) / static_cast<double>( std::numeric_limits<std::uint32_t>::max() );
    }
};


std::vector<onebit::PointD> translated( const std::vector<onebit::PointD>& input, double dx, double dy )
{
    std::vector<onebit::PointD> output;
    output.reserve( input.size() );

    for( const auto& pt : input )
        output.push_back( onebit::PointD{ pt.x + dx, pt.y + dy } );

    return output;
}


std::vector<onebit::PointD> make_polar_polygon( int vertexCount, double centerX, double centerY,
                                                double minRadius, double maxRadius, double dx, double dy,
                                                Lcg& rng )
{
    std::vector<onebit::PointD> polygon;
    polygon.reserve( static_cast<std::size_t>( vertexCount ) );

    const double twoPi = 6.28318530717958647692;

    for( int i = 0; i < vertexCount; ++i )
    {
        const double baseAngle = twoPi * static_cast<double>( i ) / static_cast<double>( vertexCount );
        const double angleJitter = ( rng.next_unit() - 0.5 ) * ( twoPi / static_cast<double>( vertexCount ) ) * 0.35;
        const double radius = minRadius + ( maxRadius - minRadius ) * rng.next_unit();
        const double angle = baseAngle + angleJitter;

        polygon.push_back( onebit::PointD{
                centerX + dx + radius * std::cos( angle ),
                centerY + dy + radius * std::sin( angle )
        } );
    }

    return polygon;
}


std::vector<std::uint8_t> render_cairo_bits( cairo_format_t format, int width, int height,
                                             const std::vector<std::vector<onebit::PointD>>& fills,
                                             const std::vector<std::vector<onebit::PointD>>& clears )
{
    cairo_surface_t* surface = cairo_image_surface_create( format, width, height );
    cairo_t* context = cairo_create( surface );

    cairo_set_antialias( context, CAIRO_ANTIALIAS_NONE );
    cairo_set_operator( context, CAIRO_OPERATOR_SOURCE );
    cairo_set_source_rgba( context, 0.0, 0.0, 0.0, 0.0 );
    cairo_paint( context );

    auto draw_poly = [&]( const std::vector<onebit::PointD>& points, cairo_operator_t op )
    {
        if( points.size() < 3 )
            return;

        cairo_new_path( context );
        cairo_move_to( context, points[0].x, points[0].y );

        for( std::size_t i = 1; i < points.size(); ++i )
            cairo_line_to( context, points[i].x, points[i].y );

        cairo_close_path( context );
        cairo_set_operator( context, op );
        cairo_set_source_rgba( context, 0.0, 0.0, 0.0, 1.0 );
        cairo_fill( context );
    };

    for( const auto& poly : fills )
        draw_poly( poly, CAIRO_OPERATOR_OVER );

    for( const auto& poly : clears )
        draw_poly( poly, CAIRO_OPERATOR_CLEAR );

    cairo_surface_flush( surface );
    const int stride = cairo_image_surface_get_stride( surface );
    const unsigned char* data = cairo_image_surface_get_data( surface );
    std::vector<std::uint8_t> bits( static_cast<std::size_t>( width ) * static_cast<std::size_t>( height ), 0 );

    if( format == CAIRO_FORMAT_A1 )
    {
        for( int y = 0; y < height; ++y )
        {
            const auto* row = data + static_cast<std::size_t>( y ) * stride;

            for( int x = 0; x < width; ++x )
            {
                const std::uint8_t cell = row[x >> 3];
                const std::uint8_t bit = static_cast<std::uint8_t>( 0x80u >> ( x & 7 ) );
                bits[static_cast<std::size_t>( y ) * static_cast<std::size_t>( width ) + static_cast<std::size_t>( x )]
                        = ( cell & bit ) != 0 ? 1 : 0;
            }
        }
    }
    else if( format == CAIRO_FORMAT_A8 )
    {
        for( int y = 0; y < height; ++y )
        {
            const auto* row = data + static_cast<std::size_t>( y ) * stride;

            for( int x = 0; x < width; ++x )
            {
                bits[static_cast<std::size_t>( y ) * static_cast<std::size_t>( width ) + static_cast<std::size_t>( x )]
                        = row[x] != 0 ? 1 : 0;
            }
        }
    }
    else
    {
        for( int y = 0; y < height; ++y )
        {
            const auto* row = reinterpret_cast<const std::uint32_t*>( data + static_cast<std::size_t>( y ) * stride );

            for( int x = 0; x < width; ++x )
            {
                const std::uint8_t alpha = static_cast<std::uint8_t>( row[x] >> 24 );
                bits[static_cast<std::size_t>( y ) * static_cast<std::size_t>( width ) + static_cast<std::size_t>( x )]
                        = alpha != 0 ? 1 : 0;
            }
        }
    }

    cairo_destroy( context );
    cairo_surface_destroy( surface );
    return bits;
}


std::vector<std::uint8_t> render_onebit_bits( int width, int height,
                                              const std::vector<std::vector<onebit::PointD>>& fills,
                                              const std::vector<std::vector<onebit::PointD>>& clears )
{
    onebit::Canvas canvas( width, height );
    canvas.Clear( false );

    for( const auto& poly : fills )
        canvas.FillPolygon( poly, true );

    for( const auto& poly : clears )
        canvas.FillPolygon( poly, false );

    std::vector<std::uint8_t> bits( static_cast<std::size_t>( width ) * static_cast<std::size_t>( height ), 0 );

    for( int y = 0; y < height; ++y )
    {
        for( int x = 0; x < width; ++x )
        {
            bits[static_cast<std::size_t>( y ) * static_cast<std::size_t>( width ) + static_cast<std::size_t>( x )]
                    = canvas.GetPixel( x, y ) ? 1 : 0;
        }
    }

    return bits;
}


void compare_case( const std::string& label, int width, int height,
                   const std::vector<std::vector<onebit::PointD>>& fills,
                   const std::vector<std::vector<onebit::PointD>>& clears = {} )
{
    if( !should_run_case( label ) )
        return;

    const auto cairoBits = render_cairo_bits( CAIRO_FORMAT_ARGB32, width, height, fills, clears );
    const auto onebitBits = render_onebit_bits( width, height, fills, clears );

    expect_true( cairoBits.size() == onebitBits.size(), label + " size mismatch" );

    std::size_t diffCount = 0;

    for( std::size_t i = 0; i < cairoBits.size() && i < onebitBits.size(); ++i )
    {
        if( cairoBits[i] == onebitBits[i] )
            continue;

        if( diffCount < 10 )
        {
            const int x = static_cast<int>( i % static_cast<std::size_t>( width ) );
            const int y = static_cast<int>( i / static_cast<std::size_t>( width ) );
            std::cerr << "DIFF " << label << " at (" << x << "," << y << ") cairo="
                      << static_cast<int>( cairoBits[i] ) << " onebit="
                      << static_cast<int>( onebitBits[i] ) << '\n';
        }

        ++diffCount;
    }

    expect_true( diffCount == 0, label + " should match Cairo pixel-for-pixel" );
}

} // namespace


int main( int argc, char** argv )
{
    try
    {
        if( argc >= 2 )
            g_case_filter = argv[1];

        compare_case( "rectangle", 8, 6,
                      { { { 1.0, 1.0 }, { 5.0, 1.0 }, { 5.0, 4.0 }, { 1.0, 4.0 } } } );

        compare_case( "triangle", 8, 8,
                      { { { 1.0, 1.0 }, { 5.0, 1.0 }, { 1.0, 5.0 } } } );

        compare_case( "concave", 8, 8,
                      { { { 1.0, 1.0 }, { 6.0, 1.0 }, { 6.0, 3.0 }, { 3.0, 3.0 }, { 3.0, 6.0 }, { 1.0, 6.0 } } } );

        compare_case( "clear", 8, 8,
                      { { { 0.0, 0.0 }, { 8.0, 0.0 }, { 8.0, 8.0 }, { 0.0, 8.0 } } },
                      { { { 2.0, 2.0 }, { 6.0, 2.0 }, { 6.0, 6.0 }, { 2.0, 6.0 } } } );

        compare_case( "auto_close", 8, 6,
                      { { { 1.0, 1.0 }, { 5.0, 1.0 }, { 5.0, 4.0 }, { 1.0, 4.0 } } } );

        compare_case( "fractional_quad", 16, 16,
                      { { { 1.2, 1.3 }, { 11.7, 1.9 }, { 10.8, 8.6 }, { 0.9, 7.4 } } } );

        compare_case( "fractional_triangle", 16, 16,
                      { { { 2.15, 1.35 }, { 12.4, 4.8 }, { 3.55, 12.2 } } } );

        compare_case( "fractional_concave", 16, 16,
                      { { { 1.2, 1.4 }, { 13.6, 1.6 }, { 12.9, 5.2 }, { 6.8, 5.35 }, { 5.9, 12.1 }, { 1.1, 11.75 } } } );

        compare_case( "fractional_clear", 16, 16,
                      { { { 0.4, 0.6 }, { 14.7, 0.5 }, { 14.5, 14.2 }, { 0.5, 14.4 } } },
                      { { { 3.1, 2.8 }, { 11.6, 3.15 }, { 11.3, 10.85 }, { 3.4, 10.5 } } } );

        const std::array<onebit::PointD, 4> baseRect = {{
            { 1.0, 1.0 }, { 11.0, 1.0 }, { 10.5, 8.0 }, { 1.1, 7.9 }
        }};
        const std::array<onebit::PointD, 5> basePentagon = {{
            { 2.0, 0.8 }, { 12.3, 2.2 }, { 11.6, 8.1 }, { 6.1, 12.4 }, { 1.0, 9.7 }
        }};
        const std::array<onebit::PointD, 6> baseConcave = {{
            { 1.0, 1.0 }, { 13.0, 1.2 }, { 12.7, 4.8 }, { 7.2, 5.0 }, { 6.8, 12.8 }, { 0.9, 12.5 }
        }};

        const std::vector<std::vector<onebit::PointD>> translationShapes = {
            std::vector<onebit::PointD>( baseRect.begin(), baseRect.end() ),
            std::vector<onebit::PointD>( basePentagon.begin(), basePentagon.end() ),
            std::vector<onebit::PointD>( baseConcave.begin(), baseConcave.end() ),
        };
        const std::vector<onebit::PointD> translationOffsets = {
            { 0.125, 0.125 },
            { 0.25, 0.75 },
            { 0.49, 0.51 },
            { 0.875, 0.375 },
            { 1.2, 0.4 },
        };

        for( std::size_t shapeIndex = 0; shapeIndex < translationShapes.size(); ++shapeIndex )
        {
            for( std::size_t offsetIndex = 0; offsetIndex < translationOffsets.size(); ++offsetIndex )
            {
                const auto& offset = translationOffsets[offsetIndex];
                std::ostringstream label;
                label << "translated_shape_" << shapeIndex << "_" << offsetIndex;
                compare_case( label.str(), 18, 18,
                              { translated( translationShapes[shapeIndex], offset.x, offset.y ) } );
            }
        }

        Lcg rng;

        for( int caseIndex = 0; caseIndex < 24; ++caseIndex )
        {
            const int vertexCount = 5 + ( caseIndex % 5 );
            const double dx = ( rng.next_unit() - 0.5 ) * 1.8;
            const double dy = ( rng.next_unit() - 0.5 ) * 1.8;
            auto polygon = make_polar_polygon( vertexCount, 16.0, 16.0, 5.0, 11.0, dx, dy, rng );

            std::ostringstream label;
            label << "generated_fractional_" << caseIndex;
            compare_case( label.str(), 32, 32, { polygon } );
        }

        if( g_failures != 0 )
        {
            std::cerr << g_failures << " Cairo compare test(s) failed\n";
            return 1;
        }

        std::cout << "All Cairo compare tests passed\n";
        return 0;
    }
    catch( const std::exception& ex )
    {
        std::cerr << "EXCEPTION: " << ex.what() << '\n';
        return 2;
    }
}
