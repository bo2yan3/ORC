#pragma once
#include <cstdint>
#include <mutex>
#include <set>

#include "capture.h"
#include "protocol.h"

namespace orc {

// Applies client input messages with SendInput and remembers what is held,
// so a dropped connection never leaves Ctrl or a mouse button stuck down.
class InputInjector {
 public:
  // Called from the video thread when the resolution changes.
  void set_monitor(const MonitorRect& r) {
    std::lock_guard<std::mutex> lock(mon_mu_);
    mon_ = r;
  }
  void apply(const Message& m);
  void release_all();

 private:
  void mouse_move(uint16_t nx, uint16_t ny);
  void mouse_button(uint8_t button, bool down);
  void mouse_wheel(int16_t dx, int16_t dy);
  void key(uint16_t vk, bool down);

  std::mutex mon_mu_;
  MonitorRect mon_;
  std::set<uint16_t> held_keys_;
  std::set<uint8_t> held_buttons_;
};

}  // namespace orc
