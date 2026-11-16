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

.PHONY: all test analyze clean
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

analyze: | build
ifeq ($(shell uname -s),Darwin)
	$(CXX) $(CPPFLAGS) -std=c++17 -Wno-deprecated-declarations --analyze -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror src/protocol.cpp src/main.cpp src/mac_usb.cpp
else
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Werror -fanalyzer -c src/protocol.cpp -o build/analyze-protocol.o
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Werror -fanalyzer -c src/main.cpp -o build/analyze-main.o
endif

clean:
	rm -rf build

