CXX = g++
# CXXFLAGS = -Werror -Wall -Wextra -Wconversion -Wpedantic
CXXFLAGS = -Wall -Wextra -Wconversion -Wpedantic
CXXLINKS = -ldpp -lmpg123

default:
	$(CXX) $(CXXFLAGS) -o mdmb src/main.cpp $(CXXLINKS)
