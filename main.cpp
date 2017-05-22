/*

Copyright (c) 2017, Elias Aebi
All rights reserved.

*/

#include "parser.hpp"
#include <string>
#include <fstream>
#include <iostream>

std::string read_file(const char* file_name) {
	std::ifstream file(file_name);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
	if (argc <= 2) {
		std::cout << "usage: raster <input> <output>" << std::endl;
		return 0;
	}
	std::string svg = read_file(argv[1]);
	try {
		Document document = parse(svg);
		rasterize(document.shapes, argv[2], document.width, document.height);
	} catch (const std::string& error) {
		std::cerr << "error: " << error << std::endl;
	}
}
