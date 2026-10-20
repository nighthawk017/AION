CXX      = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-deprecated-declarations -I. -Istubs

.PHONY: test clean

test: test_aion
	./test_aion

test_aion: test_aion.cpp mocks.h $(wildcard stubs/*.h) ../aion.ino
	$(CXX) $(CXXFLAGS) -x c++ -o $@ test_aion.cpp

clean:
	rm -f test_aion
