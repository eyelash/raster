CXXFLAGS = -std=c++11 -Wall -O2
LDLIBS = -lpng

raster: raster.cpp
	$(CXX) -o $@ $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS)

clean:
	rm -f raster

.PHONY: clean
