# EspOS Bot simulator (sim/bot/sim.cpp): the board's src/bot code with SDL2 (window, mic, speaker) and libcurl.
#   make -C sim/bot            -> sim/bot/build-sim/bot-sim     (tools/bot/sim.sh builds and starts it)
ROOT := ../..
BUILD ?= build-sim
AJ := $(ROOT)/.pio/libdeps/s3desk/ArduinoJson/src
SDL2_CONFIG ?= $(firstword $(wildcard /opt/homebrew/bin/sdl2-config /usr/local/bin/sdl2-config) sdl2-config)
SDL_CFLAGS = $(shell $(SDL2_CONFIG) --cflags)
SDL_LIBS = $(shell $(SDL2_CONFIG) --libs)
FLAGS := -O1 -g -MMD -MP -DBOT_SIM -DFW_VERSION='"espos-bot-0.1.0-sim"' -DESPOS_API='"http://127.0.0.1:8098"' \
  -Ishim -I$(ROOT)/src $(EXTRA)
CXXFLAGS := $(FLAGS) -std=gnu++17 -I$(AJ) -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 \
  -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -Wall -Wno-unused-parameter -Wno-unknown-pragmas $(SDL_CFLAGS)
BOT := app talk ui gfx art logic store usb link ear spk portal
SRC := $(addprefix $(ROOT)/src/bot/,$(addsuffix .cpp,$(BOT))) $(ROOT)/src/lic.cpp $(ROOT)/src/trace.cpp $(ROOT)/src/lunar.cpp \
  sim.cpp net_sim.cpp transport_curl.cpp
CSRC := $(ROOT)/src/wake/wake_fe.c $(ROOT)/src/wake/wake_rt.c $(ROOT)/src/bot/vendor/qrcodegen.c
OBJ := $(patsubst $(ROOT)/%.cpp,$(BUILD)/%.o,$(filter $(ROOT)/%,$(SRC))) $(patsubst %.cpp,$(BUILD)/sim/%.o,$(filter-out $(ROOT)/%,$(SRC))) \
  $(patsubst $(ROOT)/%.c,$(BUILD)/%.o,$(CSRC))

$(BUILD)/bot-sim: $(OBJ)
	$(CXX) $(EXTRA) $^ $(SDL_LIBS) -lcurl -lz -o $@

$(BUILD)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@) && $(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/sim/%.o: %.cpp
	@mkdir -p $(dir $@) && $(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@) && $(CC) $(FLAGS) -std=c99 -c $< -o $@

-include $(OBJ:.o=.d)
