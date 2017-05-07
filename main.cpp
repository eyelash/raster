/*

Copyright (c) 2017, Elias Aebi
All rights reserved.

*/

#include "document.hpp"

int main() {
	Document document;
	SolidFill blue(Color(0, 0, 1) * .85f);
	SolidFill yellow(Color(1, 1, 0) * .75f);

	{
		Path path;
		path.move_to( 50, 250);
		path.line_to(100,  50);
		path.line_to(150, 150);
		path.line_to(200, 100);
		path.line_to(250, 200);
		path.close();
		document.fill(path, &blue);
	}
	{
		Path path;
		path.move_to(100, 200);
		path.line_to(100,  50);
		path.line_to( 50, 150);
		path.close();
		document.fill(path, &yellow);
	}

	rasterize(document.shapes, "result.png", 300, 300);
}
