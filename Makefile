CXX ?= c++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Werror
COMMON = src/protocol.cpp
HEADERS = $(wildcard include/apollo/*.hpp)
ifeq ($(shell uname -s),Darwin)
BACKEND = src/mac_usb.cpp
LDLIBS += -framework IOKit -framework CoreFoundation -framework CoreAudio
# IOUSBLib is a documented compatibility API; keep all other warnings enabled.
CXXFLAGS += -Wno-deprecated-declarations
else
BACKEND =
endif

.PHONY: all test clean
all: build/apollo-inspect

build:
	mkdir -p $@

build/apollo-inspect: src/main.cpp $(COMMON) $(BACKEND) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp $(COMMON) $(BACKEND) $(LDFLAGS) $(LDLIBS) -o $@

build/protocol-test: tests/protocol_test.cpp $(COMMON) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/protocol_test.cpp $(COMMON) $(LDFLAGS) -o $@

test: all build/protocol-test
	./build/protocol-test
	python3 -m unittest discover -s tests -p 'test_*.py' -v

clean:
	rm -rf build

