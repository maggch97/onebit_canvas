#include "onebit_canvas_idskbmp.hpp"

#include <zstd.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
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


std::uint32_t u32_at( const std::vector<std::uint8_t>& b, std::size_t at )
{
    return onebit::idskbmp::detail::read_le32( b.data() + at );
}


std::uint64_t u64_at( const std::vector<std::uint8_t>& b, std::size_t at )
{
    return onebit::idskbmp::detail::read_le64( b.data() + at );
}


// Reference vectors produced with the upstream xxhash.h (XXH64, seed 0).
void test_xxh64_vectors()
{
    using onebit::idskbmp::detail::xxh64;

    struct Vector
    {
        std::string   text;
        std::uint64_t expected;
    };

    const Vector vectors[] = {
        { "", 0xef46db3751d8e999ULL },
        { "a", 0xd24ec4f1a98c6e5bULL },
        { "abc", 0x44bc2cf5ad770999ULL },
        { "Nobody inspects the spammish repetition", 0xfbcea83c8a378bf1ULL },
        { "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefXYZ", 0x5b15f946643395e4ULL },
    };

    for( const Vector& v : vectors )
        expect_true( xxh64( v.text.data(), v.text.size() ) == v.expected, "xxh64 of \"" + v.text + "\"" );

    std::vector<std::uint8_t> bytes( 1000 );

    for( int i = 0; i < 1000; ++i )
        bytes[i] = static_cast<std::uint8_t>( i * 31 + 7 );

    expect_true( xxh64( bytes.data(), 1000 ) == 0x99594f4828043d35ULL, "xxh64 of 1000-byte pattern" );
    expect_true( xxh64( bytes.data(), 37 ) == 0x5d71d9fc4b676d9fULL, "xxh64 of 37-byte pattern" );
}


onebit::Canvas make_pattern_canvas( int width, int height )
{
    onebit::Canvas canvas( width, height );

    // Row-dependent spans so every row differs and row-order mistakes show up.
    for( int y = 0; y < height; ++y )
        canvas.FillSpan1bpp( y, ( y * 7 ) % width, std::min( width, ( y * 7 ) % width + 1 + y % 97 ), true );

    return canvas;
}


std::vector<std::uint8_t> bmp_bytes( const onebit::Canvas& canvas, int dpiX, int dpiY, bool flip,
                                     onebit::BmpRowOrder order )
{
    std::ostringstream stream( std::ios::binary );
    canvas.WriteBmp( stream, dpiX, dpiY, flip, order );
    const std::string text = stream.str();
    return std::vector<std::uint8_t>( text.begin(), text.end() );
}


// Parses a container with the format's own rules (independently of the writer) and returns the
// BMP it expands to; reports any structural or hash violation.
std::vector<std::uint8_t> unpack( const std::vector<std::uint8_t>& file, const std::string& tag )
{
    using onebit::idskbmp::detail::xxh64;

    std::vector<std::uint8_t> bmp;

    if( file.size() < 40 || std::memcmp( file.data(), "IDSKBMP1", 8 ) != 0 )
    {
        expect_true( false, tag + ": magic" );
        return bmp;
    }

    const std::uint32_t codec = u32_at( file, 8 );
    const std::uint32_t headerLen = u32_at( file, 12 );
    const std::uint32_t chunkRows = u32_at( file, 16 );
    const std::uint32_t chunkCount = u32_at( file, 20 );
    const std::uint64_t pixelBytes = u64_at( file, 24 );
    const std::uint64_t tableHash = u64_at( file, 32 );

    expect_true( codec == 1, tag + ": codec" );

    const std::size_t tableStart = 40 + headerLen;
    const std::size_t payloadStart = tableStart + static_cast<std::size_t>( chunkCount ) * 24;

    std::vector<std::uint8_t> hashed( file.begin(), file.begin() + 32 );
    hashed.insert( hashed.end(), file.begin() + 40, file.begin() + payloadStart );
    expect_true( xxh64( hashed.data(), hashed.size() ) == tableHash, tag + ": tableHash" );

    bmp.assign( file.begin() + 40, file.begin() + 40 + headerLen );
    expect_true( u32_at( bmp, 10 ) == headerLen, tag + ": bfOffBits == bmpHeaderLen" );

    const std::int32_t width = static_cast<std::int32_t>( u32_at( bmp, 18 ) );
    const std::int32_t height = static_cast<std::int32_t>( u32_at( bmp, 22 ) );
    const std::uint64_t stride = ( ( static_cast<std::uint64_t>( width ) + 31 ) / 32 ) * 4;
    const std::uint64_t rows = static_cast<std::uint64_t>( height < 0 ? -height : height );

    expect_true( stride * rows == pixelBytes, tag + ": pixelBytes" );
    expect_true( chunkCount == ( rows + chunkRows - 1 ) / chunkRows, tag + ": chunkCount" );

    std::uint64_t expectedOffset = payloadStart;

    for( std::uint32_t i = 0; i < chunkCount; ++i )
    {
        const std::size_t   entry = tableStart + static_cast<std::size_t>( i ) * 24;
        const std::uint64_t offset = u64_at( file, entry );
        const std::uint32_t compressed = u32_at( file, entry + 8 );
        const std::uint32_t raw = u32_at( file, entry + 12 );
        const std::uint64_t payloadHash = u64_at( file, entry + 16 );

        const std::uint64_t chunkFirstRow = static_cast<std::uint64_t>( i ) * chunkRows;
        const std::uint64_t chunkRowCount = std::min<std::uint64_t>( chunkRows, rows - chunkFirstRow );

        expect_true( offset == expectedOffset, tag + ": chunk offsets contiguous" );
        expect_true( raw == chunkRowCount * stride, tag + ": rawSize" );
        expect_true( xxh64( file.data() + offset, compressed ) == payloadHash, tag + ": payloadHash" );
        expect_true( ZSTD_getFrameContentSize( file.data() + offset, compressed ) == raw,
                     tag + ": frame content size" );

        std::vector<std::uint8_t> out( raw );
        const std::size_t n = ZSTD_decompress( out.data(), out.size(), file.data() + offset, compressed );
        expect_true( !ZSTD_isError( n ) && n == raw, tag + ": decompress" );
        bmp.insert( bmp.end(), out.begin(), out.end() );
        expectedOffset = offset + compressed;
    }

    expect_true( expectedOffset == file.size(), tag + ": last chunk ends the file" );
    return bmp;
}


std::vector<std::uint8_t> container_bytes( const onebit::Canvas& canvas, int dpiX, int dpiY, bool flip,
                                           onebit::BmpRowOrder order, unsigned threads )
{
    std::vector<std::uint8_t>                                head;
    std::vector<onebit::idskbmp::detail::CompressedChunk> chunks;
    std::string                                              error;

    expect_true( onebit::idskbmp::build_idskbmp( canvas, dpiX, dpiY, flip, order, head, chunks, &error, threads ),
                 "build_idskbmp: " + error );

    for( const auto& chunk : chunks )
        head.insert( head.end(), chunk.bytes.begin(), chunk.bytes.end() );

    return head;
}


void test_container_expands_to_bmp_bytes()
{
    // stride 1024 B -> 4096 rows per chunk; 9000 rows -> 3 chunks, the last one short.
    const onebit::Canvas canvas = make_pattern_canvas( 8192, 9000 );

    for( onebit::BmpRowOrder order : { onebit::BmpRowOrder::TopDown, onebit::BmpRowOrder::BottomUp } )
    {
        for( bool flip : { false, true } )
        {
            const std::string tag = std::string( order == onebit::BmpRowOrder::TopDown ? "top-down" : "bottom-up" )
                                    + ( flip ? " flipped" : "" );
            const std::vector<std::uint8_t> file = container_bytes( canvas, 1440, 720, flip, order, 0 );

            expect_true( u32_at( file, 20 ) == 3, tag + ": three chunks" );
            expect_true( unpack( file, tag ) == bmp_bytes( canvas, 1440, 720, flip, order ),
                         tag + ": container expands to the BMP bytes" );
        }
    }

    // The thread count must not change the output.
    expect_true( container_bytes( canvas, 600, 600, false, onebit::BmpRowOrder::BottomUp, 1 )
                         == container_bytes( canvas, 600, 600, false, onebit::BmpRowOrder::BottomUp, 8 ),
                 "single- and multi-threaded containers are identical" );
}


void test_bottom_up_bmp_layout()
{
    const onebit::Canvas canvas = make_pattern_canvas( 37, 5 );
    const std::vector<std::uint8_t> topDown = bmp_bytes( canvas, 0, 0, false, onebit::BmpRowOrder::TopDown );
    const std::vector<std::uint8_t> bottomUp = bmp_bytes( canvas, 0, 0, false, onebit::BmpRowOrder::BottomUp );
    const std::size_t stride = static_cast<std::size_t>( canvas.StrideBytes() );

    expect_true( static_cast<std::int32_t>( u32_at( topDown, 22 ) ) == -5, "top-down biHeight is negative" );
    expect_true( static_cast<std::int32_t>( u32_at( bottomUp, 22 ) ) == 5, "bottom-up biHeight is positive" );
    expect_true( std::equal( topDown.begin(), topDown.begin() + 22, bottomUp.begin() ),
                 "headers agree before biHeight" );
    expect_true( std::equal( topDown.begin() + 26, topDown.begin() + 62, bottomUp.begin() + 26 ),
                 "headers agree after biHeight" );

    for( int r = 0; r < 5; ++r )
    {
        expect_true( std::equal( topDown.begin() + 62 + r * stride, topDown.begin() + 62 + ( r + 1 ) * stride,
                                 bottomUp.begin() + 62 + ( 4 - r ) * stride ),
                     "bottom-up stores rows in reverse" );
    }
}


void test_write_to_file()
{
    const onebit::Canvas canvas = make_pattern_canvas( 100, 30 );
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "onebit_canvas_idskbmp_test.idskbmp";
    std::string error;

    expect_true( onebit::idskbmp::write_idskbmp( canvas, path, 300, 300, false, onebit::BmpRowOrder::BottomUp,
                                                 &error ),
                 "write_idskbmp: " + error );

    std::ifstream stream( path, std::ios::binary );
    std::vector<std::uint8_t> file( ( std::istreambuf_iterator<char>( stream ) ), std::istreambuf_iterator<char>() );
    stream.close();
    std::filesystem::remove( path );

    expect_true( unpack( file, "file" ) == bmp_bytes( canvas, 300, 300, false, onebit::BmpRowOrder::BottomUp ),
                 "written file expands to the BMP bytes" );
}

} // namespace


int main()
{
    test_xxh64_vectors();
    test_container_expands_to_bmp_bytes();
    test_bottom_up_bmp_layout();
    test_write_to_file();

    if( g_failures != 0 )
    {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }

    std::cout << "onebit_canvas idskbmp tests passed\n";
    return 0;
}
