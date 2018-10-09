/*

Copyright (c) 2017-2018, Elias Aebi
All rights reserved.

*/

#include "rasterizer.hpp"
#include <vector>
#include <map>
#include <queue>
#include <algorithm>
#include <utility>
#include <cmath>
#include <cstdio>
#include <png.h>

namespace {

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

struct Event {
	enum class Type {
		LINE_START,
		LINE_END
	};
	Type type;
	float y;
	size_t index;
	constexpr Event(Type type, float y, size_t index): type(type), y(y), index(index) {}
	constexpr bool operator >(const Event& event) const {
		return y > event.y;
	}
};

}

void rasterize(const std::vector<Shape>& shapes, const char* file_name, size_t width, size_t height) {
	// collect lines and events
	std::vector<RasterizeLine> lines;
	std::priority_queue<Event, std::vector<Event>, std::greater<Event>> events;
	for (const Shape& shape: shapes) {
		for (const Segment& s: shape.segments) {
			size_t index = lines.size();
			if (s.y0 < s.y1) {
				lines.push_back(RasterizeLine(s.line, 1, &shape));
				events.push(Event(Event::Type::LINE_START, s.y0, index));
				events.push(Event(Event::Type::LINE_END, s.y1, index));
			}
			else {
				lines.push_back(RasterizeLine(s.line, -1, &shape));
				events.push(Event(Event::Type::LINE_START, s.y1, index));
				events.push(Event(Event::Type::LINE_END, s.y0, index));
			}
		}
	}

	Pixmap pixmap(width, height);

	float y = events.top().y;
	std::vector<const RasterizeLine*> current_lines;
	while (!events.empty()) {
		Event event = events.top();
		events.pop();
		while (y < event.y) {
			std::sort(current_lines.begin(), current_lines.end(), [y](const RasterizeLine* l0, const RasterizeLine* l1) {
				const float x0 = l0->get_x(y);
				const float x1 = l1->get_x(y);
				if (x0 == x1) {
					return l0->m < l1->m;
				}
				return x0 < x1;
			});
			float next_y = event.y;
			// find intersections
			for (size_t i = 1; i < current_lines.size(); ++i) {
				const RasterizeLine& l0 = *current_lines[i-1];
				const RasterizeLine& l1 = *current_lines[i];
				if (l0.m != l1.m) {
					const float intersection = intersect(l0, l1);
					if (y < intersection && intersection < next_y) {
						next_y = intersection;
					}
				}
			}
			Strip strip(y, next_y);
			for (const RasterizeLine* line: current_lines) {
				strip.lines.push_back(*line);
			}
			rasterize_strip(strip, pixmap);
			y = next_y;
		}
		switch (event.type) {
		case Event::Type::LINE_START:
			current_lines.push_back(&lines[event.index]);
			break;
		case Event::Type::LINE_END:
			current_lines.erase(std::find(current_lines.begin(), current_lines.end(), &lines[event.index]));
			break;
		}
	}

	pixmap.write(file_name);
}
