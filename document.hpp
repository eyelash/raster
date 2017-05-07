/*

Copyright (c) 2017, Elias Aebi
All rights reserved.

*/

#include "rasterizer.hpp"

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
