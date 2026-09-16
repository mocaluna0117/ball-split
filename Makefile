CXX      := clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -MMD -MP $(shell sdl2-config --cflags)
LDFLAGS  := $(shell sdl2-config --libs)

SRC    := $(wildcard src/*.cpp)
OBJ    := $(SRC:.cpp=.o)
DEP    := $(OBJ:.o=.d)
TARGET := ballsim

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(DEP) $(TARGET)

-include $(DEP)

.PHONY: clean
