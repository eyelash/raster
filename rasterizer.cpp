/*

Copyright (c) 2017-2018, Elias Aebi
All rights reserved.

*/

#include "rasterizer.hpp"
#include "png.hpp"
#include <vector>
#include <map>
#include <queue>
#include <algorithm>
#include <utility>
#include <cmath>

namespace {

struct ShapeMap: std::map<const Shape*, int> {
	void modify(const Shape* shape, int direction) {
		auto iter = find(shape);
		if (iter != end()) {
			iter->second += direction;
			if (iter->second == 0) {
				erase(iter);
			}
		}
		else {
			insert(std::make_pair(shape, direction));
		}
	}
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

struct Strip {
	float y0, y1;
	std::vector<RasterizeLine> lines;
	Strip(float y0, float y1): y0(y0), y1(y1) {}
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
		shapes.modify(l0.shape, l0.direction);
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
			const size_t index = lines.size();
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

	float y = events.empty() ? 0.f : events.top().y;
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

	write_png(pixmap, file_name);
}
