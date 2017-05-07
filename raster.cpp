/*

Copyright (c) 2017, Elias Aebi
All rights reserved.

*/

#include <vector>
#include <map>
#include <algorithm>
#include <utility>
#include <cstdio>
#include <png.h>

namespace {

struct Point {
	float x, y;
	constexpr Point(float x, float y): x(x), y(y) {}
	constexpr Point operator +(const Point& p) const {
		return Point(x + p.x, y + p.y);
	}
	constexpr Point operator -(const Point& p) const {
		return Point(x - p.x, y - p.y);
	}
	constexpr Point operator *(float f) const {
		return Point(x * f, y * f);
	}
};

constexpr float dot(const Point& p0, const Point& p1) {
	return p0.x * p1.x + p0.y * p1.y;
}

struct Line {
	float m, x0;
	constexpr Line(float m, const Point& p): m(m), x0(p.x - m * p.y) {}
	constexpr Line(const Point& p0, const Point& p1): Line((p1.x - p0.x) / (p1.y - p0.y), p0) {}
	constexpr Line(float x): m(0.f), x0(x) {}
	constexpr float get_x(float y) const {
		return m * y + x0;
	}
};

constexpr float intersect(const Line& l0, const Line& l1) {
	return (l1.x0 - l0.x0) / (l0.m - l1.m);
}

struct Segment {
	float y0, y1;
	Line line;
	constexpr Segment(const Point& p0, const Point& p1): y0(p0.y), y1(p1.y), line(p0, p1) {}
	constexpr Segment(float y0, float y1, const Line& line): y0(y0), y1(y1), line(line) {}
};

struct Color {
	float r, g, b, a;
	constexpr Color(float r, float g, float b, float a = 1.f): r(r), g(g), b(b), a(a) {}
	constexpr Color(): Color(0.f, 0.f, 0.f, 0.f) {}
	constexpr Color operator +(const Color& c) const {
		return Color(r + c.r, g + c.g, b + c.b, a + c.a);
	}
	constexpr Color operator *(float f) const {
		return Color(r * f, g * f, b * f, a * f);
	}
	constexpr Color unpremultiply() const {
		return a == 0.f ? Color() : Color(r/a, g/a, b/a, a);
	}
};

constexpr Color blend(const Color& dst, const Color& src) {
	return src + dst * (1.f - src.a);
}

struct Fill {
	virtual Color evaluate(const Point& point) = 0;
};

struct SolidFill: Fill {
	Color color;
	SolidFill(const Color& color): color(color) {}
	Color evaluate(const Point& point) override {
		return color;
	}
};

struct Gradient {
	struct Stop {
		Color color;
		float pos;
	};
	std::vector<Stop> stops;
	Gradient(std::initializer_list<Stop> stops): stops(stops) {}
	Color evaluate(float pos) {
		if (pos <= stops.front().pos) {
			return stops.front().color;
		}
		if (pos >= stops.back().pos) {
			return stops.back().color;
		}
		for (size_t i = 1; i < stops.size(); ++i) {
			if (pos <= stops[i].pos) {
				float factor = (pos - stops[i-1].pos) / (stops[i].pos - stops[i-1].pos);
				return stops[i-1].color * (1.f - factor) + stops[i].color * factor;
			}
		}
		return Color();
	}
};

struct LinearGradientFill: Fill {
	Gradient gradient;
	Point start;
	Point matrix;
	static constexpr Point get_matrix(const Point& start, const Point& d) {
		return d * (1.f / dot(d, d));
	}
	LinearGradientFill(const Point& start, const Point& end, std::initializer_list<Gradient::Stop> stops): gradient(stops), start(start), matrix(get_matrix(start, end - start)) {}
	Color evaluate(const Point& point) override {
		return gradient.evaluate(dot(matrix, point - start));
	}
};

struct Shape {
	std::vector<Segment> segments;
	Fill* fill;
	int index;
	Shape(Fill* fill, int index): fill(fill), index(index) {}
};

struct ShapeCompare {
	bool operator ()(const Shape* s0, const Shape* s1) {
		return s0->index < s1->index;
	}
};

struct ShapeMap: std::map<const Shape*, int, ShapeCompare> {
	Color get_color(const Point& point) const {
		Color color;
		for (auto& pair: *this) {
			color = blend(color, pair.first->fill->evaluate(point));
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
	void set_pixel(size_t x, size_t y, const Color& color) {
		size_t i = y * width + x;
		pixels[i] = color;
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
		for (size_t y = 0; y < height; ++y) {
			std::vector<unsigned char> row(width * 4);
			for (size_t x = 0; x < width; ++x) {
				Color color = get_pixel(x, y).unpremultiply();
				row[x * 4 + 0] = color.r * 255.f + .5f;
				row[x * 4 + 1] = color.g * 255.f + .5f;
				row[x * 4 + 2] = color.b * 255.f + .5f;
				row[x * 4 + 3] = color.a * 255.f + .5f;
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
				float factor = rasterize_pixel(trapezoid, x);
				Color color = shapes.get_color(Point(static_cast<float>(x) + .5f, static_cast<float>(y) + .5f));
				pixmap.add_pixel(x, y, color * factor);
			}
		}
	}
}

void rasterize_strip(const Strip& strip, Pixmap& pixmap) {
	float y0 = std::max(strip.y0, 0.f);
	float y1 = std::min(strip.y1, pixmap.get_height() - .5f);
	for (size_t y = y0; y < y1; ++y) {
		rasterize_row(strip, pixmap, y);
	}
}

Pixmap rasterize(const std::vector<Shape>& shapes, size_t width, size_t height) {
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
		float y0 = ys[i-1];
		float y1 = ys[i];
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
	return pixmap;
}

struct Subpath {
	std::vector<Point> points;
	bool closed;
	Subpath(): points(), closed(false) {}
};

struct Path {
	std::vector<Subpath> subpaths;
	void move_to(const Point& p) {
		subpaths.push_back(Subpath());
		subpaths.back().points.push_back(p);
	}
	void move_to(float x, float y) {
		move_to(Point(x, y));
	}
	void line_to(const Point& p) {
		subpaths.back().points.push_back(p);
	}
	void line_to(float x, float y) {
		line_to(Point(x, y));
	}
	void close() {
		subpaths.back().closed = true;
	}
};

struct Document {
	std::vector<Shape> shapes;
	void append_segment(const Point& p0, const Point& p1) {
		if (p0.y != p1.y) {
			shapes.back().segments.push_back(Segment(p0, p1));
		}
	}
	void fill(const Path& path, Fill* fill) {
		int index = shapes.size();
		shapes.push_back(Shape(fill, index));
		for (const Subpath& subpath: path.subpaths) {
			const std::vector<Point>& points = subpath.points;
			for (size_t i = 1; i < points.size(); ++i) {
				append_segment(points[i-1], points[i]);
			}
			append_segment(points.back(), points.front());
		}
	}
};

}

int main() {
	Document document;
	SolidFill blue(Color(0, 0, 1) * .85f);
	SolidFill yellow(Color(1, 1, 0) * .75f);

	{
		Path path;
		path.move_to( 50, 250);
		path.line_to(100,  50);
		path.line_to(150, 150);
		path.line_to(200, 100);
		path.line_to(250, 200);
		path.close();
		document.fill(path, &blue);
	}
	{
		Path path;
		path.move_to(100, 200);
		path.line_to(100,  50);
		path.line_to( 50, 150);
		path.close();
		document.fill(path, &yellow);
	}

	Pixmap pixmap = rasterize(document.shapes, 300, 300);
	pixmap.write("result.png");
}
