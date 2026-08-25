# 32-bit Windows VST2 host, cross-compiled from Linux, run under Wine.

MINGW_PREFIX ?= $(HOME)/.local/opt/mingw-i686/usr/bin
CXX          := $(firstword $(wildcard $(MINGW_PREFIX)/i686-w64-mingw32-g++-win32 \
                                       $(MINGW_PREFIX)/i686-w64-mingw32-g++) \
                            i686-w64-mingw32-g++)

CXXFLAGS := -O2 -Wall -Wextra -std=gnu++11 -m32
LDFLAGS  := -static -static-libgcc -static-libstdc++ -lwinmm -lgdi32 -luser32 -lcomdlg32

TARGET := vsthost32.exe

all: $(TARGET)

$(TARGET): host.cpp vst2.h
	$(CXX) $(CXXFLAGS) host.cpp -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
