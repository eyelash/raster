/*

Copyright (c) 2017, Elias Aebi
All rights reserved.

*/

#include <vector>
#include <cstddef>

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

struct Shape {
	std::vector<Segment> segments;
	Fill* fill;
	int index;
	Shape(Fill* fill, int index): fill(fill), index(index) {}
};

void rasterize(const std::vector<Shape>& shapes, const char* file_name, size_t width, size_t height);
