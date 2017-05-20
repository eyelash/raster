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
	void error(std::string message) {
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
