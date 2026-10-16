# Metronomicon for Disting NT
#
#   make NT_API=/path/to/distingNT_API      -> plugins/metronomicon.o  (copy to SD card /programs/plug-ins/)
#   make test                               -> builds and runs the host test suite
#
# Requires the Arm GNU toolchain (arm-none-eabi-g++) for the plugin and any
# host C++ compiler for the tests.

NT_API   ?= ../distingNT_API
CXX_ARM  ?= arm-none-eabi-c++
CXX_HOST ?= c++

ARM_FLAGS := -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb \
             -fno-rtti -fno-exceptions -Os -fPIC -Wall -I$(NT_API)/include -Isrc

all: plugins/metronomicon.o

plugins/metronomicon.o: src/metronome.cpp src/metronome_engine.h src/patterns.h
	@mkdir -p plugins
	$(CXX_ARM) $(ARM_FLAGS) -c -o $@ src/metronome.cpp
	@echo "Built $@"

test: build/test_engine
	./build/test_engine

build/test_engine: tests/test_engine.cpp src/metronome_engine.h src/patterns.h
	@mkdir -p build
	$(CXX_HOST) -std=c++11 -O2 -Wall -DMETRO_ENGINE_TEST -Isrc -o $@ tests/test_engine.cpp

clean:
	rm -rf plugins build

.PHONY: all test clean
