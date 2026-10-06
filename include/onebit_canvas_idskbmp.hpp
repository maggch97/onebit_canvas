#pragma once

// Writes an onebit::Canvas as an ".idskbmp" container: the exact bytes of an uncompressed
// 1bpp BMP, with the pixel array split into row chunks that are compressed independently
// with zstd. Unpacking the chunks and concatenating them after the stored BMP header gives
// back, byte for byte, the BMP that Canvas::WriteBmp would have written with the same options.
//
// Layout (all integers little-endian), "IDSKBMP1":
//
//   offset  size  field
//   0       8     magic "IDSKBMP1"
//   8       4     u32 codec           1 = zstd
//   12      4     u32 bmpHeaderLen    = the BMP's bfOffBits
//   16      4     u32 chunkRows       rows per chunk; the last chunk may be shorter
//   20      4     u32 chunkCount      ceil( height / chunkRows )
//   24      8     u64 pixelBytes      stride * height
//   32      8     u64 tableHash       XXH64( bytes[0,32) || bmpHeader || chunkTable ), seed 0
//   40      N     bmpHeader           the BMP file header + DIB header + palette
//   40+N    24*C  chunkTable          per chunk: u64 offset (from file start), u32 compressedSize,
//                                     u32 rawSize, u64 payloadHash = XXH64( compressed bytes )
//   ...           payloads            one zstd frame per chunk, back to back, in table order
//
// Chunk i holds storage rows [ i * chunkRows, min( ( i + 1 ) * chunkRows, height ) ) of the
// BMP pixel array, i.e. rows in file order. chunkRows targets ~4 MiB of raw pixels per chunk.
// Every zstd frame carries a content checksum so readers detect corruption while decoding.
//
// This header needs <zstd.h> and the zstd library; the rest of onebit_canvas does not.

#include "onebit_canvas.hpp"

#include <zstd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace onebit
{

namespace idskbmp
{

inline constexpr char kMagic[8] = { 'I', 'D', 'S', 'K', 'B', 'M', 'P', '1' };
inline constexpr std::uint32_t kCodecZstd = 1;
inline constexpr std::uint32_t kFixedHeaderSize = 40;
inline constexpr std::uint32_t kChunkEntrySize = 24;
inline constexpr std::uint64_t kTargetChunkRawBytes = 4u << 20;
inline constexpr int kCompressionLevel = 1;


namespace detail
{

// XXH64 (seed 0 for every use in this format). Self-contained so this header does not depend on
// xxhash.h being installed next to zstd; checked against the reference implementation's vectors
// in tests/test_onebit_canvas_idskbmp.cpp.
inline constexpr std::uint64_t kXxhPrime1 = 11400714785074694791ULL;
inline constexpr std::uint64_t kXxhPrime2 = 14029467366897019727ULL;
inline constexpr std::uint64_t kXxhPrime3 = 1609587929392839161ULL;
inline constexpr std::uint64_t kXxhPrime4 = 9650029242287828579ULL;
inline constexpr std::uint64_t kXxhPrime5 = 2870177450012600261ULL;


inline std::uint64_t rotl64( std::uint64_t value, int bits )
{
    return ( value << bits ) | ( value >> ( 64 - bits ) );
}


inline std::uint64_t read_le64( const std::uint8_t* p )
{
    std::uint64_t value = 0;

    for( int i = 7; i >= 0; --i )
        value = ( value << 8 ) | p[i];

    return value;
}


inline std::uint32_t read_le32( const std::uint8_t* p )
{
    return static_cast<std::uint32_t>( p[0] ) | ( static_cast<std::uint32_t>( p[1] ) << 8 )
           | ( static_cast<std::uint32_t>( p[2] ) << 16 ) | ( static_cast<std::uint32_t>( p[3] ) << 24 );
}


inline std::uint64_t xxh64_round( std::uint64_t acc, std::uint64_t input )
{
    acc += input * kXxhPrime2;
    acc = rotl64( acc, 31 );
    return acc * kXxhPrime1;
}


inline std::uint64_t xxh64_merge_round( std::uint64_t acc, std::uint64_t value )
{
    acc ^= xxh64_round( 0, value );
    return acc * kXxhPrime1 + kXxhPrime4;
}


inline std::uint64_t xxh64( const void* data, std::size_t length, std::uint64_t seed = 0 )
{
    const std::uint8_t* p = static_cast<const std::uint8_t*>( data );
    const std::uint8_t* const end = p + length;
    std::uint64_t hash;

    if( length >= 32 )
    {
        const std::uint8_t* const limit = end - 32;
        std::uint64_t v1 = seed + kXxhPrime1 + kXxhPrime2;
        std::uint64_t v2 = seed + kXxhPrime2;
        std::uint64_t v3 = seed;
        std::uint64_t v4 = seed - kXxhPrime1;

        do
        {
            v1 = xxh64_round( v1, read_le64( p ) );
            v2 = xxh64_round( v2, read_le64( p + 8 ) );
            v3 = xxh64_round( v3, read_le64( p + 16 ) );
            v4 = xxh64_round( v4, read_le64( p + 24 ) );
            p += 32;
        } while( p <= limit );

        hash = rotl64( v1, 1 ) + rotl64( v2, 7 ) + rotl64( v3, 12 ) + rotl64( v4, 18 );
        hash = xxh64_merge_round( hash, v1 );
        hash = xxh64_merge_round( hash, v2 );
        hash = xxh64_merge_round( hash, v3 );
        hash = xxh64_merge_round( hash, v4 );
    }
    else
    {
        hash = seed + kXxhPrime5;
    }

    hash += static_cast<std::uint64_t>( length );

    while( end - p >= 8 )
    {
        hash ^= xxh64_round( 0, read_le64( p ) );
        hash = rotl64( hash, 27 ) * kXxhPrime1 + kXxhPrime4;
        p += 8;
    }

    if( end - p >= 4 )
    {
        hash ^= static_cast<std::uint64_t>( read_le32( p ) ) * kXxhPrime1;
        hash = rotl64( hash, 23 ) * kXxhPrime2 + kXxhPrime3;
        p += 4;
    }

    while( p < end )
    {
        hash ^= static_cast<std::uint64_t>( *p ) * kXxhPrime5;
        hash = rotl64( hash, 11 ) * kXxhPrime1;
        ++p;
    }

    hash ^= hash >> 33;
    hash *= kXxhPrime2;
    hash ^= hash >> 29;
    hash *= kXxhPrime3;
    hash ^= hash >> 32;
    return hash;
}


inline void append_le64( std::vector<std::uint8_t>& out, std::uint64_t value )
{
    for( int shift = 0; shift < 64; shift += 8 )
        out.push_back( static_cast<std::uint8_t>( ( value >> shift ) & 0xFFu ) );
}


inline void store_le64( std::uint8_t* p, std::uint64_t value )
{
    for( int i = 0; i < 8; ++i )
        p[i] = static_cast<std::uint8_t>( ( value >> ( 8 * i ) ) & 0xFFu );
}


inline void set_error( std::string* error, const std::string& message )
{
    if( error )
        *error = message;
}


struct CompressedChunk
{
    std::vector<std::uint8_t> bytes;
    std::uint32_t             rawSize = 0;
    std::uint64_t             hash = 0;
};

} // namespace detail


// Rows per chunk for a given stride: ~4 MiB of raw pixels, at least one row.
inline std::uint32_t chunk_rows_for_stride( int stride )
{
    return static_cast<std::uint32_t>(
            std::max<std::uint64_t>( 1, kTargetChunkRawBytes / static_cast<std::uint64_t>( stride ) ) );
}


// Builds the whole container in memory. Chunks are compressed on up to `threads` worker threads
// (0 = hardware concurrency). Returns false and fills *error on failure; never throws for
// zstd errors.
inline bool build_idskbmp( const Canvas& canvas, int dpiX, int dpiY, bool flipVertically,
                           BmpRowOrder rowOrder, std::vector<std::uint8_t>& head,
                           std::vector<detail::CompressedChunk>& chunks, std::string* error = nullptr,
                           unsigned threads = 0 )
{
    std::vector<std::uint8_t> bmpHeader;

    if( !onebit::detail::build_bmp_header( canvas.Width(), canvas.Height(), canvas.StrideBytes(), dpiX, dpiY,
                                           rowOrder == BmpRowOrder::BottomUp, bmpHeader ) )
    {
        detail::set_error( error, "canvas is empty or too large for a BMP" );
        return false;
    }

    const int           height = canvas.Height();
    const int           stride = canvas.StrideBytes();
    const std::uint32_t chunkRows = chunk_rows_for_stride( stride );
    const std::uint32_t chunkCount =
            static_cast<std::uint32_t>( ( static_cast<std::uint64_t>( height ) + chunkRows - 1 ) / chunkRows );

    chunks.assign( chunkCount, {} );

    if( threads == 0 )
        threads = std::max( 1u, std::thread::hardware_concurrency() );

    threads = std::min<unsigned>( threads, std::max<std::uint32_t>( 1, chunkCount ) );

    std::atomic<std::uint32_t> next{ 0 };
    std::mutex                 errorMutex;
    std::string                firstError;

    auto worker = [&]()
    {
        ZSTD_CCtx* cctx = ZSTD_createCCtx();

        if( !cctx )
        {
            std::lock_guard<std::mutex> lock( errorMutex );
            firstError = "ZSTD_createCCtx failed";
            return;
        }

        ZSTD_CCtx_setParameter( cctx, ZSTD_c_compressionLevel, kCompressionLevel );
        ZSTD_CCtx_setParameter( cctx, ZSTD_c_checksumFlag, 1 );

        std::vector<std::uint8_t> raw;

        for( std::uint32_t index = next++; index < chunkCount; index = next++ )
        {
            const int firstRow = static_cast<int>( static_cast<std::uint64_t>( index ) * chunkRows );
            const int rowCount = std::min<int>( static_cast<int>( chunkRows ), height - firstRow );
            const std::size_t rawSize = static_cast<std::size_t>( rowCount ) * static_cast<std::size_t>( stride );

            raw.resize( rawSize );

            for( int r = 0; r < rowCount; ++r )
            {
                std::memcpy( raw.data() + static_cast<std::size_t>( r ) * stride,
                             canvas.StoredRow( firstRow + r, flipVertically, rowOrder ),
                             static_cast<std::size_t>( stride ) );
            }

            detail::CompressedChunk& chunk = chunks[index];
            chunk.bytes.resize( ZSTD_compressBound( rawSize ) );
            const std::size_t written =
                    ZSTD_compress2( cctx, chunk.bytes.data(), chunk.bytes.size(), raw.data(), rawSize );

            if( ZSTD_isError( written ) )
            {
                std::lock_guard<std::mutex> lock( errorMutex );

                if( firstError.empty() )
                    firstError = std::string( "zstd compression failed: " ) + ZSTD_getErrorName( written );

                break;
            }

            chunk.bytes.resize( written );
            chunk.rawSize = static_cast<std::uint32_t>( rawSize );
            chunk.hash = detail::xxh64( chunk.bytes.data(), chunk.bytes.size() );
        }

        ZSTD_freeCCtx( cctx );
    };

    std::vector<std::thread> pool;

    for( unsigned t = 1; t < threads; ++t )
        pool.emplace_back( worker );

    worker();

    for( std::thread& thread : pool )
        thread.join();

    if( !firstError.empty() )
    {
        detail::set_error( error, firstError );
        return false;
    }

    head.clear();
    head.insert( head.end(), kMagic, kMagic + sizeof( kMagic ) );
    onebit::detail::append_le32( head, kCodecZstd );
    onebit::detail::append_le32( head, static_cast<std::uint32_t>( bmpHeader.size() ) );
    onebit::detail::append_le32( head, chunkRows );
    onebit::detail::append_le32( head, chunkCount );
    detail::append_le64( head, static_cast<std::uint64_t>( stride ) * static_cast<std::uint64_t>( height ) );
    detail::append_le64( head, 0 ); // tableHash, filled in below
    head.insert( head.end(), bmpHeader.begin(), bmpHeader.end() );

    std::uint64_t offset = kFixedHeaderSize + bmpHeader.size()
                           + static_cast<std::uint64_t>( chunkCount ) * kChunkEntrySize;

    for( const detail::CompressedChunk& chunk : chunks )
    {
        detail::append_le64( head, offset );
        onebit::detail::append_le32( head, static_cast<std::uint32_t>( chunk.bytes.size() ) );
        onebit::detail::append_le32( head, chunk.rawSize );
        detail::append_le64( head, chunk.hash );
        offset += chunk.bytes.size();
    }

    // tableHash covers the 32 fixed bytes before it plus everything after it in `head`.
    std::vector<std::uint8_t> hashed( head.begin(), head.begin() + 32 );
    hashed.insert( hashed.end(), head.begin() + kFixedHeaderSize, head.end() );
    detail::store_le64( head.data() + 32, detail::xxh64( hashed.data(), hashed.size() ) );
    return true;
}


// Writes the container to `path` (truncating it). This is a plain write, not an atomic replace:
// callers that need crash safety write to a temporary name and rename it themselves.
inline bool write_idskbmp( const Canvas& canvas, const std::filesystem::path& path, int dpiX = 0, int dpiY = 0,
                           bool flipVertically = false, BmpRowOrder rowOrder = BmpRowOrder::TopDown,
                           std::string* error = nullptr, unsigned threads = 0 )
{
    std::vector<std::uint8_t>            head;
    std::vector<detail::CompressedChunk> chunks;

    if( !build_idskbmp( canvas, dpiX, dpiY, flipVertically, rowOrder, head, chunks, error, threads ) )
        return false;

    std::ofstream stream( path, std::ios::binary | std::ios::trunc );

    if( !stream.is_open() )
    {
        detail::set_error( error, "cannot open " + path.string() );
        return false;
    }

    stream.write( reinterpret_cast<const char*>( head.data() ), static_cast<std::streamsize>( head.size() ) );

    for( const detail::CompressedChunk& chunk : chunks )
    {
        stream.write( reinterpret_cast<const char*>( chunk.bytes.data() ),
                      static_cast<std::streamsize>( chunk.bytes.size() ) );
    }

    stream.close();

    if( !stream.good() )
    {
        detail::set_error( error, "write failed: " + path.string() );
        return false;
    }

    return true;
}

} // namespace idskbmp


inline bool onebit_surface_write_to_idskbmp( Surface* surface, const std::filesystem::path& path,
                                             int dpiX = 0, int dpiY = 0, bool flipVertically = false,
                                             BmpRowOrder rowOrder = BmpRowOrder::TopDown,
                                             std::string* error = nullptr )
{
    if( surface == nullptr )
    {
        idskbmp::detail::set_error( error, "null surface" );
        return false;
    }

    return idskbmp::write_idskbmp( surface->canvas, path, dpiX, dpiY, flipVertically, rowOrder, error );
}

} // namespace onebit
