/*

Copyright (c) 2017-2018, Elias Aebi
All rights reserved.

*/

#include "parser.hpp"
#include <map>
#include <memory>

class Parser {
	StringView s;
public:
	static constexpr bool numeric(Character c) {
		return c.between('0', '9');
	}
	static constexpr bool white_space(Character c) {
		return c == ' ' || c == '\n' || c == '\r' || c == '\t';
	}
	static constexpr bool white_space_or_comma(Character c) {
		return white_space(c) || c == ',';
	}
	static constexpr bool any_char(Character c) {
		return true;
	}
	static constexpr bool number_start_char(Character c) {
		return numeric(c) || c == '-';
	}
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
	Character next() {
		return s.next();
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
	float parse_number_positive() {
		if (!copy().parse(numeric)) error("expected a number");
		float n = 0.f;
		parse_all([&](Character c) {
			if (c.between('0', '9')) {
				n *= 10.f;
				n += c.get_digit();
				return true;
			}
			else {
				return false;
			}
		});
		if (parse('.')) {
			float factor = .1f;
			parse_all([&](Character c) {
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
	float parse_number() {
		if (parse('-')) {
			return -parse_number_positive();
		}
		else {
			parse('+');
			return parse_number_positive();
		}
	}
};

class XMLNode {
	StringView name;
	std::map<StringView, StringView> attributes;
	std::vector<std::unique_ptr<XMLNode>> children;
public:
	XMLNode(const StringView& name): name(name) {}
	const StringView& get_name() const {
		return name;
	}
	void set_attribute(const StringView& name, const StringView& value) {
		attributes[name] = value;
	}
	StringView get_attribute(const StringView& name) const {
		auto i = attributes.find(name);
		return i != attributes.end() ? i->second : StringView();
	}
	void add_child(std::unique_ptr<XMLNode>&& child) {
		children.push_back(std::move(child));
	}
	std::vector<std::unique_ptr<XMLNode>>& get_children() {
		return children;
	}
};

class XMLParser: public Parser {
	static constexpr bool name_start_char(Character c) {
		return c.between('a', 'z') || c.between('A', 'Z') || c == ':' || c == '_';
	}
	static constexpr bool name_char(Character c) {
		return name_start_char(c) || c == '-' || c == '.' || c.between('0', '9');
	}
	using Parser::parse;
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
	void parse_attributes(std::unique_ptr<XMLNode>& node) {
		while (copy().parse(name_start_char)) {
			StringView name = parse_name();
			parse_all(white_space);
			expect("=");
			parse_all(white_space);
			StringView value = parse_attribute_value();
			parse_all(white_space);
			node->set_attribute(name, value);
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
	std::unique_ptr<XMLNode> parse_node() {
		StringView name = parse_start_tag();
		std::unique_ptr<XMLNode> node(new XMLNode(name));
		parse_attributes(node);
		while (!next_is_end_tag()) {
			if (next_is_comment()) parse_comment();
			else if (next_is_start_tag()) node->add_child(parse_node());
			else parse_char_data();
		}
		parse_end_tag(name);
		return node;
	}
public:
	XMLParser(const StringView& s): Parser(s) {}
	std::unique_ptr<XMLNode> parse() {
		skip_prolog();
		return parse_node();
	}
};

class PathParser: public Parser {
	Path& path;
	Point parse_point() {
		float x = parse_number();
		parse_all(white_space_or_comma);
		float y = parse_number();
		parse_all(white_space_or_comma);
		return Point(x, y);
	}
	using Parser::parse;
public:
	PathParser(const StringView& s, Path& path): Parser(s), path(path) {}
	void parse() {
		Point current_point(0.f, 0.f);
		Point initial_point(0.f, 0.f);
		Point cubic_p2(0.f, 0.f);
		Point quadratic_p1(0.f, 0.f);
		parse_all(white_space);
		while (has_next()) {
			const Character command = next();
			parse_all(white_space);
			if (command == 'M') {
				current_point = parse_point();
				path.move_to(current_point);
				initial_point = current_point;
				while (copy().parse(number_start_char)) {
					current_point = parse_point();
					path.line_to(current_point);
				}
			}
			else if (command == 'm') {
				current_point = current_point + parse_point();
				path.move_to(current_point);
				initial_point = current_point;
				while (copy().parse(number_start_char)) {
					current_point = current_point + parse_point();
					path.line_to(current_point);
				}
			}
			else if (command == 'L') {
				while (copy().parse(number_start_char)) {
					current_point = parse_point();
					path.line_to(current_point);
				}
			}
			else if (command == 'l') {
				while (copy().parse(number_start_char)) {
					current_point = current_point + parse_point();
					path.line_to(current_point);
				}
			}
			else if (command == 'H') {
				while (copy().parse(number_start_char)) {
					current_point.x = parse_number();
					parse_all(white_space_or_comma);
					path.line_to(current_point);
				}
			}
			else if (command == 'h') {
				while (copy().parse(number_start_char)) {
					current_point.x += parse_number();
					parse_all(white_space_or_comma);
					path.line_to(current_point);
				}
			}
			else if (command == 'V') {
				while (copy().parse(number_start_char)) {
					current_point.y = parse_number();
					parse_all(white_space_or_comma);
					path.line_to(current_point);
				}
			}
			else if (command == 'v') {
				while (copy().parse(number_start_char)) {
					current_point.y += parse_number();
					parse_all(white_space_or_comma);
					path.line_to(current_point);
				}
			}
			else if (command == 'C') {
				while (copy().parse(number_start_char)) {
					const Point p1 = parse_point();
					const Point p2 = parse_point();
					current_point = parse_point();
					path.curve_to(p1, p2, current_point);
					cubic_p2 = p2;
				}
			}
			else if (command == 'c') {
				while (copy().parse(number_start_char)) {
					const Point p1 = current_point + parse_point();
					const Point p2 = current_point + parse_point();
					current_point = current_point + parse_point();
					path.curve_to(p1, p2, current_point);
					cubic_p2 = p2;
				}
			}
			else if (command == 'S') {
				while (copy().parse(number_start_char)) {
					const Point p1 = current_point * 2.f - cubic_p2;
					const Point p2 = parse_point();
					current_point = parse_point();
					path.curve_to(p1, p2, current_point);
					cubic_p2 = p2;
				}
			}
			else if (command == 's') {
				while (copy().parse(number_start_char)) {
					const Point p1 = current_point * 2.f - cubic_p2;
					const Point p2 = current_point + parse_point();
					current_point = current_point + parse_point();
					path.curve_to(p1, p2, current_point);
					cubic_p2 = p2;
				}
			}
			else if (command == 'Q') {
				while (copy().parse(number_start_char)) {
					const Point p1 = parse_point();
					current_point = parse_point();
					path.quadratic_curve_to(p1, current_point);
					quadratic_p1 = p1;
				}
			}
			else if (command == 'q') {
				while (copy().parse(number_start_char)) {
					const Point p1 = current_point + parse_point();
					current_point = current_point + parse_point();
					path.quadratic_curve_to(p1, current_point);
					quadratic_p1 = p1;
				}
			}
			else if (command == 'T') {
				while (copy().parse(number_start_char)) {
					const Point p1 = current_point * 2.f - quadratic_p1;
					current_point = parse_point();
					path.quadratic_curve_to(p1, current_point);
					quadratic_p1 = p1;
				}
			}
			else if (command == 't') {
				while (copy().parse(number_start_char)) {
					const Point p1 = current_point * 2.f - quadratic_p1;
					current_point = current_point + parse_point();
					path.quadratic_curve_to(p1, current_point);
					quadratic_p1 = p1;
				}
			}
			else if (command == 'Z' || command == 'z') {
				path.close();
				current_point = initial_point;
			}
			else {
				error("unexpected command");
			}
			if (command != 'C' && command != 'c' && command != 'S' && command != 's') {
				cubic_p2 = current_point;
			}
			if (command != 'Q' && command != 'q' && command != 'T' && command != 't') {
				quadratic_p1 = current_point;
			}
		}
	}
};

struct NamedColor {
	StringView name;
	Color color;
	constexpr NamedColor(const StringView& name, const Color& color): name(name), color(color) {}
	constexpr bool operator <(const StringView& rhs) const {
		return name < rhs;
	}
};
constexpr NamedColor color_names[] = {
	NamedColor("red",                  Color::rgb(255,   0,   0)),
	NamedColor("tan",                  Color::rgb(210, 180, 140)),
	NamedColor("aqua",                 Color::rgb(  0, 255, 255)),
	NamedColor("blue",                 Color::rgb(  0,   0, 255)),
	NamedColor("cyan",                 Color::rgb(  0, 255, 255)),
	NamedColor("gold",                 Color::rgb(255, 215,   0)),
	NamedColor("gray",                 Color::rgb(128, 128, 128)),
	NamedColor("grey",                 Color::rgb(128, 128, 128)),
	NamedColor("lime",                 Color::rgb(  0, 255,   0)),
	NamedColor("navy",                 Color::rgb(  0,   0, 128)),
	NamedColor("peru",                 Color::rgb(205, 133,  63)),
	NamedColor("pink",                 Color::rgb(255, 192, 203)),
	NamedColor("plum",                 Color::rgb(221, 160, 221)),
	NamedColor("snow",                 Color::rgb(255, 250, 250)),
	NamedColor("teal",                 Color::rgb(  0, 128, 128)),
	NamedColor("azure",                Color::rgb(240, 255, 255)),
	NamedColor("beige",                Color::rgb(245, 245, 220)),
	NamedColor("black",                Color::rgb(  0,   0,   0)),
	NamedColor("brown",                Color::rgb(165,  42,  42)),
	NamedColor("coral",                Color::rgb(255, 127,  80)),
	NamedColor("green",                Color::rgb(  0, 128,   0)),
	NamedColor("ivory",                Color::rgb(255, 255, 240)),
	NamedColor("khaki",                Color::rgb(240, 230, 140)),
	NamedColor("linen",                Color::rgb(250, 240, 230)),
	NamedColor("olive",                Color::rgb(128, 128,   0)),
	NamedColor("wheat",                Color::rgb(245, 222, 179)),
	NamedColor("white",                Color::rgb(255, 255, 255)),
	NamedColor("bisque",               Color::rgb(255, 228, 196)),
	NamedColor("indigo",               Color::rgb( 75,   0, 130)),
	NamedColor("maroon",               Color::rgb(128,   0,   0)),
	NamedColor("orange",               Color::rgb(255, 165,   0)),
	NamedColor("orchid",               Color::rgb(218, 112, 214)),
	NamedColor("purple",               Color::rgb(128,   0, 128)),
	NamedColor("salmon",               Color::rgb(250, 128, 114)),
	NamedColor("sienna",               Color::rgb(160,  82,  45)),
	NamedColor("silver",               Color::rgb(192, 192, 192)),
	NamedColor("tomato",               Color::rgb(255,  99,  71)),
	NamedColor("violet",               Color::rgb(238, 130, 238)),
	NamedColor("yellow",               Color::rgb(255, 255,   0)),
	NamedColor("crimson",              Color::rgb(220,  20,  60)),
	NamedColor("darkred",              Color::rgb(139,   0,   0)),
	NamedColor("dimgray",              Color::rgb(105, 105, 105)),
	NamedColor("dimgrey",              Color::rgb(105, 105, 105)),
	NamedColor("fuchsia",              Color::rgb(255,   0, 255)),
	NamedColor("hotpink",              Color::rgb(255, 105, 180)),
	NamedColor("magenta",              Color::rgb(255,   0, 255)),
	NamedColor("oldlace",              Color::rgb(253, 245, 230)),
	NamedColor("skyblue",              Color::rgb(135, 206, 235)),
	NamedColor("thistle",              Color::rgb(216, 191, 216)),
	NamedColor("cornsilk",             Color::rgb(255, 248, 220)),
	NamedColor("darkblue",             Color::rgb(  0,   0, 139)),
	NamedColor("darkcyan",             Color::rgb(  0, 139, 139)),
	NamedColor("darkgray",             Color::rgb(169, 169, 169)),
	NamedColor("darkgrey",             Color::rgb(169, 169, 169)),
	NamedColor("deeppink",             Color::rgb(255,  20, 147)),
	NamedColor("honeydew",             Color::rgb(240, 255, 240)),
	NamedColor("lavender",             Color::rgb(230, 230, 250)),
	NamedColor("moccasin",             Color::rgb(255, 228, 181)),
	NamedColor("seagreen",             Color::rgb( 46, 139,  87)),
	NamedColor("seashell",             Color::rgb(255, 245, 238)),
	NamedColor("aliceblue",            Color::rgb(240, 248, 255)),
	NamedColor("burlywood",            Color::rgb(222, 184, 135)),
	NamedColor("cadetblue",            Color::rgb( 95, 158, 160)),
	NamedColor("chocolate",            Color::rgb(210, 105,  30)),
	NamedColor("darkgreen",            Color::rgb(  0, 100,   0)),
	NamedColor("darkkhaki",            Color::rgb(189, 183, 107)),
	NamedColor("firebrick",            Color::rgb(178,  34,  34)),
	NamedColor("gainsboro",            Color::rgb(220, 220, 220)),
	NamedColor("goldenrod",            Color::rgb(218, 165,  32)),
	NamedColor("indianred",            Color::rgb(205,  92,  92)),
	NamedColor("lawngreen",            Color::rgb(124, 252,   0)),
	NamedColor("lightblue",            Color::rgb(173, 216, 230)),
	NamedColor("lightcyan",            Color::rgb(224, 255, 255)),
	NamedColor("lightgray",            Color::rgb(211, 211, 211)),
	NamedColor("lightgrey",            Color::rgb(211, 211, 211)),
	NamedColor("lightpink",            Color::rgb(255, 182, 193)),
	NamedColor("limegreen",            Color::rgb( 50, 205,  50)),
	NamedColor("mintcream",            Color::rgb(245, 255, 250)),
	NamedColor("mistyrose",            Color::rgb(255, 228, 225)),
	NamedColor("olivedrab",            Color::rgb(107, 142,  35)),
	NamedColor("orangered",            Color::rgb(255,  69,   0)),
	NamedColor("palegreen",            Color::rgb(152, 251, 152)),
	NamedColor("peachpuff",            Color::rgb(255, 218, 185)),
	NamedColor("rosybrown",            Color::rgb(188, 143, 143)),
	NamedColor("royalblue",            Color::rgb( 65, 105, 225)),
	NamedColor("slateblue",            Color::rgb(106,  90, 205)),
	NamedColor("slategray",            Color::rgb(112, 128, 144)),
	NamedColor("slategrey",            Color::rgb(112, 128, 144)),
	NamedColor("steelblue",            Color::rgb( 70, 130, 180)),
	NamedColor("turquoise",            Color::rgb( 64, 224, 208)),
	NamedColor("aquamarine",           Color::rgb(127, 255, 212)),
	NamedColor("blueviolet",           Color::rgb(138,  43, 226)),
	NamedColor("chartreuse",           Color::rgb(127, 255,   0)),
	NamedColor("darkorange",           Color::rgb(255, 140,   0)),
	NamedColor("darkorchid",           Color::rgb(153,  50, 204)),
	NamedColor("darksalmon",           Color::rgb(233, 150, 122)),
	NamedColor("darkviolet",           Color::rgb(148,   0, 211)),
	NamedColor("dodgerblue",           Color::rgb( 30, 144, 255)),
	NamedColor("ghostwhite",           Color::rgb(248, 248, 255)),
	NamedColor("lightcoral",           Color::rgb(240, 128, 128)),
	NamedColor("lightgreen",           Color::rgb(144, 238, 144)),
	NamedColor("mediumblue",           Color::rgb(  0,   0, 205)),
	NamedColor("papayawhip",           Color::rgb(255, 239, 213)),
	NamedColor("powderblue",           Color::rgb(176, 224, 230)),
	NamedColor("sandybrown",           Color::rgb(244, 164,  96)),
	NamedColor("whitesmoke",           Color::rgb(245, 245, 245)),
	NamedColor("darkmagenta",          Color::rgb(139,   0, 139)),
	NamedColor("deepskyblue",          Color::rgb(  0, 191, 255)),
	NamedColor("floralwhite",          Color::rgb(255, 250, 240)),
	NamedColor("forestgreen",          Color::rgb( 34, 139,  34)),
	NamedColor("greenyellow",          Color::rgb(173, 255,  47)),
	NamedColor("lightsalmon",          Color::rgb(255, 160, 122)),
	NamedColor("lightyellow",          Color::rgb(255, 255, 224)),
	NamedColor("navajowhite",          Color::rgb(255, 222, 173)),
	NamedColor("saddlebrown",          Color::rgb(139,  69,  19)),
	NamedColor("springgreen",          Color::rgb(  0, 255, 127)),
	NamedColor("yellowgreen",          Color::rgb(154, 205,  50)),
	NamedColor("antiquewhite",         Color::rgb(250, 235, 215)),
	NamedColor("darkseagreen",         Color::rgb(143, 188, 143)),
	NamedColor("lemonchiffon",         Color::rgb(255, 250, 205)),
	NamedColor("lightskyblue",         Color::rgb(135, 206, 250)),
	NamedColor("mediumorchid",         Color::rgb(186,  85, 211)),
	NamedColor("mediumpurple",         Color::rgb(147, 112, 219)),
	NamedColor("midnightblue",         Color::rgb( 25,  25, 112)),
	NamedColor("darkgoldenrod",        Color::rgb(184, 134,  11)),
	NamedColor("darkslateblue",        Color::rgb( 72,  61, 139)),
	NamedColor("darkslategray",        Color::rgb( 47,  79,  79)),
	NamedColor("darkslategrey",        Color::rgb( 47,  79,  79)),
	NamedColor("darkturquoise",        Color::rgb(  0, 206, 209)),
	NamedColor("lavenderblush",        Color::rgb(255, 240, 245)),
	NamedColor("lightseagreen",        Color::rgb( 32, 178, 170)),
	NamedColor("palegoldenrod",        Color::rgb(238, 232, 170)),
	NamedColor("paleturquoise",        Color::rgb(175, 238, 238)),
	NamedColor("palevioletred",        Color::rgb(219, 112, 147)),
	NamedColor("blanchedalmond",       Color::rgb(255, 235, 205)),
	NamedColor("cornflowerblue",       Color::rgb(100, 149, 237)),
	NamedColor("darkolivegreen",       Color::rgb( 85, 107,  47)),
	NamedColor("lightslategray",       Color::rgb(119, 136, 153)),
	NamedColor("lightslategrey",       Color::rgb(119, 136, 153)),
	NamedColor("lightsteelblue",       Color::rgb(176, 196, 222)),
	NamedColor("mediumseagreen",       Color::rgb( 60, 179, 113)),
	NamedColor("mediumslateblue",      Color::rgb(123, 104, 238)),
	NamedColor("mediumturquoise",      Color::rgb( 72, 209, 204)),
	NamedColor("mediumvioletred",      Color::rgb(199,  21, 133)),
	NamedColor("mediumaquamarine",     Color::rgb(102, 205, 170)),
	NamedColor("mediumspringgreen",    Color::rgb(  0, 250, 154)),
	NamedColor("lightgoldenrodyellow", Color::rgb(250, 250, 210)),
};

using PaintServerMap = std::map<StringView, std::shared_ptr<PaintServer>>;

class StyleParser: public Parser {
public:
	StyleParser(const StringView& s): Parser(s) {}
	Color parse_color() {
		if (parse('#')) {
			int d[6];
			int i = 0;
			parse_all([&](Character c) {
				if (c.between('0', '9') || c.between('a', 'f') || c.between('A', 'F')) {
					if (i < 6) {
						d[i] = c.get_digit();
					}
					++i;
					return true;
				}
				return false;
			});
			if (i == 6) {
				const float red = (d[0] << 4 | d[1]) / 255.f;
				const float green = (d[2] << 4 | d[3]) / 255.f;
				const float blue = (d[4] << 4 | d[5]) / 255.f;
				return Color(red, green, blue);
			}
			else if (i == 3) {
				const float red = d[0] / 15.f;
				const float green = d[1] / 15.f;
				const float blue = d[2] / 15.f;
				return Color(red, green, blue);
			}
			else {
				error("expected 3 or 6 digits");
			}
		}
		else if (parse("rgb")) {
			expect("(");
			parse_all(white_space);
			float red = parse_number();
			if (parse('%')) {
				red /= 100.f;
			}
			else {
				red /= 255.f;
			}
			parse_all(white_space);
			expect(",");
			parse_all(white_space);
			float green = parse_number();
			if (parse('%')) {
				green /= 100.f;
			}
			else {
				green /= 255.f;
			}
			parse_all(white_space);
			expect(",");
			parse_all(white_space);
			float blue = parse_number();
			if (parse('%')) {
				blue /= 100.f;
			}
			else {
				blue /= 255.f;
			}
			parse_all(white_space);
			expect(")");
			return Color(red, green, blue);
		}
		else {
			StringView start = get();
			parse_all([](Character c) {
				return c.between('a', 'z');
			});
			StringView name = get() - start;
			auto i = std::lower_bound(std::begin(color_names), std::end(color_names), name);
			if (i == std::end(color_names) || i->name != name) {
				error("invalid color");
			}
			return i->color;
		}
	}
	void parse_paint(std::shared_ptr<PaintServer>& paint, const PaintServerMap& paint_servers) {
		if (parse("none")) {
			paint = nullptr;
		}
		else if (parse("inherit")) {

		}
		else if (parse("url")) {
			expect("(");
			expect("#");
			StringView start = get();
			parse_all([](Character c) {
				return c != ')';
			});
			StringView id = get() - start;
			expect(")");
			auto i = paint_servers.find(id);
			if (i == paint_servers.end()) {
				//error("invalid IRI: " + id.to_string());
				printf("url not found: %s\n", id.to_string().c_str());
			}
			else {
				paint = i->second;
			}
		}
		else {
			paint = std::make_shared<ColorPaintServer>(parse_color());
		}
	}
	void parse_style(std::unique_ptr<XMLNode>& node) {
		parse_all(white_space);
		while (has_next()) {
			StringView start = get();
			parse_all([](Character c) {
				return c != ':' && !white_space(c);
			});
			StringView key = get() - start;
			parse_all(white_space);
			parse(':');
			parse_all(white_space);
			start = get();
			parse_all([](Character c) {
				return c != ';';
			});
			StringView value = get() - start;
			node->set_attribute(key, value);
			parse(';');
			parse_all(white_space);
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
				const float a = parse_number();
				parse_all(white_space_or_comma);
				const float b = parse_number();
				parse_all(white_space_or_comma);
				const float c = parse_number();
				parse_all(white_space_or_comma);
				const float d = parse_number();
				parse_all(white_space_or_comma);
				const float e = parse_number();
				parse_all(white_space_or_comma);
				const float f = parse_number();
				parse_all(white_space);
				expect(")");
				t = t * Transformation(a, b, c, d, e, f);
			}
			else if (parse("translate")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float x = parse_number();
				parse_all(white_space_or_comma);
				float y = 0.f;
				if (copy().parse(number_start_char)) {
					y = parse_number();
					parse_all(white_space);
				}
				expect(")");
				t = t * Transformation::translate(x, y);
			}
			else if (parse("scale")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float x = parse_number();
				parse_all(white_space_or_comma);
				float y = x;
				if (copy().parse(number_start_char)) {
					y = parse_number();
					parse_all(white_space);
				}
				expect(")");
				t = t * Transformation::scale(x, y);
			}
			else if (parse("rotate")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float a = parse_number() * (M_PI / 180.f);
				parse_all(white_space);
				if (copy().parse(number_start_char)) {
					const float x = parse_number();
					parse_all(white_space_or_comma);
					const float y = parse_number();
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
				const float a = parse_number() * (M_PI / 180.f);
				parse_all(white_space);
				expect(")");
				t = t * Transformation(1.f, 0.f, std::tan(a), 1.f, 0.f, 0.f);
			}
			else if (parse("skewY")) {
				parse_all(white_space);
				expect("(");
				parse_all(white_space);
				const float a = parse_number() * (M_PI / 180.f);
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

class SVGParser: public XMLParser {
	Document& document;
	Transformation transformation;
	Style style;
	PaintServerMap paint_servers;
	float get_number(const std::unique_ptr<XMLNode>& node, const StringView& attribute, float default_value) {
		if (StringView value = node->get_attribute(attribute)) {
			Parser parser(value);
			return parser.parse_number();
		}
		else {
			return default_value;
		}
	}
	void parse_gradient(std::unique_ptr<XMLNode>& node, Gradient& gradient) {
		if (StringView value = node->get_attribute("style")) {
			StyleParser p(value);
			p.parse_style(node);
		}
		const StringView& name = node->get_name();
		if (name == "stop") {
			Gradient::Stop stop;
			if (StringView value = node->get_attribute("offset")) {
				Parser p(value);
				stop.pos = p.parse_number();
				if (p.parse('%')) {
					stop.pos /= 100.f;
				}
			}
			if (StringView value = node->get_attribute("stop-color")) {
				StyleParser p(value);
				stop.color = p.parse_color();
			}
			const float opacity = get_number(node, "stop-opacity", 1.f);
			stop.color = stop.color * opacity;
			gradient.stops.push_back(stop);
		}
	}
	void parse_def(std::unique_ptr<XMLNode>& node) {
		const StringView& name = node->get_name();
		if (name == "linearGradient") {
			LinearGradient gradient;
			StringView id = node->get_attribute("id");
			gradient.start.x = get_number(node, "x1", 0.f);
			gradient.start.y = get_number(node, "y1", 0.f);
			gradient.end.x = get_number(node, "x2", 1.f);
			gradient.end.y = get_number(node, "y2", 0.f);
			if (StringView value = node->get_attribute("gradientUnits")) {
				// TODO: implement
			}
			if (StringView value = node->get_attribute("gradientTransform")) {
				TransformParser p(value);
				const Transformation transformation = p.parse();
				gradient.start = transformation * gradient.start;
				gradient.end = transformation * gradient.end;
			}
			for (auto& child: node->get_children()) {
				parse_gradient(child, gradient);
			}
			paint_servers[id] = std::make_shared<LinearGradientPaintServer>(gradient);
		}
		else if (name == "radialGradient") {
			RadialGradient gradient;
			StringView id = node->get_attribute("id");
			gradient.c.x = get_number(node, "cx", .5f);
			gradient.c.y = get_number(node, "cy", .5f);
			gradient.r = get_number(node, "r", .5f);
			gradient.f.x = get_number(node, "fx", gradient.c.x);
			gradient.f.y = get_number(node, "fy", gradient.c.y);
			if (StringView value = node->get_attribute("gradientUnits")) {
				// TODO: implement
			}
			if (StringView value = node->get_attribute("gradientTransform")) {
				// TODO: implement
			}
			for (auto& child: node->get_children()) {
				parse_gradient(child, gradient);
			}
			paint_servers[id] = std::make_shared<RadialGradientPaintServer>(gradient);
		}
	}
	void parse_style(std::unique_ptr<XMLNode>& node) {
		if (StringView value = node->get_attribute("style")) {
			StyleParser p(value);
			p.parse_style(node);
		}
		if (StringView value = node->get_attribute("fill")) {
			StyleParser p(value);
			p.parse_paint(style.fill, paint_servers);
		}
		if (StringView value = node->get_attribute("fill-opacity")) {
			Parser p(value);
			style.fill_opacity = p.parse_number();
		}
		if (StringView value = node->get_attribute("stroke")) {
			StyleParser p(value);
			p.parse_paint(style.stroke, paint_servers);
		}
		if (StringView value = node->get_attribute("stroke-width")) {
			Parser p(value);
			style.stroke_width = p.parse_number();
		}
		if (StringView value = node->get_attribute("stroke-opacity")) {
			Parser p(value);
			style.stroke_opacity = p.parse_number();
		}
	}
	void parse_node(std::unique_ptr<XMLNode>& node) {
		const StringView& name = node->get_name();
		if (name == "path") {
			Path path;
			Style previous_style = style;
			parse_style(node);
			if (StringView value = node->get_attribute("d")) {
				PathParser p(value, path);
				p.parse();
			}
			document.draw(path, style, transformation);
			style = previous_style;
		}
		else if (name == "g") {
			Transformation previous_transformation = transformation;
			Style previous_style = style;
			parse_style(node);
			if (StringView value = node->get_attribute("transform")) {
				TransformParser p(value);
				transformation = transformation * p.parse();
			}
			for (auto& child: node->get_children()) {
				parse_node(child);
			}
			transformation = previous_transformation;
			style = previous_style;
		}
		else if (name == "defs") {
			for (auto& child: node->get_children()) {
				parse_def(child);
			}
		}
	}
public:
	SVGParser(const StringView& s, Document& document): XMLParser(s), document(document) {}
	void parse() {
		std::unique_ptr<XMLNode> root = XMLParser::parse();
		if (root->get_name() != "svg") error("expected svg tag");
		struct {
			float x = 0.f;
			float y = 0.f;
			float width = 0.f;
			float height = 0.f;
		} view_box;
		if (StringView value = root->get_attribute("viewBox")) {
			Parser p(value);
			p.parse_all(white_space);
			view_box.x = p.parse_number();
			p.parse_all(white_space_or_comma);
			view_box.y = p.parse_number();
			p.parse_all(white_space_or_comma);
			view_box.width = p.parse_number();
			p.parse_all(white_space_or_comma);
			view_box.height = p.parse_number();
		}
		document.width = get_number(root, "width", 0.f);
		document.height = get_number(root, "height", 0.f);
		if (document.width == 0.f) document.width = view_box.width;
		if (document.height == 0.f) document.height = view_box.height;
		if (view_box.width > 0.f && view_box.height > 0.f) {
			transformation = Transformation::scale(document.width/view_box.width, document.height/view_box.height) * Transformation::translate(-view_box.x, -view_box.y);
		}
		for (auto& child: root->get_children()) {
			parse_node(child);
		}
	}
};

Document parse(const StringView& svg) {
	Document document;
	SVGParser parser(svg, document);
	parser.parse();
	return document;
}
