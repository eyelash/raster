/*

Copyright (c) 2017, Elias Aebi
All rights reserved.

*/

#include "rasterizer.hpp"
#include <cmath>

struct Subpath {
	std::vector<Point> points;
	bool closed;
	Subpath(): points(), closed(false) {}
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
	void move_to_relative(const Point& p) {
		move_to(current_point() + p);
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
	void line_to_relative(const Point& p) {
		line_to(current_point() + p);
	}
	void close() {
		subpaths.back().closed = true;
	}
};

struct Document {
	std::vector<Shape> shapes;
	float width, height;
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

inline Path operator *(const Transformation& t, Path path) {
	for (Subpath& subpath: path.subpaths) {
		for (Point& p: subpath.points) {
			p = t * p;
		}
	}
	return path;
}
