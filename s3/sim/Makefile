# EspOS simulator (host build of the UI). Run from s3/: make -C sim -j8
ROOT := ..
LV := $(ROOT)/.pio/libdeps/s3desk/lvgl
AJ := $(ROOT)/.pio/libdeps/s3desk/ArduinoJson/src
BUILD ?= build
CFLAGS := -O1 -g -MMD -MP $(EXTRA) -DSIM -DLV_CONF_INCLUDE_SIMPLE -Ishim -I$(ROOT)/include -I$(LV) -I$(ROOT)/src -w
CXXFLAGS := $(CFLAGS) -std=gnu++17 -I$(AJ) -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 \
  -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 \
  -DESPOS_API='"https://api.espos.dev"' -DESPOS_WEB='"https://espos.dev"'

LV_SRC := $(shell find $(LV)/src -name '*.c')
LV_OBJ := $(patsubst $(LV)/src/%.c,$(BUILD)/lvgl/%.o,$(LV_SRC))
HW := desk net power sound photo ota touch lcd trace devtools main remote mic
APP_CPP := $(filter-out $(addprefix $(ROOT)/src/,$(addsuffix .cpp,$(HW))),$(wildcard $(ROOT)/src/*.cpp))
APP_C := $(wildcard $(ROOT)/src/*.c) $(wildcard $(ROOT)/src/fonts/*.c)
# PawBoop (src/pet) and its Opus decoder (lib/opus, the same fixed-point build as the board).
APP_CPP += $(wildcard $(ROOT)/src/pet/*.cpp)
APP_C += $(wildcard $(ROOT)/src/pet/*.c)
CXXFLAGS += -I$(ROOT)/lib/opus/include
OPUS_SRC := $(wildcard $(ROOT)/lib/opus/celt/*.c $(ROOT)/lib/opus/silk/*.c $(ROOT)/lib/opus/src/*.c)
OPUS_OBJ := $(patsubst $(ROOT)/lib/opus/%.c,$(BUILD)/opus/%.o,$(OPUS_SRC))
OPUS_FLAGS := -O2 -w -DFIXED_POINT=1 -DOPUS_BUILD -DNONTHREADSAFE_PSEUDOSTACK -DGLOBAL_STACK_SIZE=120000 \
  -DDISABLE_FLOAT_API -DHAVE_LRINT=1 -DHAVE_LRINTF=1 -I$(ROOT)/lib/opus/include -I$(ROOT)/lib/opus/celt \
  -I$(ROOT)/lib/opus/silk -I$(ROOT)/lib/opus/silk/fixed
$(BUILD)/opus/%.o: $(ROOT)/lib/opus/%.c
	@mkdir -p $(dir $@) && $(CC) $(OPUS_FLAGS) -c $< -o $@
# JavaScript apps (src/js) and QuickJS-ng (lib/quickjs, unchanged sources).
APP_CPP += $(wildcard $(ROOT)/src/js/*.cpp)
CXXFLAGS += -I$(ROOT)/lib/quickjs/src
QJS_SRC := $(addprefix $(ROOT)/lib/quickjs/src/,quickjs.c libregexp.c libunicode.c cutils.c xsum.c)
QJS_OBJ := $(patsubst $(ROOT)/lib/quickjs/src/%.c,$(BUILD)/qjs/%.o,$(QJS_SRC))
$(BUILD)/qjs/%.o: $(ROOT)/lib/quickjs/src/%.c
	@mkdir -p $(dir $@) && $(CC) -O2 -w $(EXTRA) -c $< -o $@
# Packages and storage (src/pkg), the PawBoop run game (src/run). docs/ecosystem.md.
APP_CPP += $(wildcard $(ROOT)/src/pkg/*.cpp) $(wildcard $(ROOT)/src/run/*.cpp)
# The widget families (src/wid, docs/widgets.md).
APP_CPP += $(wildcard $(ROOT)/src/wid/*.cpp)
# The mic's listener and the "Gâu" wake word (src/wake: the same C as the board and the training tools).
APP_CPP += $(wildcard $(ROOT)/src/wake/*.cpp)
APP_C += $(wildcard $(ROOT)/src/wake/*.c)
APP_CPP += $(wildcard $(ROOT)/src/j2me/*.cpp)
# Hồ cá cảnh, the aquarium (src/tank, docs/tank.md).
APP_CPP += $(wildcard $(ROOT)/src/tank/*.cpp)
APP_C += $(wildcard $(ROOT)/src/run/*.c)
# The board's descriptor (src/boards/boards.cpp, the registry: the 3.5" board's; EXTRA=-DBOARD_ES3C28P the 2.8" one's).
# The boards' drivers (src/boards/<name>/, src/hal/hal.cpp) are not built here: sim/stubs.cpp stands in for them.
APP_CPP += $(ROOT)/src/boards/boards.cpp
# SKIP=run leaves out src/<dir> while someone is in the middle of writing it (parallel work).
APP_CPP := $(filter-out $(foreach d,$(SKIP),$(ROOT)/src/$(d)/%),$(APP_CPP))
APP_C := $(filter-out $(foreach d,$(SKIP),$(ROOT)/src/$(d)/%),$(APP_C))
APP_OBJ := $(patsubst $(ROOT)/src/%,$(BUILD)/app/%.o,$(APP_CPP) $(APP_C)) $(OPUS_OBJ) $(QJS_OBJ)
SIM_OBJ := $(BUILD)/sim/main.o $(BUILD)/sim/stubs.o

all: $(BUILD)/espos-sim

$(BUILD)/liblvgl.a: $(LV_OBJ)
	@rm -f $@ && ar rcs $@ $^

$(BUILD)/lvgl/%.o: $(LV)/src/%.c
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/app/%.cpp.o: $(ROOT)/src/%.cpp
	@mkdir -p $(dir $@) && $(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/app/%.c.o: $(ROOT)/src/%.c
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sim/%.o: %.cpp
	@mkdir -p $(dir $@) && $(CXX) $(CXXFLAGS) -c $< -o $@

# -lz: the built-in art is deflated (src/artz.cpp inflates it with zlib here, the ROM's tinfl on the board).
# -lcurl: net.request for JavaScript apps (src/js/jsnet.cpp) talks to real servers from the simulator (a local mock).
$(BUILD)/espos-sim: $(APP_OBJ) $(SIM_OBJ) $(BUILD)/liblvgl.a
	$(CXX) $(EXTRA) $^ -lz -lcurl -o $@

# The live simulator (sim/live.cpp, SDL2): make -C sim live, or tools/sim-live.sh.
SDL2_CONFIG ?= $(firstword $(wildcard /opt/homebrew/bin/sdl2-config /usr/local/bin/sdl2-config) sdl2-config)
SDL_CFLAGS = $(shell $(SDL2_CONFIG) --cflags)
SDL_LIBS = $(shell $(SDL2_CONFIG) --libs) -framework ApplicationServices
.PHONY: all live clean
live: $(BUILD)/espos-live
$(BUILD)/sim/main-live.o: main.cpp
	@mkdir -p $(dir $@) && $(CXX) $(CXXFLAGS) -DSIM_LIVE $(SDL_CFLAGS) -c $< -o $@
$(BUILD)/sim/live.o: live.cpp live.h realsize.h
	@mkdir -p $(dir $@) && $(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c $< -o $@
$(BUILD)/sim/realsize.o: realsize.cpp realsize.h
	@mkdir -p $(dir $@) && $(CXX) -O1 -g $(EXTRA) $(SDL_CFLAGS) -c $< -o $@
$(BUILD)/espos-live: $(APP_OBJ) $(BUILD)/sim/main-live.o $(BUILD)/sim/live.o $(BUILD)/sim/realsize.o $(BUILD)/sim/stubs.o $(BUILD)/liblvgl.a
	$(CXX) $(EXTRA) $^ -lz -lcurl $(SDL_LIBS) -o $@

# Header changes rebuild what uses them.
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD)
