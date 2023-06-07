CXX = g++
CXXFLAGS = -Werror -Wall -Wextra -Wconversion -Wpedantic
CXXLINKS = -ldpp

default:
	$(CXX) $(CXXFLAGS) -o mdmb src/main.cpp $(CXXLINKS)
