/*

Copyright (c) 2017-2018, Elias Aebi
All rights reserved.

*/

#include <vector>
#include <cstddef>
#include <memory>

constexpr float clamp(float value, float min, float max) {
	return value < min ? min : (max < value ? max : value);
}

struct Point {
	float x, y;
	constexpr Point(float x, float y): x(x), y(y) {}
	constexpr Point operator +(const Point& p) const {
		return Point(x + p.x, y + p.y);
	}
	constexpr Point operator -(const Point& p) const {
		return Point(x - p.x, y - p.y);
	}
	constexpr Point operator -() const {
		return Point(-x, -y);
	}
	constexpr Point operator *(float f) const {
		return Point(x * f, y * f);
	}
	constexpr bool operator ==(const Point& p) const {
		return x == p.x && y == p.y;
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
	static constexpr Color rgb(unsigned char r, unsigned char g, unsigned char b) {
		return Color(r / 255.f, g / 255.f, b / 255.f);
	}
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

struct Paint {
	virtual Color evaluate(const Point& point) = 0;
};

struct Shape {
	std::vector<Segment> segments;
	std::shared_ptr<Paint> paint;
	Shape(const std::shared_ptr<Paint>& paint): paint(paint) {}
	void append_segment(const Point& p0, const Point& p1) {
		if (p0.y != p1.y) {
			segments.emplace_back(p0, p1);
		}
	}
};

void rasterize(const std::vector<Shape>& shapes, const char* file_name, size_t width, size_t height);
