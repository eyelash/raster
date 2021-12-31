/*

Copyright (c) 2021, Elias Aebi
All rights reserved.

*/

#include "rasterizer.hpp"
#include "png.hpp"
#include <fstream>
#include <cmath>

namespace {

class Adler32 {
	std::uint32_t s1 = 1;
	std::uint32_t s2 = 0;
public:
	Adler32& operator <<(std::uint8_t data) {
		s1 = (s1 + data) % 65521;
		s2 = (s2 + s1) % 65521;
		return *this;
	}
	operator std::uint32_t() const {
		return s2 << 16 | s1;
	}
};

class Crc32 {
	std::uint32_t crc = ~0;
public:
	Crc32& operator <<(std::uint8_t data) {
		crc = crc ^ data;
		for (int j = 0; j < 8; ++j) {
			if (crc & 1) {
				crc = (crc >> 1) ^ 0xEDB88320;
			}
			else {
				crc = crc >> 1;
			}
		}
		return *this;
	}
	operator std::uint32_t() const {
		return ~crc;
	}
};

template <class S0, class S1> class CombineStreams {
	S0& s0;
	S1& s1;
public:
	CombineStreams(S0& s0, S1& s1): s0(s0), s1(s1) {}
	CombineStreams& operator <<(std::uint8_t data) {
		s0 << data;
		s1 << data;
		return *this;
	}
};
template <class S0, class S1> CombineStreams<S0, S1> combine_streams(S0& s0, S1& s1) {
	return CombineStreams<S0, S1>(s0, s1);
}

template <class T> T convert_endianness(T t) {
	T result = 0;
	for (std::size_t i = 0; i < sizeof(T); ++i) {
		result <<= 8;
		result |= t & 0xFF;
		t >>= 8;
	}
	return result;
}

template <class T, class S> void write(S& stream, T t) {
	t = convert_endianness<T>(t);
	for (std::size_t i = 0; i < sizeof(T); ++i) {
		stream << static_cast<std::uint8_t>(t & 0xFF);
		t >>= 8;
	}
}

template <class T, class S> void write(S& stream, std::initializer_list<T> ts) {
	for (T t: ts) {
		write(stream, t);
	}
}

class Random {
	uint64_t s[2] = {0xC0DEC0DEC0DEC0DE, 0xC0DEC0DEC0DEC0DE};
public:
	uint64_t next() {
		// xorshift128+
		const uint64_t result = s[0] + s[1];
		const uint64_t s1 = s[0] ^ (s[0] << 23);
		s[0] = s[1];
		s[1] = s1 ^ s[1] ^ (s1 >> 18) ^ (s[1] >> 5);
		return result;
	}
	float next_float() {
		return std::ldexp(static_cast<float>(next()), -64);
	}
	unsigned char dither(float value) {
		return clamp(value * 255.f + next_float(), 0.f, 255.f);
	}
};

}

void write_png(const Pixmap& pixmap, const char* file_name) {
	const std::uint32_t width = pixmap.get_width();
	const std::uint32_t height = pixmap.get_height();
	std::ofstream file(file_name);

	write<std::uint8_t>(file, {137, 'P', 'N', 'G', 13, 10, 26, 10});

	write<std::uint32_t>(file, 13); // IHDR chunk length
	Crc32 ihdr_crc;
	auto ihdr_stream = combine_streams(file, ihdr_crc);
	write<std::uint8_t>(ihdr_stream, {'I', 'H', 'D', 'R'});
	write<std::uint32_t>(ihdr_stream, width);
	write<std::uint32_t>(ihdr_stream, height);
	write<std::uint8_t>(ihdr_stream, 8); // bit depth
	write<std::uint8_t>(ihdr_stream, 6); // colour type = truecolor with alpha
	write<std::uint8_t>(ihdr_stream, 0); // compression method
	write<std::uint8_t>(ihdr_stream, 0); // filter method
	write<std::uint8_t>(ihdr_stream, 0); // interlace method
	write<std::uint32_t>(file, ihdr_crc);

	write<std::uint32_t>(file, (width * 4 + 6) * height + 6); // IDAT chunk length
	Crc32 idat_crc;
	auto idat_stream = combine_streams(file, idat_crc);
	write<std::uint8_t>(idat_stream, {'I', 'D', 'A', 'T'}); // chunk type
	const std::uint8_t cmf = 8 | (15 - 8) << 4; // compression method and info
	const std::uint8_t fdict = 0; // preset dictionary
	const std::uint8_t flevel = 0; // compression level
	const std::uint8_t fcheck = 31 - (cmf << 8 | fdict << 5 | flevel << 6) % 31;
	const std::uint8_t flg = fcheck | fdict << 5 | flevel << 6;
	write<std::uint8_t>(idat_stream, cmf);
	write<std::uint8_t>(idat_stream, flg);
	Adler32 adler;
	auto data_stream = combine_streams(idat_stream, adler);
	Random random;
	for (std::uint32_t y = 0; y < height; ++y) {
		const std::uint8_t is_final = y == height - 1;
		write<std::uint8_t>(idat_stream, is_final);
		const std::uint16_t length = convert_endianness<std::uint16_t>(1 + width * 4);
		write<std::uint16_t>(idat_stream, length);
		write<std::uint16_t>(idat_stream, ~length);
		write<std::uint8_t>(data_stream, 0); // filter type
		for (std::uint32_t x = 0; x < width; ++x) {
			const Color color = pixmap.get_pixel(x, y).unpremultiply();
			write<std::uint8_t>(data_stream, random.dither(color.r));
			write<std::uint8_t>(data_stream, random.dither(color.g));
			write<std::uint8_t>(data_stream, random.dither(color.b));
			write<std::uint8_t>(data_stream, random.dither(color.a));
		}
	}
	write<std::uint32_t>(idat_stream, adler);
	write<std::uint32_t>(file, idat_crc);

	write<std::uint32_t>(file, 0); // IEND chunk length
	Crc32 iend_crc;
	auto iend_stream = combine_streams(file, iend_crc);
	write<std::uint8_t>(iend_stream, {'I', 'E', 'N', 'D'}); // chunk type
	write<std::uint32_t>(file, iend_crc);
}
