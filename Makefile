CXX = g++
CXXFLAGS = -std=c++20 -Werror -Wall -Wextra -Wconversion -Wpedantic
CXXLINKS = -ldpp -loggz -lmpg123 -lpthread -lboost_system

default:
	$(CXX) $(CXXFLAGS) -g -o mdmb src/main.cpp $(CXXLINKS)

mp3_example:
	$(CXX) $(CXXFLAGS) -g -o mdmb src/mp3_example.cpp $(CXXLINKS)
