CXXFLAGS=--std=c++17 -g -Wall -Werror -O3

ALL_H=$(wildcard *.h)
ALL_CPP=$(wildcard *.cpp)
ALL=$(ALL_CPP) $(ALL_H)

integrity_checker: $(ALL)
	$(CXX) -o $@ $(ALL_CPP) $(LDXXFLAGS)

