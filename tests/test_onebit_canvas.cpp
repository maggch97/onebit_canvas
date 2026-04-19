#include "onebit_canvas.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

int g_failures = 0;


void expect_true( bool condition, const std::string& message )
{
    if( condition )
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
}


void expect_equal( int actual, int expected, const std::string& message )
{
    if( actual == expected )
        return;

    std::cerr << "FAIL: " << message << " expected=" << expected << " actual=" << actual << '\n';
    ++g_failures;
}


int count_set_pixels( const onebit::Canvas& canvas )
{
    int total = 0;

    for( int y = 0; y < canvas.Height(); ++y )
    {
        for( int x = 0; x < canvas.Width(); ++x )
            total += canvas.GetPixel( x, y ) ? 1 : 0;
    }

    return total;
}


void expect_canvas_equals( const onebit::Canvas& canvas, const std::vector<std::string>& rows,
                           const std::string& label )
{
    expect_equal( canvas.Height(), static_cast<int>( rows.size() ), label + " height" );

    if( rows.empty() )
        return;

    expect_equal( canvas.Width(), static_cast<int>( rows.front().size() ), label + " width" );

    for( int y = 0; y < canvas.Height() && y < static_cast<int>( rows.size() ); ++y )
    {
        for( int x = 0; x < canvas.Width() && x < static_cast<int>( rows[y].size() ); ++x )
        {
            const bool expected = rows[y][x] == '#';
            const bool actual = canvas.GetPixel( x, y );

            if( expected == actual )
                continue;

            std::cerr << "FAIL: " << label << " pixel mismatch at (" << x << "," << y
                      << ") expected=" << expected << " actual=" << actual << '\n';
            ++g_failures;
        }
    }
}


void test_blank_bmp()
{
    onebit::Canvas canvas( 8, 4 );
    canvas.Clear( false );

    const auto temp = std::filesystem::path( "onebit_canvas_blank.bmp" );
    const bool wrote = canvas.WriteBmp( temp, 300, 300 );
    expect_true( wrote, "WriteBmp should succeed for blank image" );

    if( !wrote )
        return;

    {
        std::ifstream stream( temp, std::ios::binary );
        std::vector<std::uint8_t> bytes( ( std::istreambuf_iterator<char>( stream ) ),
                                         std::istreambuf_iterator<char>() );

        const bool hasHeaders = bytes.size() >= 62;
        expect_true( hasHeaders, "BMP file should contain headers and pixel data" );

        if( !hasHeaders )
            return;

        expect_equal( bytes[0], 'B', "BMP signature byte 0" );
        expect_equal( bytes[1], 'M', "BMP signature byte 1" );

        const std::uint32_t rawWidth =
                static_cast<std::uint32_t>( bytes[18] )
                | ( static_cast<std::uint32_t>( bytes[19] ) << 8 )
                | ( static_cast<std::uint32_t>( bytes[20] ) << 16 )
                | ( static_cast<std::uint32_t>( bytes[21] ) << 24 );
        const std::uint32_t rawHeight =
                static_cast<std::uint32_t>( bytes[22] )
                | ( static_cast<std::uint32_t>( bytes[23] ) << 8 )
                | ( static_cast<std::uint32_t>( bytes[24] ) << 16 )
                | ( static_cast<std::uint32_t>( bytes[25] ) << 24 );

        const std::int32_t width = static_cast<std::int32_t>( rawWidth );
        const std::int32_t height = static_cast<std::int32_t>( rawHeight );

        expect_equal( width, 8, "BMP width should match canvas width" );
        expect_equal( height, -4, "BMP height should be negative for top-down output" );
    }

    std::filesystem::remove( temp );
}


void test_bmp_vertical_flip()
{
    onebit::Canvas canvas( 8, 2 );
    canvas.Clear( false );
    canvas.SetPixel( 0, 0, true );
    canvas.SetPixel( 7, 1, true );

    const auto normalPath = std::filesystem::path( "onebit_canvas_normal.bmp" );
    const auto flippedPath = std::filesystem::path( "onebit_canvas_flipped.bmp" );

    expect_true( canvas.WriteBmp( normalPath, 300, 300, false ),
                 "Normal BMP write should succeed" );
    expect_true( canvas.WriteBmp( flippedPath, 300, 300, true ),
                 "Flipped BMP write should succeed" );

    auto read_file = []( const std::filesystem::path& path )
    {
        std::ifstream stream( path, std::ios::binary );
        return std::vector<std::uint8_t>( ( std::istreambuf_iterator<char>( stream ) ),
                                          std::istreambuf_iterator<char>() );
    };

    const auto normalBytes = read_file( normalPath );
    const auto flippedBytes = read_file( flippedPath );

    expect_true( normalBytes.size() >= 70, "Normal BMP should contain full row data" );
    expect_true( flippedBytes.size() >= 70, "Flipped BMP should contain full row data" );

    if( normalBytes.size() >= 70 )
    {
        const std::uint32_t rawHeight =
                static_cast<std::uint32_t>( normalBytes[22] )
                | ( static_cast<std::uint32_t>( normalBytes[23] ) << 8 )
                | ( static_cast<std::uint32_t>( normalBytes[24] ) << 16 )
                | ( static_cast<std::uint32_t>( normalBytes[25] ) << 24 );
        expect_equal( static_cast<std::int32_t>( rawHeight ), -2,
                      "Normal BMP height should stay negative for top-down storage" );
        expect_equal( normalBytes[62], 0x80, "Normal BMP first row should contain the top row" );
        expect_equal( normalBytes[66], 0x01, "Normal BMP second row should contain the bottom row" );
    }

    if( flippedBytes.size() >= 70 )
    {
        const std::uint32_t rawHeight =
                static_cast<std::uint32_t>( flippedBytes[22] )
                | ( static_cast<std::uint32_t>( flippedBytes[23] ) << 8 )
                | ( static_cast<std::uint32_t>( flippedBytes[24] ) << 16 )
                | ( static_cast<std::uint32_t>( flippedBytes[25] ) << 24 );
        expect_equal( static_cast<std::int32_t>( rawHeight ), -2,
                      "Flipped BMP height should stay negative for top-down storage" );
        expect_equal( flippedBytes[62], 0x01, "Flipped BMP first row should contain the bottom row" );
        expect_equal( flippedBytes[66], 0x80, "Flipped BMP second row should contain the top row" );
    }

    std::filesystem::remove( normalPath );
    std::filesystem::remove( flippedPath );
}


void test_filled_rectangle()
{
    onebit::Canvas canvas( 8, 6 );
    canvas.Clear( false );

    const std::array<onebit::PointD, 4> rect = {{
        { 1.0, 1.0 }, { 5.0, 1.0 }, { 5.0, 4.0 }, { 1.0, 4.0 }
    }};

    expect_true( canvas.FillPolygon( rect, true ), "Rectangle polygon fill should succeed" );
    expect_equal( count_set_pixels( canvas ), 12, "Rectangle area should be 4x3 pixels" );
    expect_canvas_equals( canvas,
                          {
                              "........",
                              ".####...",
                              ".####...",
                              ".####...",
                              "........",
                              "........",
                          },
                          "rectangle" );
}


void test_triangle()
{
    onebit::Canvas canvas( 8, 8 );
    canvas.Clear( false );

    const std::array<onebit::PointD, 3> triangle = {{
        { 1.0, 1.0 }, { 5.0, 1.0 }, { 1.0, 5.0 }
    }};

    canvas.FillPolygon( triangle, true );

    expect_canvas_equals( canvas,
                          {
                              "........",
                              ".####...",
                              ".###....",
                              ".##.....",
                              ".#......",
                              "........",
                              "........",
                              "........",
                          },
                          "triangle" );
}


void test_concave_polygon()
{
    onebit::Canvas canvas( 8, 8 );
    canvas.Clear( false );

    const std::array<onebit::PointD, 6> concave = {{
        { 1.0, 1.0 }, { 6.0, 1.0 }, { 6.0, 3.0 }, { 3.0, 3.0 }, { 3.0, 6.0 }, { 1.0, 6.0 }
    }};

    canvas.FillPolygon( concave, true );

    expect_canvas_equals( canvas,
                          {
                              "........",
                              ".#####..",
                              ".#####..",
                              ".##.....",
                              ".##.....",
                              ".##.....",
                              "........",
                              "........",
                          },
                          "concave" );
}


void test_clear_polygon()
{
    onebit::Canvas canvas( 8, 8 );
    canvas.Clear( false );

    const std::array<onebit::PointD, 4> outer = {{
        { 0.0, 0.0 }, { 8.0, 0.0 }, { 8.0, 8.0 }, { 0.0, 8.0 }
    }};

    const std::array<onebit::PointD, 4> inner = {{
        { 2.0, 2.0 }, { 6.0, 2.0 }, { 6.0, 6.0 }, { 2.0, 6.0 }
    }};

    canvas.FillPolygon( outer, true );
    canvas.FillPolygon( inner, false );

    expect_canvas_equals( canvas,
                          {
                              "########",
                              "########",
                              "##....##",
                              "##....##",
                              "##....##",
                              "##....##",
                              "########",
                              "########",
                          },
                          "clear" );
}


void test_auto_close_matches_explicit_close()
{
    onebit::Canvas openCanvas( 8, 8 );
    onebit::Canvas closedCanvas( 8, 8 );
    openCanvas.Clear( false );
    closedCanvas.Clear( false );

    const std::array<onebit::PointD, 4> openRect = {{
        { 1.0, 1.0 }, { 5.0, 1.0 }, { 5.0, 4.0 }, { 1.0, 4.0 }
    }};

    const std::array<onebit::PointD, 5> closedRect = {{
        { 1.0, 1.0 }, { 5.0, 1.0 }, { 5.0, 4.0 }, { 1.0, 4.0 }, { 1.0, 1.0 }
    }};

    openCanvas.FillPolygon( openRect, true );
    closedCanvas.FillPolygon( closedRect, true );

    for( int y = 0; y < openCanvas.Height(); ++y )
    {
        for( int x = 0; x < openCanvas.Width(); ++x )
        {
            const std::string label = "Auto-close pixel mismatch at (" + std::to_string( x )
                                      + "," + std::to_string( y ) + ")";
            expect_true( openCanvas.GetPixel( x, y ) == closedCanvas.GetPixel( x, y ), label );
        }
    }
}


void test_context_stats()
{
    onebit::Canvas canvas( 8, 8 );
    onebit::Context context( canvas );

    context.MoveTo( 1.0, 1.0 );
    context.LineTo( 5.0, 1.0 );
    context.LineTo( 5.0, 5.0 );
    context.LineTo( 1.0, 5.0 );
    context.ClosePath();
    context.Fill();

    const onebit::RasterStats& stats = context.Stats();
    expect_equal( static_cast<int>( stats.polygon_count ), 1, "Context should record polygon count" );
    expect_equal( static_cast<int>( stats.total_vertex_count ), 4, "Context should record vertex count" );
    expect_equal( static_cast<int>( stats.max_vertex_count ), 4, "Context should record max vertex count" );
}


void test_large_canvas_support()
{
    onebit::Canvas canvas( 200000, 64 );
    canvas.Clear( false );

    const std::array<onebit::PointD, 4> polygon = {{
        { 100.0, 4.0 }, { 199900.0, 4.0 }, { 150000.0, 60.0 }, { 50000.0, 60.0 }
    }};

    expect_true( canvas.FillPolygon( polygon, true ),
                 "Large polygon fill should succeed on widths above 32767" );
    expect_true( canvas.GetPixel( 100000, 20 ),
                 "Large polygon should fill interior pixels on wide canvases" );
    expect_true( canvas.GetPixel( 60000, 50 ),
                 "Large polygon should fill lower interior pixels on wide canvases" );
    expect_true( !canvas.GetPixel( 1000, 20 ),
                 "Large polygon should leave left exterior pixels clear" );
    expect_true( !canvas.GetPixel( 199950, 20 ),
                 "Large polygon should leave right exterior pixels clear" );
}

} // namespace


int main()
{
    test_blank_bmp();
    test_bmp_vertical_flip();
    test_filled_rectangle();
    test_triangle();
    test_concave_polygon();
    test_clear_polygon();
    test_auto_close_matches_explicit_close();
    test_context_stats();
    test_large_canvas_support();

    if( g_failures != 0 )
    {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All onebit_canvas tests passed\n";
    return 0;
}
