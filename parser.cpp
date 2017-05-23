/*

Copyright (c) 2017, Elias Aebi
All rights reserved.

*/

#include "parser.hpp"

class Parser {
	StringView s;
public:
	Parser(const StringView& s): s(s) {}
	StringView get() const {
		return s;
	}
	Parser copy() const {
		return Parser(s);
	}
	bool has_next() const {
		return s.has_next();
	}
	template <class F> bool parse(F&& f) {
		if (!s.has_next()) return false;
		StringView copy = s;
		if (!f(s.next())) {
			s = copy;
			return false;
		}
		return true;
	}
	template <class F> void parse_all(F&& f) {
		while (parse(f));
	}
	bool parse(Character c) {
		return parse([c](Character c2) {
			return c2 == c;
		});
	}
	bool parse(char c) {
		return parse(Character(c));
	}
	bool parse(StringView prefix) {
		StringView copy = s;
		while (prefix.has_next()) {
			if (!s.has_next() || s.next() != prefix.next()) {
				s = copy;
				return false;
			}
		}
		return true;
	}
	bool parse(const char* prefix) {
		return parse(StringView(prefix));
	}
	[[noreturn]] void error(std::string message) {
		throw message;
	}
	void expect(const StringView& s) {
		if (!parse(s)) error("expected " + s.to_string());
	}
};

constexpr bool numeric(Character c) {
	return c.between('0', '9');
}

constexpr bool white_space(Character c) {
	return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

constexpr bool white_space_or_comma(Character c) {
	return white_space(c) || c == ',';
}

constexpr bool any_char(Character c) {
	return true;
}

float parse_number_positive(Parser& p) {
	if (!p.copy().parse(numeric)) p.error("expected a number");
	float n = 0.f;
	p.parse_all([&](Character c) {
		if (c.between('0', '9')) {
			n *= 10.f;
			n += c.get_digit();
			return true;
		}
		else {
			return false;
		}
	});
	if (p.parse('.')) {
		float factor = .1f;
		p.parse_all([&](Character c) {
			if (c.between('0', '9')) {
				n += factor * c.get_digit();
				factor /= 10.f;
				return true;
			}
			else {
				return false;
			}
		});
	}
	return n;
}

float parse_number(Parser& p) {
	if (p.parse('-')) {
		return -parse_number_positive(p);
	}
	else {
		p.parse('+');
		return parse_number_positive(p);
	}
}

constexpr bool number_start_char(Character c) {
	return numeric(c) || c == '-';
}

class XMLParser: public Parser {
	static constexpr bool name_start_char(Character c) {
		return c.between('a', 'z') || c.between('A', 'Z') || c == ':' || c == '_';
	}
	static constexpr bool name_char(Character c) {
		return name_start_char(c) || c == '-' || c == '.' || c.between('0', '9');
	}
	StringView parse_name() {
		StringView start = get();
		if (!parse(name_start_char)) error("expected a name");
		parse_all(name_char);
		return get() - start;
	}
	StringView parse_attribute_value() {
		if (parse('"')) {
			StringView start = get();
			parse_all([](Character c) {
				return c != '"';
			});
			StringView value = get() - start;
			expect("\"");
			return value;
		}
		else if (parse('\'')) {
			StringView start = get();
			parse_all([](Character c) {
				return c != '\'';
			});
			StringView value = get() - start;
			expect("'");
			return value;
		}
		else {
			error("expected attribute value");
		}
	}
	void skip_misc() {
		while (true) {
			if (next_is_comment()) {
				parse_comment();
			}
			else if (parse(white_space)) {

			}
			else {
				break;
			}
		}
	}
public:
	XMLParser(const StringView& s): Parser(s) {}
	void skip_prolog() {
		if (parse("<?xml")) {
			while (!parse("?>")) {
				if (!parse(any_char)) error("unexpected end");
			}
		}
		skip_misc();
		while (parse("<!DOCTYPE")) {
			parse_all([](Character c) {
				return c != '>';
			});
			expect(">");
			skip_misc();
		}
	}
	bool next_is_start_tag() const {
		return copy().parse('<');
	}
	StringView parse_start_tag() {
		expect("<");
		StringView name = parse_name();
		parse_all(white_space);
		return name;
	}
	template <class F> void parse_attributes(F&& f) {
		while (copy().parse(name_start_char)) {
			StringView name = parse_name();
			parse_all(white_space);
			expect("=");
			parse_all(white_space);
			StringView value = parse_attribute_value();
			parse_all(white_space);
			f(name, value);
		}
		parse('>');
	}
	bool next_is_end_tag() const {
		return copy().parse("/>") || copy().parse("</");
	}
	void parse_end_tag(const StringView& name) {
		if (parse("/>")) {

		}
		else if (parse("</")) {
			if (!parse(name)) error("expected '" + name.to_string() + "'");
			parse_all(white_space);
			expect(">");
		}
		else {
			error("expected end tag");
		}
	}
	bool next_is_comment() const {
		return copy().parse("<!--");
	}
	void parse_comment() {
		expect("<!--");
		while (!parse("-->")) {
			if (!parse(any_char)) error("unexpected end");
		}
	}
	StringView parse_char_data() {
		StringView start = get();
		parse_all([](Character c) {
			return c != '<';
		});
		return get() - start;
	}
};

class PathParser: public Parser {
	Path& path;
	Point parse_point() {
		float x = parse_number(*this);
		parse_all(white_space_or_comma);
		float y = parse_number(*this);
		return Point(x, y);
	}
	using Parser::parse;
public:
	PathParser(const StringView& s, Path& path): Parser(s), path(path) {}
	void parse() {
		parse_all(white_space);
		while (has_next()) {
			if (parse('M')) {
				parse_all(white_space);
				path.move_to(parse_point());
				parse_all(white_space_or_comma);
				while (copy().parse(number_start_char)) {
					path.line_to(parse_point());
					parse_all(white_space_or_comma);
				}
			}
			if (parse('m')) {
				parse_all(white_space);
				path.move_to_relative(parse_point());
				parse_all(white_space_or_comma);
				while (copy().parse(number_start_char)) {
					path.line_to_relative(parse_point());
					parse_all(white_space_or_comma);
				}
			}
			else if (parse('L')) {
				parse_all(white_space);
				while (copy().parse(number_start_char)) {
					path.line_to(parse_point());
					parse_all(white_space_or_comma);
				}
			}
			else if (parse('l')) {
				parse_all(white_space);
				while (copy().parse(number_start_char)) {
					path.line_to_relative(parse_point());
					parse_all(white_space_or_comma);
				}
			}
			else if (parse('Z') || parse('z')) {
				path.close();
				parse_all(white_space);
			}
			else {
				error("unexpected command");
			}
		}
	}
};

class TransformParser: public Parser {
	using Parser::parse;
public:
	TransformParser(const StringView& s): Parser(s) {}
	Transformation parse() {
		Transformation t;
		parse_all(white_space);
		while (has_next()) {
			if (parse("matrix")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float a = parse_number(*this);
				parse_all(white_space_or_comma);
				const float b = parse_number(*this);
				parse_all(white_space_or_comma);
				const float c = parse_number(*this);
				parse_all(white_space_or_comma);
				const float d = parse_number(*this);
				parse_all(white_space_or_comma);
				const float e = parse_number(*this);
				parse_all(white_space_or_comma);
				const float f = parse_number(*this);
				parse_all(white_space);
				expect(")");
				t = t * Transformation(a, b, c, d, e, f);
			}
			else if (parse("translate")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float x = parse_number(*this);
				parse_all(white_space_or_comma);
				float y = 0.f;
				if (copy().parse(number_start_char)) {
					y = parse_number(*this);
					parse_all(white_space);
				}
				expect(")");
				t = t * Transformation::translate(x, y);
			}
			else if (parse("scale")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float x = parse_number(*this);
				parse_all(white_space_or_comma);
				float y = x;
				if (copy().parse(number_start_char)) {
					y = parse_number(*this);
					parse_all(white_space);
				}
				expect(")");
				t = t * Transformation::scale(x, y);
			}
			else if (parse("rotate")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float a = parse_number(*this) * (M_PI / 180.f);
				parse_all(white_space);
				if (copy().parse(number_start_char)) {
					const float x = parse_number(*this);
					parse_all(white_space_or_comma);
					const float y = parse_number(*this);
					parse_all(white_space);
					t = t * Transformation::translate(x, y) * Transformation::rotate(a) * Transformation::translate(-x, -y);
				}
				else {
					t = t * Transformation::rotate(a);
				}
				expect(")");
			}
			else if (parse("skewX")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float a = parse_number(*this) * (M_PI / 180.f);
				parse_all(white_space);
				expect(")");
				t = t * Transformation(1.f, 0.f, std::tan(a), 1.f, 0.f, 0.f);
			}
			else if (parse("skewY")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float a = parse_number(*this) * (M_PI / 180.f);
				parse_all(white_space);
				expect(")");
				t = t * Transformation(1.f, std::tan(a), 0.f, 1.f, 0.f, 0.f);
			}
			else {
				error("unexpected transformation");
			}
			parse_all(white_space);
		}
		return t;
	}
};

static SolidFill fill(Color(1, 1, 1) * .7f);

class SVGParser: public XMLParser {
	Document& document;
	Transformation transformation;
	void skip_tag() {
		StringView name = parse_start_tag();
		parse_attributes([](const StringView& name, const StringView& value) {});
		while (!next_is_end_tag()) {
			if (next_is_comment()) parse_comment();
			else if (next_is_start_tag()) skip_tag();
			else parse_char_data();
		}
		parse_end_tag(name);
	}
	void parse_tag() {
		StringView name = parse_start_tag();
		if (name == "path") {
			Path path;
			parse_attributes([&](const StringView& name, const StringView& value) {
				if (name == "d") {
					PathParser p(value, path);
					p.parse();
				}
			});
			document.fill(transformation * path, &fill);
			while (!next_is_end_tag()) {
				if (next_is_comment()) parse_comment();
				else if (next_is_start_tag()) skip_tag();
				else parse_char_data();
			}
		}
		if (name == "g") {
			Transformation previous_transformation = transformation;
			parse_attributes([&](const StringView& name, const StringView& value) {
				if (name == "transform") {
					TransformParser p(value);
					transformation = transformation * p.parse();
				}
			});
			while (!next_is_end_tag()) {
				if (next_is_comment()) parse_comment();
				else if (next_is_start_tag()) parse_tag();
				else parse_char_data();
			}
			transformation = previous_transformation;
		}
		else {
			parse_attributes([](const StringView& name, const StringView& value) {});
			while (!next_is_end_tag()) {
				if (next_is_comment()) parse_comment();
				else if (next_is_start_tag()) skip_tag();
				else parse_char_data();
			}
		}
		parse_end_tag(name);
	}
public:
	SVGParser(const StringView& s, Document& document): XMLParser(s), document(document) {}
	void parse() {
		skip_prolog();
		StringView name = parse_start_tag();
		if (name != "svg") error("expected svg tag");
		struct {
			float x, y;
			float width, height;
		} view_box;
		bool has_view_box = false;
		parse_attributes([&](const StringView& name, const StringView& value) {
			if (name == "viewBox") {
				Parser p(value);
				p.parse_all(::white_space);
				view_box.x = parse_number(p);
				p.parse_all(white_space_or_comma);
				view_box.y = parse_number(p);
				p.parse_all(white_space_or_comma);
				view_box.width = parse_number(p);
				p.parse_all(white_space_or_comma);
				view_box.height = parse_number(p);
				has_view_box = true;
			}
			else if (name == "width") {
				Parser p(value);
				document.width = parse_number(p);
			}
			else if (name == "height") {
				Parser p(value);
				document.height = parse_number(p);
			}
		});
		if (has_view_box) {
			transformation = Transformation::scale(document.width/view_box.width, document.height/view_box.height) * Transformation::translate(-view_box.x, -view_box.y);
		}
		while (!next_is_end_tag()) {
			if (next_is_comment()) parse_comment();
			else if (next_is_start_tag()) parse_tag();
			else parse_char_data();
		}
		parse_end_tag(name);
	}
};

Document parse(const StringView& svg) {
	Document document;
	SVGParser parser(svg, document);
	parser.parse();
	return document;
}
