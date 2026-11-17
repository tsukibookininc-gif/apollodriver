CXX ?= c++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Werror
COMMON = src/protocol.cpp
BOOT_COMMON = src/fx3.cpp src/hash.cpp
HEADERS = $(wildcard include/apollo/*.hpp)
ifeq ($(shell uname -s),Darwin)
BACKEND = src/mac_usb.cpp
BOOT_BACKEND = src/mac_boot.cpp
LDLIBS += -framework IOKit -framework CoreFoundation -framework CoreAudio
BOOT_LIBS = -framework IOKit -framework CoreFoundation
# IOUSBLib is a documented compatibility API; keep all other warnings enabled.
CXXFLAGS += -Wno-deprecated-declarations
else
BACKEND =
BOOT_BACKEND =
BOOT_LIBS =
endif

.PHONY: all test analyze sanitize clean
all: build/apollo-inspect build/apollo-boot

build:
	mkdir -p $@

build/apollo-inspect: src/main.cpp $(COMMON) $(BACKEND) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp $(COMMON) $(BACKEND) $(LDFLAGS) $(LDLIBS) -o $@

build/protocol-test: tests/protocol_test.cpp $(COMMON) $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/protocol_test.cpp $(COMMON) $(LDFLAGS) -o $@

build/apollo-boot: src/boot_main.cpp $(COMMON) $(BOOT_COMMON) $(BOOT_BACKEND) $(HEADERS) third_party/picosha2.h | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/boot_main.cpp $(COMMON) $(BOOT_COMMON) $(BOOT_BACKEND) $(LDFLAGS) $(BOOT_LIBS) -o $@

build/boot-test: tests/boot_test.cpp $(COMMON) $(BOOT_COMMON) $(HEADERS) third_party/picosha2.h | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/boot_test.cpp $(COMMON) $(BOOT_COMMON) $(LDFLAGS) $(BOOT_LIBS) -o $@

test: all build/protocol-test build/boot-test
	./build/protocol-test
	./build/boot-test
	python3 -m unittest discover -s tests -p 'test_*.py' -v

analyze: | build
ifeq ($(shell uname -s),Darwin)
	$(CXX) $(CPPFLAGS) -std=c++17 -Wno-deprecated-declarations --analyze -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror src/protocol.cpp src/main.cpp src/mac_usb.cpp src/fx3.cpp src/hash.cpp src/boot_main.cpp src/mac_boot.cpp
else
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Werror -fanalyzer -c src/protocol.cpp -o build/analyze-protocol.o
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Werror -fanalyzer -c src/main.cpp -o build/analyze-main.o
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Werror -fanalyzer -c src/fx3.cpp -o build/analyze-fx3.o
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Werror -fanalyzer -c src/hash.cpp -o build/analyze-hash.o
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Werror -fanalyzer -c src/boot_main.cpp -o build/analyze-boot-main.o
endif

sanitize: | build
	$(CXX) $(CPPFLAGS) -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(COMMON) tests/protocol_test.cpp -o build/protocol-sanitized
	./build/protocol-sanitized
	$(CXX) $(CPPFLAGS) -std=c++17 -Wno-deprecated-declarations -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(COMMON) $(BOOT_COMMON) tests/boot_test.cpp $(BOOT_LIBS) -o build/boot-sanitized
	./build/boot-sanitized

clean:
	rm -rf build

