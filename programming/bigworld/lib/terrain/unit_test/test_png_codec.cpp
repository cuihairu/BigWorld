#include "pch.hpp"

#include "moo/png.hpp"

#include <string.h>

BW_BEGIN_NAMESPACE

/**
 * Tests for the PNG codec in lib/moo (png.cpp).
 *
 * libpng now comes from vcpkg (1.6.58) instead of the vendored 1.6.2 and
 * png.cpp was ported from libpng private structs to the public accessor
 * API. These tests pin the roundtrip behaviour (and the in-memory buffer
 * plumbing through png_get_io_ptr) that terrain height/AO/normal map
 * compression relies on.
 */

namespace // (anonymous)
{

// Generates a deterministic pattern for a w*h 1-byte-per-pixel image.
uint8 grayValue( uint32 x, uint32 y )
{
	return (uint8)(( x * 11 + y * 31 ) & 0xff);
}

} // end namespace (anonymous)


TEST( png_codec_gray_roundtrip )
{
	const uint32 WIDTH = 64;
	const uint32 HEIGHT = 48;

	BW::vector<uint8> pixels( WIDTH * HEIGHT );
	for (uint32 y = 0; y < HEIGHT; ++y)
	{
		for (uint32 x = 0; x < WIDTH; ++x)
		{
			pixels[ y * WIDTH + x ] = grayValue( x, y );
		}
	}

	Moo::PNGImageData src;
	src.data_ = &pixels[0];
	src.width_ = WIDTH;
	src.height_ = HEIGHT;
	src.bpp_ = 8;
	src.stride_ = WIDTH;
	src.upsideDown_ = false;

	BinaryPtr compressed = Moo::compressPNG( src,
		"TerrainUnitTest/PNGGray" );

	// A gradient-less pattern of this size compresses meaningfully but the
	// result is a real PNG stream, always at least the header + CRCs.
	CHECK( compressed );
	if (!compressed)
	{
		return;
	}
	CHECK( compressed->len() > 16 );

	Moo::PNGImageData dst;
	bool ok = Moo::decompressPNG( compressed, dst );
	CHECK( ok );

	if (ok)
	{
		CHECK( dst.width_ == WIDTH );
		CHECK( dst.height_ == HEIGHT );
		CHECK( dst.bpp_ == 8 );
		CHECK( dst.stride_ == WIDTH );
		CHECK( !dst.upsideDown_ );
		CHECK( memcmp( dst.data_, &pixels[0], pixels.size() ) == 0 );

		delete [] dst.data_;
	}
}


TEST( png_codec_rgba_roundtrip )
{
	const uint32 WIDTH = 33;	// deliberately not a power of two
	const uint32 HEIGHT = 17;

	BW::vector<uint8> pixels( WIDTH * HEIGHT * 4 );
	for (uint32 y = 0; y < HEIGHT; ++y)
	{
		for (uint32 x = 0; x < WIDTH; ++x)
		{
			uint8 *p = &pixels[ (y * WIDTH + x) * 4 ];
			p[0] = grayValue( x, y );
			p[1] = grayValue( y, x );
			p[2] = (uint8)(x * 8);
			p[3] = (uint8)(y * 15);
		}
	}

	Moo::PNGImageData src;
	src.data_ = &pixels[0];
	src.width_ = WIDTH;
	src.height_ = HEIGHT;
	src.bpp_ = 32;
	src.stride_ = WIDTH * 4;
	src.upsideDown_ = false;

	BinaryPtr compressed = Moo::compressPNG( src,
		"TerrainUnitTest/PNGRGBA" );
	CHECK( compressed );
	if (!compressed)
	{
		return;
	}

	Moo::PNGImageData dst;
	bool ok = Moo::decompressPNG( compressed, dst );
	CHECK( ok );

	if (ok)
	{
		CHECK( dst.width_ == WIDTH );
		CHECK( dst.height_ == HEIGHT );
		CHECK( dst.bpp_ == 32 );
		CHECK( memcmp( dst.data_, &pixels[0], pixels.size() ) == 0 );

		delete [] dst.data_;
	}
}


TEST( png_codec_invalid_stream_rejected )
{
	// Not a PNG: the reader must fail cleanly (via the error callback and
	// the exception escape hatch) instead of crashing.
	BW::vector<uint8> garbage( 64, 0xAB );
	BinaryPtr badData = new BinaryBlock( &garbage[0], garbage.size(),
		"TerrainUnitTest/PNGGarbage" );

	Moo::PNGImageData dst;
	CHECK( !Moo::decompressPNG( badData, dst ) );
}

BW_END_NAMESPACE
