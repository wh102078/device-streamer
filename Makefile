CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g -Isrc
FF_CFLAGS := $(shell pkg-config --cflags libavcodec libavformat libavutil libswscale)
FF_LIBS   := $(shell pkg-config --libs libavcodec libavformat libavutil libswscale)

TARGET := device_streamer
SRC    := $(wildcard src/*.cpp)

# 内存/未定义行为检查
ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
# 数据竞争检查
TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(FF_CFLAGS) -o $@ $(SRC) $(FF_LIBS)

device_streamer_asan: $(SRC)
	$(CXX) $(CXXFLAGS) $(ASAN_FLAGS) $(FF_CFLAGS) -o $@ $(SRC) $(FF_LIBS)

device_streamer_tsan: $(SRC)
	$(CXX) $(CXXFLAGS) $(TSAN_FLAGS) $(FF_CFLAGS) -o $@ $(SRC) $(FF_LIBS)

asan: device_streamer_asan
tsan: device_streamer_tsan

clean:
	rm -f $(TARGET) device_streamer_asan device_streamer_tsan out/*

.PHONY: asan tsan clean
