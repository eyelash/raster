/*

Copyright (c) 2017-2018, Elias Aebi
All rights reserved.

*/

#include "rasterizer.hpp"
#include <cmath>
#include <algorithm>

struct Transformation {
	// +-     -+
	// | a c e |
	// | b d f |
	// | 0 0 1 |
	// +-     -+
	float a, b, c, d, e, f;
	constexpr Transformation(float a, float b, float c, float d, float e, float f): a(a), b(b), c(c), d(d), e(e), f(f) {}
	constexpr Transformation(): Transformation(1.f, 0.f, 0.f, 1.f, 0.f, 0.f) {}
	static constexpr Transformation scale(float x, float y) {
		return Transformation(x, 0.f, 0.f, y, 0.f, 0.f);
	}
	static constexpr Transformation translate(float x, float y) {
		return Transformation(1.f, 0.f, 0.f, 1.f, x, y);
	}
	static Transformation rotate(float a) {
		const float c = std::cos(a);
		const float s = std::sin(a);
		return Transformation(c, s, -s, c, 0.f, 0.f);
	}
	Transformation invert() const {
		const float det = a * d - b * c;
		return Transformation(
			d / det,
			-b / det,
			-c / det,
			a / det,
			(c * f - d * e) / det,
			(b * e - a * f) / det
		);
	}
};

constexpr Point operator *(const Transformation& t, const Point& p) {
	return Point(
		t.a * p.x + t.c * p.y + t.e,
		t.b * p.x + t.d * p.y + t.f
	);
}

constexpr Transformation operator *(const Transformation& t0, const Transformation& t1) {
	return Transformation(
		t0.a * t1.a + t0.c * t1.b,
		t0.b * t1.a + t0.d * t1.b,
		t0.a * t1.c + t0.c * t1.d,
		t0.b * t1.c + t0.d * t1.d,
		t0.a * t1.e + t0.c * t1.f + t0.e,
		t0.b * t1.e + t0.d * t1.f + t0.f
	);
}

struct CubicBezierCurve {
	Point p0, p1, p2, p3;
	constexpr CubicBezierCurve(const Point& p0, const Point& p1, const Point& p2, const Point& p3): p0(p0), p1(p1), p2(p2), p3(p3) {}
	static constexpr Point reject(const Point& a, const Point& b) {
		return a - b * (dot(a, b) / dot(b, b));
	}
	float get_error_squared() const {
		const Point d = p3 - p0;
		const Point e1 = reject(p1 - p0, d);
		const Point e2 = reject(p2 - p0, d);
		return std::max(dot(e1, e1), dot(e2, e2)) * dot(d, d);
	}
};

struct Subpath {
	std::vector<Point> points;
	bool closed = false;
	static float length(const Point& p) {
		return std::sqrt(dot(p, p));
	}
	void push_offset_segment(const Point& p0, const Point& p1, float offset) {
		Point d = p1 - p0;
		if (length(d) == 0.f) {
			return;
		}
		d = d * (offset / length(d));
		d = Point(-d.y, d.x);
		points.push_back(p0 + d);
		points.push_back(p1 + d);
	}
};

struct Path {
	std::vector<Subpath> subpaths;
	Point current_point() const {
		if (subpaths.empty()) {
			return Point(0.f, 0.f);
		}
		const Subpath& subpath = subpaths.back();
		if (subpath.closed) {
			return subpath.points.front();
		}
		else {
			return subpath.points.back();
		}
	}
	void move_to(const Point& p) {
		subpaths.push_back(Subpath());
		subpaths.back().points.push_back(p);
	}
	void move_to(float x, float y) {
		move_to(Point(x, y));
	}
	void line_to(const Point& p) {
		if (subpaths.empty() || subpaths.back().closed) {
			move_to(current_point());
		}
		subpaths.back().points.push_back(p);
	}
	void line_to(float x, float y) {
		line_to(Point(x, y));
	}
	void curve_to(const Point& p1, const Point& p2, const Point& p3) {
		const Point& p0 = current_point();
		constexpr float tolerance = 1.f / 256.f;
		if (CubicBezierCurve(p0, p1, p2, p3).get_error_squared() < tolerance * tolerance) {
			line_to(p3);
		}
		else {
			const Point p4 = (p0 + p1) * .5f;
			const Point p5 = (p1 + p2) * .5f;
			const Point p6 = (p2 + p3) * .5f;
			const Point p7 = (p4 + p5) * .5f;
			const Point p8 = (p5 + p6) * .5f;
			const Point p9 = (p7 + p8) * .5f;
			curve_to(p4, p7, p9);
			curve_to(p8, p6, p3);
		}
	}
	void close() {
		subpaths.back().closed = true;
	}
	Path stroke(float width) const {
		const float offset = width / 2.f;
		Path path;
		for (const Subpath& subpath: subpaths) {
			const std::vector<Point>& points = subpath.points;
			Subpath new_subpath;
			for (size_t i = 1; i < points.size(); ++i) {
				new_subpath.push_offset_segment(points[i-1], points[i], offset);
			}
			if (subpath.closed) {
				new_subpath.push_offset_segment(points.back(), points.front(), offset);
				new_subpath.closed = true;
				path.subpaths.push_back(new_subpath);
				new_subpath = Subpath();
				new_subpath.push_offset_segment(points.front(), points.back(), offset);
			}
			for (size_t i = points.size() - 1; i > 0; --i) {
				new_subpath.push_offset_segment(points[i], points[i-1], offset);
			}
			new_subpath.closed = true;
			path.subpaths.push_back(new_subpath);
		}
		return path;
	}
};

inline Path operator *(const Transformation& t, Path path) {
	for (Subpath& subpath: path.subpaths) {
		for (Point& p: subpath.points) {
			p = t * p;
		}
	}
	return path;
}

struct ColorPaint: Paint {
	Color color;
	ColorPaint(const Color& color): color(color) {}
	Color evaluate(const Point& point) override {
		return color;
	}
};

struct Gradient {
	struct Stop {
		Color color;
		float pos;
		constexpr bool operator <(float pos) const {
			return this->pos < pos;
		}
	};
	std::vector<Stop> stops;
	Gradient() {}
	Gradient(std::initializer_list<Stop> stops): stops(stops) {}
	Color evaluate(float pos) const {
		// the stops need to be sorted
		if (stops.empty()) {
			return Color();
		}
		const auto i = std::lower_bound(stops.begin(), stops.end(), pos);
		if (i == stops.begin()) {
			return stops.front().color;
		}
		if (i == stops.end()) {
			return stops.back().color;
		}
		const auto i0 = i - 1;
		const float factor = (pos - i0->pos) / (i->pos - i0->pos);
		return i0->color * (1.f - factor) + i->color * factor;
	}
};

struct LinearGradient: Gradient {
	Point start;
	Point end;
	LinearGradient(const Point& start, const Point& end, std::initializer_list<Stop> stops): Gradient(stops), start(start), end(end) {}
	Color evaluate(const Point& p) const {
		const Point d = end - start;
		return Gradient::evaluate(dot(p - start, d) / dot(d, d));
	}
};

struct LinearGradientPaint: Paint {
	LinearGradient gradient;
	LinearGradientPaint(const LinearGradient& gradient): gradient(gradient) {}
	Color evaluate(const Point& point) override {
		return gradient.evaluate(point);
	}
};

struct OpacityPaint: Paint {
	std::shared_ptr<Paint> paint;
	float opacity;
	OpacityPaint(const std::shared_ptr<Paint>& paint, float opacity): paint(paint), opacity(opacity) {}
	Color evaluate(const Point& point) override {
		return paint->evaluate(point) * opacity;
	}
};

struct Style {
	std::shared_ptr<Paint> fill;
	float fill_opacity = 1.f;
	std::shared_ptr<Paint> stroke;
	float stroke_width = 1.f;
	float stroke_opacity = 1.f;
};

struct Document {
	std::vector<Shape> shapes;
	float width = 0.f;
	float height = 0.f;
	void append_segment(const Point& p0, const Point& p1) {
		if (p0.y != p1.y) {
			shapes.back().segments.push_back(Segment(p0, p1));
		}
	}
	void fill(const Path& path, const std::shared_ptr<Paint>& paint) {
		const int index = shapes.size();
		shapes.push_back(Shape(paint, index));
		for (const Subpath& subpath: path.subpaths) {
			const std::vector<Point>& points = subpath.points;
			for (size_t i = 1; i < points.size(); ++i) {
				append_segment(points[i-1], points[i]);
			}
			append_segment(points.back(), points.front());
		}
	}
	void stroke(const Path& path, const std::shared_ptr<Paint>& paint, float width) {
		fill(path.stroke(width), paint);
	}
	void draw(const Path& path, const Style& style, const Transformation& transformation = Transformation()) {
		if (style.fill && style.fill_opacity > 0.f) {
			fill(transformation * path, std::make_shared<OpacityPaint>(style.fill, style.fill_opacity));
		}
		if (style.stroke && style.stroke_width > 0.f && style.stroke_opacity > 0.f) {
			fill(transformation * path.stroke(style.stroke_width), std::make_shared<OpacityPaint>(style.stroke, style.stroke_opacity));
		}
	}
};
