CXXFLAGS = -std=c++11 -Wall -O2
LDLIBS = -lpng

raster: main.cpp rasterizer.cpp
	$(CXX) -o $@ $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS)

clean:
	rm -f raster

.PHONY: clean
