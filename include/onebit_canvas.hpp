#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace onebit
{

struct PointD
{
    double x = 0.0;
    double y = 0.0;
};

} // namespace onebit

#include "onebit_canvas_cairo_mono.hpp"

namespace onebit
{


struct RasterStats
{
    std::size_t polygon_count = 0;
    std::size_t total_vertex_count = 0;
    std::size_t max_vertex_count = 0;
};


namespace detail
{

inline constexpr double raster_x_threshold = 130.0 / 256.0;

inline std::uint8_t first_mask( int x0 )
{
    return static_cast<std::uint8_t>( 0xFFu >> ( x0 & 7 ) );
}


inline std::uint8_t last_mask( int x1 )
{
    const int rem = x1 & 7;

    if( rem == 0 )
        return 0xFFu;

    return static_cast<std::uint8_t>( 0xFFu << ( 8 - rem ) );
}


inline void write_le16( std::ostream& os, std::uint16_t value )
{
    const char bytes[2] = {
        static_cast<char>( value & 0xFFu ),
        static_cast<char>( ( value >> 8 ) & 0xFFu )
    };

    os.write( bytes, sizeof( bytes ) );
}


inline void write_le32( std::ostream& os, std::uint32_t value )
{
    const char bytes[4] = {
        static_cast<char>( value & 0xFFu ),
        static_cast<char>( ( value >> 8 ) & 0xFFu ),
        static_cast<char>( ( value >> 16 ) & 0xFFu ),
        static_cast<char>( ( value >> 24 ) & 0xFFu )
    };

    os.write( bytes, sizeof( bytes ) );
}


inline void write_le32s( std::ostream& os, std::int32_t value )
{
    write_le32( os, static_cast<std::uint32_t>( value ) );
}


inline int dpi_to_pixels_per_meter( int dpi )
{
    if( dpi <= 0 )
        return 0;

    return static_cast<int>( std::llround( static_cast<double>( dpi ) / 0.0254 ) );
}


inline int clamp_ceil_to_int( double value )
{
    if( value <= static_cast<double>( std::numeric_limits<int>::min() ) )
        return std::numeric_limits<int>::min();

    if( value >= static_cast<double>( std::numeric_limits<int>::max() ) )
        return std::numeric_limits<int>::max();

    return static_cast<int>( std::ceil( value ) );
}


inline int clamp_floor_to_int( double value )
{
    if( value <= static_cast<double>( std::numeric_limits<int>::min() ) )
        return std::numeric_limits<int>::min();

    if( value >= static_cast<double>( std::numeric_limits<int>::max() ) )
        return std::numeric_limits<int>::max();

    return static_cast<int>( std::floor( value ) );
}

} // namespace detail


class Canvas
{
public:
    Canvas() = default;

    Canvas( int width, int height )
    {
        Reset( width, height );
    }

    void Reset( int width, int height )
    {
        if( width < 0 || height < 0 )
            throw std::invalid_argument( "Canvas dimensions must be non-negative" );

        m_width = width;
        m_height = height;
        m_stride = ( ( width + 31 ) & ~31 ) >> 3;
        m_bits.assign( static_cast<std::size_t>( m_stride ) * static_cast<std::size_t>( m_height ), 0 );
        ResetStats();
    }

    int Width() const noexcept { return m_width; }
    int Height() const noexcept { return m_height; }
    int StrideBytes() const noexcept { return m_stride; }

    const std::uint8_t* Data() const noexcept { return m_bits.data(); }
    std::uint8_t* Data() noexcept { return m_bits.data(); }

    const RasterStats& Stats() const noexcept { return m_stats; }

    void ResetStats() noexcept
    {
        m_stats = {};
    }

    void Clear( bool value )
    {
        std::fill( m_bits.begin(), m_bits.end(), value ? 0xFFu : 0x00u );
    }

    bool GetPixel( int x, int y ) const noexcept
    {
        if( x < 0 || y < 0 || x >= m_width || y >= m_height )
            return false;

        const std::size_t index = static_cast<std::size_t>( y ) * static_cast<std::size_t>( m_stride )
                                  + static_cast<std::size_t>( x >> 3 );
        const std::uint8_t bit = static_cast<std::uint8_t>( 0x80u >> ( x & 7 ) );
        return ( m_bits[index] & bit ) != 0;
    }

    void SetPixel( int x, int y, bool value )
    {
        if( x < 0 || y < 0 || x >= m_width || y >= m_height )
            return;

        std::uint8_t& cell = m_bits[static_cast<std::size_t>( y ) * static_cast<std::size_t>( m_stride )
                                   + static_cast<std::size_t>( x >> 3 )];
        const std::uint8_t bit = static_cast<std::uint8_t>( 0x80u >> ( x & 7 ) );

        if( value )
            cell |= bit;
        else
            cell &= static_cast<std::uint8_t>( ~bit );
    }

    void FillSpan1bpp( int y, int x0, int x1, bool value )
    {
        if( y < 0 || y >= m_height )
            return;

        x0 = std::max( x0, 0 );
        x1 = std::min( x1, m_width );

        if( x0 >= x1 )
            return;

        std::uint8_t* row = m_bits.data() + static_cast<std::size_t>( y ) * static_cast<std::size_t>( m_stride );
        const int firstByte = x0 >> 3;
        const int lastByte = ( x1 - 1 ) >> 3;

        if( firstByte == lastByte )
        {
            const std::uint8_t mask =
                    static_cast<std::uint8_t>( detail::first_mask( x0 ) & detail::last_mask( x1 ) );

            if( value )
                row[firstByte] |= mask;
            else
                row[firstByte] &= static_cast<std::uint8_t>( ~mask );

            return;
        }

        const std::uint8_t firstMask = detail::first_mask( x0 );
        const std::uint8_t lastMask = detail::last_mask( x1 );

        if( value )
        {
            row[firstByte] |= firstMask;

            if( lastByte - firstByte > 1 )
                std::memset( row + firstByte + 1, 0xFF, static_cast<std::size_t>( lastByte - firstByte - 1 ) );

            row[lastByte] |= lastMask;
        }
        else
        {
            row[firstByte] &= static_cast<std::uint8_t>( ~firstMask );

            if( lastByte - firstByte > 1 )
                std::memset( row + firstByte + 1, 0x00, static_cast<std::size_t>( lastByte - firstByte - 1 ) );

            row[lastByte] &= static_cast<std::uint8_t>( ~lastMask );
        }
    }

    bool FillPolygon( const PointD* points, std::size_t count, bool value )
    {
        if( points == nullptr || count < 3 || m_width <= 0 || m_height <= 0 )
            return false;

        m_stats.polygon_count += 1;
        m_stats.total_vertex_count += count;
        m_stats.max_vertex_count = std::max( m_stats.max_vertex_count, count );

        return detail::cairo_mono::RasterizePolygon(
                points, count, m_width, m_height,
                [this, value]( int y, int x0, int x1 )
                {
                    FillSpan1bpp( y, x0, x1, value );
                } );
    }

    template <typename Container>
    bool FillPolygon( const Container& points, bool value )
    {
        return FillPolygon( points.data(), points.size(), value );
    }

    bool WriteBmp( const std::filesystem::path& path, int dpiX = 0, int dpiY = 0,
                   bool flipVertically = false ) const
    {
        std::ofstream stream( path, std::ios::binary );

        if( !stream.is_open() )
            return false;

        const bool ok = WriteBmp( stream, dpiX, dpiY, flipVertically );
        stream.close();
        return ok && stream.good();
    }

    bool WriteBmp( std::ostream& stream, int dpiX = 0, int dpiY = 0,
                   bool flipVertically = false ) const
    {
        if( m_width <= 0 || m_height <= 0 )
            return false;

        constexpr std::uint32_t fileHeaderSize = 14;
        constexpr std::uint32_t infoHeaderSize = 40;
        constexpr std::uint32_t paletteSize = 8;
        constexpr std::uint32_t pixelDataOffset = fileHeaderSize + infoHeaderSize + paletteSize;

        const std::uint64_t imageSize = static_cast<std::uint64_t>( m_stride )
                                       * static_cast<std::uint64_t>( m_height );
        const std::uint64_t fileSize = pixelDataOffset + imageSize;

        if( fileSize > std::numeric_limits<std::uint32_t>::max() )
            return false;

        if( m_height > std::numeric_limits<std::int32_t>::max() )
            return false;

        detail::write_le16( stream, 0x4D42u );
        detail::write_le32( stream, static_cast<std::uint32_t>( fileSize ) );
        detail::write_le16( stream, 0u );
        detail::write_le16( stream, 0u );
        detail::write_le32( stream, pixelDataOffset );

        detail::write_le32( stream, infoHeaderSize );
        detail::write_le32s( stream, static_cast<std::int32_t>( m_width ) );
        detail::write_le32s( stream, -static_cast<std::int32_t>( m_height ) );
        detail::write_le16( stream, 1u );
        detail::write_le16( stream, 1u );
        detail::write_le32( stream, 0u );
        detail::write_le32( stream, static_cast<std::uint32_t>( imageSize ) );
        detail::write_le32s( stream, detail::dpi_to_pixels_per_meter( dpiX ) );
        detail::write_le32s( stream, detail::dpi_to_pixels_per_meter( dpiY ) );
        detail::write_le32( stream, 2u );
        detail::write_le32( stream, 2u );

        const char palette[8] = {
            static_cast<char>( 0xFF ), static_cast<char>( 0xFF ), static_cast<char>( 0xFF ), 0x00,
            0x00, 0x00, 0x00, 0x00
        };

        stream.write( palette, sizeof( palette ) );

        for( int row = 0; row < m_height; ++row )
        {
            const int sourceRow = flipVertically ? ( m_height - 1 - row ) : row;
            const std::uint8_t* rowData =
                    m_bits.data() + static_cast<std::size_t>( sourceRow ) * static_cast<std::size_t>( m_stride );

            stream.write( reinterpret_cast<const char*>( rowData ),
                          static_cast<std::streamsize>( m_stride ) );
        }

        return stream.good();
    }

private:
    int m_width = 0;
    int m_height = 0;
    int m_stride = 0;
    std::vector<std::uint8_t> m_bits;
    RasterStats m_stats;
};


class Context
{
public:
    explicit Context( Canvas& canvas ) :
            m_canvas( &canvas )
    {}

    void SetValue( bool value ) noexcept
    {
        m_sourceValue = value;
    }

    void SetClearMode( bool clear ) noexcept
    {
        m_clearMode = clear;
    }

    bool CurrentValue() const noexcept
    {
        return effectiveValue();
    }

    void MoveTo( double x, double y )
    {
        m_path.clear();
        m_path.push_back( PointD{ x, y } );
        m_closed = false;
    }

    void LineTo( double x, double y )
    {
        if( m_path.empty() )
            MoveTo( x, y );
        else
            m_path.push_back( PointD{ x, y } );
    }

    void ClosePath() noexcept
    {
        m_closed = true;
    }

    void ClearPath() noexcept
    {
        m_path.clear();
        m_closed = false;
    }

    void Fill()
    {
        if( m_path.size() >= 3 )
            m_canvas->FillPolygon( m_path.data(), m_path.size(), effectiveValue() );

        ClearPath();
    }

    void Rectangle( double x, double y, double width, double height )
    {
        MoveTo( x, y );
        LineTo( x + width, y );
        LineTo( x + width, y + height );
        LineTo( x, y + height );
        ClosePath();
    }

    void Paint()
    {
        m_canvas->Clear( effectiveValue() );
    }

    bool WriteToBmp( const std::filesystem::path& path, int dpiX = 0, int dpiY = 0,
                     bool flipVertically = false ) const
    {
        return m_canvas->WriteBmp( path, dpiX, dpiY, flipVertically );
    }

    const RasterStats& Stats() const noexcept
    {
        return m_canvas->Stats();
    }

private:
    bool effectiveValue() const noexcept
    {
        return m_clearMode ? false : m_sourceValue;
    }

    Canvas* m_canvas = nullptr;
    bool m_sourceValue = true;
    bool m_clearMode = false;
    bool m_closed = false;
    std::vector<PointD> m_path;
};


struct Surface
{
    explicit Surface( int width, int height ) :
            canvas( width, height )
    {}

    Canvas canvas;
};


inline Surface* onebit_image_surface_create( int width, int height )
{
    return new Surface( width, height );
}


inline void onebit_surface_destroy( Surface* surface )
{
    delete surface;
}


inline Context* onebit_create( Surface* surface )
{
    if( surface == nullptr )
        return nullptr;

    return new Context( surface->canvas );
}


inline void onebit_destroy( Context* context )
{
    delete context;
}


inline void onebit_set_clear_mode( Context* context, bool clear )
{
    if( context )
        context->SetClearMode( clear );
}


inline void onebit_set_value( Context* context, bool value )
{
    if( context )
        context->SetValue( value );
}


inline void onebit_move_to( Context* context, double x, double y )
{
    if( context )
        context->MoveTo( x, y );
}


inline void onebit_line_to( Context* context, double x, double y )
{
    if( context )
        context->LineTo( x, y );
}


inline void onebit_close_path( Context* context )
{
    if( context )
        context->ClosePath();
}


inline void onebit_fill( Context* context )
{
    if( context )
        context->Fill();
}


inline void onebit_clear_path( Context* context )
{
    if( context )
        context->ClearPath();
}


inline void onebit_rectangle( Context* context, double x, double y, double width, double height )
{
    if( context )
        context->Rectangle( x, y, width, height );
}


inline void onebit_paint( Context* context )
{
    if( context )
        context->Paint();
}


inline bool onebit_surface_write_to_bmp( Surface* surface, const std::filesystem::path& path,
                                         int dpiX = 0, int dpiY = 0,
                                         bool flipVertically = false )
{
    return surface != nullptr && surface->canvas.WriteBmp( path, dpiX, dpiY, flipVertically );
}


namespace cairo_like
{

using surface_t = Surface;
using context_t = Context;

enum format_t
{
    FORMAT_MONO = 0
};

enum status_t
{
    STATUS_SUCCESS = 0,
    STATUS_INVALID = 1
};

enum antialias_t
{
    ANTIALIAS_DEFAULT = 0,
    ANTIALIAS_NONE = 1
};

enum operator_t
{
    OPERATOR_OVER = 0,
    OPERATOR_CLEAR = 1
};

enum line_cap_t
{
    LINE_CAP_ROUND = 0
};

enum line_join_t
{
    LINE_JOIN_ROUND = 0
};

inline bool rgba_to_value( double r, double g, double b, double a ) noexcept
{
    if( a <= 0.0 )
        return false;

    const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return luma < 0.5;
}


inline surface_t* image_surface_create( format_t, int width, int height )
{
    return onebit_image_surface_create( width, height );
}


inline void surface_destroy( surface_t* surface )
{
    onebit_surface_destroy( surface );
}


inline status_t surface_status( const surface_t* surface ) noexcept
{
    return surface != nullptr ? STATUS_SUCCESS : STATUS_INVALID;
}


inline context_t* create( surface_t* surface )
{
    return onebit_create( surface );
}


inline void destroy( context_t* context )
{
    onebit_destroy( context );
}


inline status_t status( const context_t* context ) noexcept
{
    return context != nullptr ? STATUS_SUCCESS : STATUS_INVALID;
}


inline void set_antialias( context_t*, antialias_t ) noexcept {}


inline void set_source_rgba( context_t* context, double r, double g, double b, double a )
{
    onebit_set_value( context, rgba_to_value( r, g, b, a ) );
}


inline void set_operator( context_t* context, operator_t op )
{
    onebit_set_clear_mode( context, op == OPERATOR_CLEAR );
}


inline void paint( context_t* context )
{
    onebit_paint( context );
}


inline void set_line_cap( context_t*, line_cap_t ) noexcept {}


inline void set_line_join( context_t*, line_join_t ) noexcept {}


inline void set_line_width( context_t*, double ) noexcept {}


inline void move_to( context_t* context, double x, double y )
{
    onebit_move_to( context, x, y );
}


inline void line_to( context_t* context, double x, double y )
{
    onebit_line_to( context, x, y );
}


inline void close_path( context_t* context )
{
    onebit_close_path( context );
}


inline void fill( context_t* context )
{
    onebit_fill( context );
}


inline void rectangle( context_t* context, double x, double y, double width, double height )
{
    onebit_rectangle( context, x, y, width, height );
}


inline void surface_flush( surface_t* ) noexcept {}


inline bool surface_write_to_bmp( surface_t* surface, const std::filesystem::path& path,
                                  int dpiX = 0, int dpiY = 0,
                                  bool flipVertically = false )
{
    return onebit_surface_write_to_bmp( surface, path, dpiX, dpiY, flipVertically );
}


inline const RasterStats& stats( const context_t* context )
{
    static const RasterStats empty;
    return context != nullptr ? context->Stats() : empty;
}

} // namespace cairo_like

} // namespace onebit
