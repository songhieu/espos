#pragma once
// MIDP for the J2ME VM: the natives of com.espos.j2me.Native (j2me/classlib/com/espos/j2me/Native.java) and the
// host they talk to. The device (src/j2me/app_j2me.cpp) and the computer's runner (tools/j2me) each give a Host: where
// frames go, the soft key labels, sound, the game's properties and where its record stores live. Events go the other
// way through post()/key(), from any thread.
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace j2me {

class Vm;

struct MidpHost {
  int screenW = 240, screenH = 320;
  // The game's screen changed in x, y, w, h (ARGB pixels, stride = screenW). Called on the VM's thread.
  std::function<void(const uint32_t* px, int x, int y, int w, int h)> flush;
  std::function<void(const std::string& left, const std::string& right)> softLabels;
  std::function<void(int ms)> vibrate;
  std::function<void(int note, int ms, int volume)> tone;
  std::function<void()> exit;  // MIDlet.notifyDestroyed
  std::function<void(int w, int h)> screenHint;  // the game's Canvas says its screen is w x h (Native.screenHint)
  std::map<std::string, std::string> props;  // MIDlet-Name, MIDlet-1, ... (manifest / JAD)
  std::string rmsDir;  // the game's record stores, a folder ending in '/'
  // Runs the record stores' file work and returns its result. The board does it on the UI thread (the VM's task has
  // its stack in PSRAM: it must not touch flash, whose writes and reads turn the cache off). Unset: done in place.
  std::function<bool(const std::function<bool()>& job)> fileJob;

  // ---- from the host (any thread)
  // Event types as Native.poll: 1 key pressed, 2 released, 3 repeated (a = key code); 4 pointer pressed,
  // 5 released, 6 dragged (a, b = x, y on the game's screen); 7 paused, 8 shown again, 9 the user leaves.
  void post(int type, int a = 0, int b = 0);
  // A key went down or up (keypad): its event, and the GameCanvas key state bits.
  void key(int code, bool down);
  // ---- the natives' side
  int poll(int* ev);  // the next event's type and data, 0 = none
  uint32_t keyStates();

 private:
  struct Ev {
    int type, a, b;
  };
  std::mutex mu;
  std::deque<Ev> events;
  uint32_t held = 0, latched = 0;  // under mu
};

// Registers every com.espos.j2me.Native method on vm, talking to host (which must outlive vm).
void registerMidp(Vm& vm, MidpHost& host);

// GameCanvas bit for a key code (UP_PRESSED = 1 << UP ...), 0 for none.
uint32_t gameBit(int keyCode);

}  // namespace j2me
