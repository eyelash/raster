/*

Copyright (c) 2017-2018, Elias Aebi
All rights reserved.

*/

#include "rasterizer.hpp"
#include <vector>
#include <map>
#include <algorithm>
#include <utility>
#include <cstdio>
#include <png.h>

namespace {

template <class T> constexpr const T& clamp(const T& value, const T& min, const T& max) {
	return value < min ? min : (max < value ? max : value);
}

struct ShapeCompare {
	bool operator ()(const Shape* s0, const Shape* s1) {
		return s0->index < s1->index;
	}
};

struct ShapeMap: std::map<const Shape*, int, ShapeCompare> {
	Color get_color(const Point& point) const {
		Color color;
		for (auto& pair: *this) {
			color = blend(color, pair.first->paint->evaluate(point));
		}
		return color;
	}
};

struct RasterizeLine: Line {
	int direction;
	const Shape* shape;
	RasterizeLine(const Line& line, int direction, const Shape* shape): Line(line), direction(direction), shape(shape) {}
};

struct RasterizeSegment: Segment {
	int direction;
	const Shape* shape;
	static constexpr int get_direction(const Segment& s) {
		return s.y0 < s.y1 ? 1 : -1;
	}
	static constexpr Segment normalize_segment(const Segment& s) {
		return s.y0 < s.y1 ? s : Segment(s.y1, s.y0, s.line);
	}
	RasterizeSegment(const Segment& segment, const Shape* shape): Segment(normalize_segment(segment)), direction(get_direction(segment)), shape(shape) {}
	RasterizeLine get_line() const {
		return RasterizeLine(line, direction, shape);
	}
};

struct Strip {
	float y0, y1;
	std::vector<RasterizeLine> lines;
	Strip(float y0, float y1): y0(y0), y1(y1) {}
};

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

class Pixmap {
	std::vector<Color> pixels;
	size_t width;
public:
	Pixmap(size_t width, size_t height): pixels(width*height), width(width) {}
	size_t get_width() const {
		return width;
	}
	size_t get_height() const {
		return pixels.size() / width;
	}
	Color get_pixel(size_t x, size_t y) const {
		size_t i = y * width + x;
		return pixels[i];
	}
	void add_pixel(size_t x, size_t y, const Color& color) {
		size_t i = y * width + x;
		pixels[i] = pixels[i] + color;
	}
	void write(const char* file_name) const {
		size_t height = get_height();
		FILE* file = fopen(file_name, "wb");
		png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
		png_init_io(png, file);
		png_infop info = png_create_info_struct(png);
		png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		png_write_info(png, info);
		Random random;
		std::vector<unsigned char> row(width * 4);
		for (size_t y = 0; y < height; ++y) {
			for (size_t x = 0; x < width; ++x) {
				Color color = get_pixel(x, y).unpremultiply();
				row[x * 4 + 0] = random.dither(color.r);
				row[x * 4 + 1] = random.dither(color.g);
				row[x * 4 + 2] = random.dither(color.b);
				row[x * 4 + 3] = random.dither(color.a);
			}
			png_write_row(png, row.data());
		}
		png_write_end(png, info);
		png_destroy_write_struct(&png, &info);
		fclose(file);
	}
};

struct Trapezoid {

	//    y1    --------
	//         /       /
	//        /       /
	//       /       /
	//    y0 --------
	//      x0 x1   x2 x3

	float y0, y1;
	float x0, x1, x2, x3;
	constexpr Trapezoid(float y0, float y1, float x0, float x1, float x2, float x3): y0(y0), y1(y1), x0(x0), x1(x1), x2(x2), x3(x3) {}
	constexpr Trapezoid(float y0, float y1, const Line& l0, const Line& l1): y0(y0), y1(y1), x0(l0.get_x(y0)), x1(l0.get_x(y1)), x2(l1.get_x(y0)), x3(l1.get_x(y1)) {}
	constexpr float get_area() const {
		return (y1 - y0) * (x2 + x3 - x0 - x1) * .5f;
	}
};

float rasterize_pixel(const Trapezoid& trapezoid, size_t x) {
	// compute the intersection of the trapezoid and the pixel
	const float y0 = trapezoid.y0;
	const float y1 = trapezoid.y1;
	const float x0 = trapezoid.x0;
	const float x1 = trapezoid.x1;
	const float x2 = trapezoid.x2;
	const float x3 = trapezoid.x3;
	const float x4 = x;
	const float x5 = x + 1;

	// calculate the area assuming x4 >= x1 && x5 <= x2
	float area = y1 - y0;

	// and correct it if the assumtion is wrong
	if (x4 < x1) {
		const Line l0(Point(x0, y0), Point(x1, y1));
		if (x4 < x0) {
			area -= Trapezoid(y0, y1, x4, x4, x0, x1).get_area();
		}
		else {
			const float intersection = intersect(l0, Line(x4));
			area -= Trapezoid(intersection, y1, x4, x4, x4, x1).get_area();
		}
		if (x5 < x1) {
			const float intersection = intersect(l0, Line(x5));
			area += Trapezoid(intersection, y1, x5, x5, x5, x1).get_area();
		}
	}
	if (x5 > x2) {
		const Line l1(Point(x2, y0), Point(x3, y1));
		if (x5 > x3) {
			area -= Trapezoid(y0, y1, x2, x3, x5, x5).get_area();
		}
		else {
			const float intersection = intersect(l1, Line(x5));
			area -= Trapezoid(y0, intersection, x2, x5, x5, x5).get_area();
		}
		if (x4 > x2) {
			const float intersection = intersect(l1, Line(x4));
			area += Trapezoid(y0, intersection, x2, x4, x4, x4).get_area();
		}
	}

	return area;
}

void rasterize_row(const Strip& strip, Pixmap& pixmap, size_t y) {
	float y0 = std::max(static_cast<float>(y), strip.y0);
	float y1 = std::min(static_cast<float>(y+1), strip.y1);
	ShapeMap shapes;
	for (size_t i = 1; i < strip.lines.size(); ++i) {
		const RasterizeLine& l0 = strip.lines[i-1];
		auto iter = shapes.find(l0.shape);
		if (iter != shapes.end()) {
			iter->second += l0.direction;
			if (iter->second == 0) {
				shapes.erase(iter);
			}
		}
		else {
			shapes.insert(std::make_pair(l0.shape, l0.direction));
		}
		if (!shapes.empty()) {
			const RasterizeLine& l1 = strip.lines[i];
			Trapezoid trapezoid(y0, y1, l0, l1);
			if (trapezoid.x0 > trapezoid.x1) std::swap(trapezoid.x0, trapezoid.x1);
			if (trapezoid.x2 > trapezoid.x3) std::swap(trapezoid.x2, trapezoid.x3);
			const float x0 = std::max(trapezoid.x0, 0.f);
			const float x1 = std::min(trapezoid.x3, pixmap.get_width() - .5f);
			for (size_t x = x0; x < x1; ++x) {
				const float factor = rasterize_pixel(trapezoid, x);
				const Color color = shapes.get_color(Point(static_cast<float>(x) + .5f, static_cast<float>(y) + .5f));
				pixmap.add_pixel(x, y, color * factor);
			}
		}
	}
}

void rasterize_strip(const Strip& strip, Pixmap& pixmap) {
	const float y0 = std::max(strip.y0, 0.f);
	const float y1 = std::min(strip.y1, pixmap.get_height() - .5f);
	for (size_t y = y0; y < y1; ++y) {
		rasterize_row(strip, pixmap, y);
	}
}

}

void rasterize(const std::vector<Shape>& shapes, const char* file_name, size_t width, size_t height) {
	std::vector<RasterizeSegment> segments;
	std::vector<float> ys;

	// collect the segments and ys for each shape
	for (const Shape& shape: shapes) {
		for (const Segment& segment: shape.segments) {
			segments.push_back(RasterizeSegment(segment, &shape));
			ys.push_back(segment.y0);
			ys.push_back(segment.y1);
		}
	}

	// add additional ys for intersecting segments
	for (size_t i = 0; i < segments.size(); ++i) {
		for (size_t j = i + 1; j < segments.size(); ++j) {
			const Segment& s0 = segments[i];
			const Segment& s1 = segments[j];
			float y0 = std::max(s0.y0, s1.y0);
			float y1 = std::min(s0.y1, s1.y1);
			if (y0 < y1 && s0.line.m != s1.line.m) {
				float y = intersect(s0.line, s1.line);
				if (y0 < y && y < y1) {
					ys.push_back(y);
				}
			}
		}
	}

	std::sort(ys.begin(), ys.end());

	Pixmap pixmap(width, height);
	for (size_t i = 1; i < ys.size(); ++i) {
		const float y0 = ys[i-1];
		const float y1 = ys[i];
		if (y0 != y1) {
			Strip strip(y0, y1);
			for (const RasterizeSegment& segment: segments) {
				if (y0 < segment.y1 && segment.y0 < y1) {
					strip.lines.push_back(segment.get_line());
				}
			}
			const float cy = y0 + (y1 - y0) * .5f;
			std::sort(strip.lines.begin(), strip.lines.end(), [cy](const Line& l0, const Line& l1) {
				return l0.get_x(cy) < l1.get_x(cy);
			});
			rasterize_strip(strip, pixmap);
		}
	}
	pixmap.write(file_name);
}
