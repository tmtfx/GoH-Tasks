APP_NAME = GoH-Tasks
SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=build/%.o)
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -I/boot/system/develop/headers -Isrc -Iresources
LDFLAGS = -lbe -lnetwork -lcurl

RDEF = resources/GTasks-icon.rdef
RSRC = build/GTasks-icon.rsrc

all: $(APP_NAME)

$(APP_NAME): $(OBJS) $(RSRC)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)
	xres -o $@ $(RSRC)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(RSRC): $(RDEF) | build
	rc -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build $(APP_NAME)

.PHONY: all clean
