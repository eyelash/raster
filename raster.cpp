/*

Copyright (c) 2017, Elias Aebi
All rights reserved.

*/

#include <vector>
#include <set>
#include <algorithm>
#include <cstdio>
#include <png.h>

namespace {

struct Point {
	float x, y;
	constexpr Point(float x, float y): x(x), y(y) {}
	constexpr Point operator +(const Point& p) const {
		return Point(x + p.x, y + p.y);
	}
};

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

struct Polygon {
	std::vector<Point> points;
	Fill* fill;
	int index;
	mutable int n;
	Polygon(Fill* fill, int index, std::initializer_list<Point> points): points(points), fill(fill), index(index), n(0) {}
};

struct PolygonCompare {
	bool operator ()(const Polygon* p0, const Polygon* p1) {
		return p0->index < p1->index;
	}
};

struct PolygonSet: std::set<const Polygon*, PolygonCompare> {
	void remove(const Polygon* polygon) {
		auto i = find(polygon);
		erase(i);
	}
	Color get_color(const Point& point) const {
		Color color;
		for (const Polygon* polygon: *this) {
			color = blend(color, polygon->fill->evaluate(point));
		}
		return color;
	}
};

struct Document {
	std::vector<Polygon> polygons;
	Document(std::initializer_list<Polygon> polygons): polygons(polygons) {}
};

struct PolygonLine: Line {
	int direction;
	const Polygon* polygon;
	constexpr PolygonLine(const Point& p0, const Point& p1, int direction, const Polygon* polygon): Line(p0, p1), direction(direction), polygon(polygon) {}
};

struct Segment {
	float y0, y1;
	PolygonLine line;
	constexpr Segment(const Point& p0, const Point& p1, int direction, const Polygon* polygon): y0(p0.y), y1(p1.y), line(p0, p1, direction, polygon) {}
};

struct Strip {
	float y0, y1;
	std::vector<PolygonLine> lines;
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
	float y0, y1;
	Line l0, l1;
	constexpr Trapezoid(float y0, float y1, const Line& l0, const Line& l1): y0(y0), y1(y1), l0(l0), l1(l1) {}
	float get_area() const {
		const float height = y1 - y0;
		const float cy = y0 + height * .5f;
		return (l1.get_x(cy) - l0.get_x(cy)) * height;
	}
};

float rasterize_pixel(const Trapezoid& trapezoid, size_t x) {
	float area = trapezoid.get_area();

	// compute the intersection of the trapezoid and the pixel
	const float x0 = static_cast<float>(x);
	const float x1 = static_cast<float>(x + 1);
	const float y0 = trapezoid.y0;
	const float y1 = trapezoid.y1;

	//     x3 ------ x5
	//       /     /
	//      /     /
	//   x2 ------ x4
	float x2 = trapezoid.l0.get_x(y0);
	float x3 = trapezoid.l0.get_x(y1);
	float x4 = trapezoid.l1.get_x(y0);
	float x5 = trapezoid.l1.get_x(y1);
	if (x2 > x3) std::swap(x2, x3);
	if (x4 > x5) std::swap(x4, x5);
	
	Line l0(Point(x2, y0), Point(x3, y1));
	Line l1(Point(x4, y0), Point(x5, y1));
	
	if (x0 > x2) {
		Line line(x0);
		if (x0 > x3) {
			area -= Trapezoid(y0, y1, l0, line).get_area();
		}
		else {
			float intersection = intersect(l0, line);
			area -= Trapezoid(y0, intersection, l0, line).get_area();
		}
		if (x0 > x4) {
			float intersection = intersect(l1, line);
			area += Trapezoid(y0, intersection, l1, line).get_area();
		}
	}
	if (x1 < x5) {
		Line line(x1);
		if (x1 < x4) {
			area -= Trapezoid(y0, y1, line, l1).get_area();
		}
		else {
			float intersection = intersect(l1, line);
			area -= Trapezoid(intersection, y1, line, l1).get_area();
		}
		if (x1 < x3) {
			float intersection = intersect(l0, line);
			area += Trapezoid(intersection, y1, line, l0).get_area();
		}
	}
	return area;
}

void rasterize_row(const Strip& strip, Pixmap& pixmap, size_t y) {
	float y0 = std::max(static_cast<float>(y), strip.y0);
	float y1 = std::min(static_cast<float>(y+1), strip.y1);
	PolygonSet set;
	for (size_t i = 1; i < strip.lines.size(); ++i) {
		PolygonLine l0 = strip.lines[i-1];
		l0.polygon->n += l0.direction;
		if (l0.polygon->n != 0) {
			set.insert(l0.polygon);
		}
		else {
			set.remove(l0.polygon);
		}
		if (!set.empty()) {
			PolygonLine l1 = strip.lines[i];
			Trapezoid trapezoid(y0, y1, l0, l1);
			float x0 = std::min(l0.get_x(y0), l0.get_x(y1));
			float x1 = std::max(l1.get_x(y0), l1.get_x(y1));
			for (size_t x = x0; x < x1; ++x) {
				float factor = rasterize_pixel(trapezoid, x);
				Color color = set.get_color(Point(static_cast<float>(x) + .5f, static_cast<float>(y) + .5f));
				pixmap.add_pixel(x, y, color * factor);
			}
		}
	}
	PolygonLine l = strip.lines.back();
	l.polygon->n += l.direction;
}

void rasterize_strip(const Strip& strip, Pixmap& pixmap) {
	float y0 = std::max(0.f, strip.y0);
	float y1 = std::min(static_cast<float>(pixmap.get_height()), strip.y1);
	for (size_t y = y0; y < y1; ++y) {
		rasterize_row(strip, pixmap, y);
	}
}

void append_segment(std::vector<Segment>& segments, const Point& p0, const Point& p1, const Polygon* polygon) {
	if (p0.y < p1.y) {
		segments.push_back(Segment(p0, p1, 1, polygon));
	} else if (p0.y > p1.y) {
		segments.push_back(Segment(p1, p0, -1, polygon));
	}
}

Pixmap rasterize(const Document& document, size_t width, size_t height) {
	std::vector<Segment> segments;
	std::vector<float> ys;

	for (const Polygon& polygon: document.polygons) {
		const std::vector<Point>& points = polygon.points;

		for (size_t i = 1; i < points.size(); ++i) {
			append_segment(segments, points[i-1], points[i], &polygon);
		}
		append_segment(segments, points.back(), points[0], &polygon);

		for (const Point& p: points) {
			ys.push_back(p.y);
		}
	}

	// TODO: handle intersections

	std::sort(ys.begin(), ys.end());

	std::vector<Strip> strips;
	for (size_t i = 1; i < ys.size(); ++i) {
		float y0 = ys[i-1];
		float y1 = ys[i];
		if (y0 != y1) {
			Strip strip(y0, y1);
			for (const Segment& segment: segments) {
				if (y0 < segment.y1 && segment.y0 < y1) {
					strip.lines.push_back(segment.line);
				}
			}
			float cy = y0 + (y1 - y0) * .5f;
			std::sort(strip.lines.begin(), strip.lines.end(), [cy](const Line& l0, const Line& l1) {
				return l0.get_x(cy) < l1.get_x(cy);
			});
			strips.push_back(strip);
		}
	}

	Pixmap pixmap(width, height);
	for (const Strip& strip: strips) {
		rasterize_strip(strip, pixmap);
	}
	return pixmap;
}

}

int main() {
	SolidFill blue(Color(0, 0, 1) * .85f);
	SolidFill yellow(Color(1, 1, 0) * .7f);

	Document document {
		Polygon(&blue, 1, {
			Point( 50, 250),
			Point(100,  50),
			Point(150, 150),
			Point(200, 100),
			Point(250, 200)
		}),
		Polygon(&yellow, 2, {
			Point(100, 200),
			Point(100, 50),
			Point(150, 150)
		})
	};
	Pixmap pixmap = rasterize(document, 300, 300);
	pixmap.write("result.png");
}
