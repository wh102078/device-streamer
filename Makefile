CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g -Isrc
FF_CFLAGS := $(shell pkg-config --cflags libavcodec libavutil libswscale)
FF_LIBS   := $(shell pkg-config --libs libavcodec libavutil libswscale)

TARGET := device_streamer
SRC    := $(wildcard src/*.cpp)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(FF_CFLAGS) -o $@ $(SRC) $(FF_LIBS)

clean:
	rm -f $(TARGET) out/*

.PHONY: clean
