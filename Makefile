APP_NAME = GoH-Tasks
SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=build/%.o)
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -I/boot/system/develop/headers -Isrc
LDFLAGS = -lbe -lnetwork

all: $(APP_NAME)

$(APP_NAME): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build $(APP_NAME)

.PHONY: all clean
