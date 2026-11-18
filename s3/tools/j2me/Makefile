# Host build of EspOS's J2ME VM (src/j2me) and its runner. From s3/:
#   make -C tools/j2me            -> tools/j2me/build/j2me-run (and inflate-test, the deflate decoder's check)
#   make -C tools/j2me asan       -> tools/j2me/build-asan/j2me-run (AddressSanitizer + UBSan)
# docs/j2me.md. The VM sources are the firmware's own (no copies).
ROOT := ../..
BUILD ?= build
CXX ?= c++
OPT ?= -O2
CXXFLAGS := -std=c++17 $(OPT) -g -MMD -MP -Wall -Wextra -Wno-unused-parameter -fno-exceptions -fno-rtti \
  -I$(ROOT)/src/j2me $(EXTRA)
VM_SRC := $(ROOT)/src/j2me/jvm.cpp $(wildcard $(ROOT)/src/j2me/vm_*.cpp)
OBJ := $(patsubst $(ROOT)/src/j2me/%.cpp,$(BUILD)/vm/%.o,$(VM_SRC)) $(BUILD)/main.o

all: $(BUILD)/j2me-run $(BUILD)/inflate-test

$(BUILD)/j2me-run: $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@

# the deflate decoder against zlib's vectors (j2me/test/run.sh makes them)
$(BUILD)/inflate-test: $(filter-out $(BUILD)/main.o,$(OBJ)) $(BUILD)/inflate_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/inflate_test.o: inflate_test.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/vm/%.o: $(ROOT)/src/j2me/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/main.o: main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

asan:
	$(MAKE) BUILD=build-asan OPT=-O1 EXTRA="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"

clean:
	rm -rf build build-asan

.PHONY: all asan clean
-include $(OBJ:.o=.d) $(BUILD)/inflate_test.d
