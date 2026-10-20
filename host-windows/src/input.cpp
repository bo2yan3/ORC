#include "input.h"

#include <windows.h>

#include <cmath>
#include <vector>

namespace orc {

void InputInjector::apply(const Message& m) {
  const auto& p = m.payload;
  switch (m.type) {
    case MsgType::MouseMove:
      if (p.size() >= 4) mouse_move(get_u16(&p[0]), get_u16(&p[2]));
      break;
    case MsgType::MouseButton:
      if (p.size() >= 2) mouse_button(p[0], p[1] != 0);
      break;
    case MsgType::MouseWheel:
      if (p.size() >= 4) mouse_wheel(int16_t(get_u16(&p[0])), int16_t(get_u16(&p[2])));
      break;
    case MsgType::Key:
      if (p.size() >= 3) key(get_u16(&p[0]), p[2] != 0);
      break;
    case MsgType::ReleaseAll:
      release_all();
      break;
    default:
      break;
  }
}

void InputInjector::mouse_move(uint16_t nx, uint16_t ny) {
  MonitorRect mon;
  {
    std::lock_guard<std::mutex> lock(mon_mu_);
    mon = mon_;
  }
  // Normalized position on the streamed monitor -> desktop pixel.
  const double fx = nx / 65535.0, fy = ny / 65535.0;
  const double px = mon.left + fx * double(mon.right - mon.left - 1);
  const double py = mon.top + fy * double(mon.bottom - mon.top - 1);

  // Desktop pixel -> SendInput's 0..65535 range across the whole virtual desktop.
  const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (vw <= 1 || vh <= 1) return;

  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dx = LONG(std::lround((px - vx) * 65535.0 / (vw - 1)));
  in.mi.dy = LONG(std::lround((py - vy) * 65535.0 / (vh - 1)));
  in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  SendInput(1, &in, sizeof in);
}

void InputInjector::mouse_button(uint8_t button, bool down) {
  INPUT in{};
  in.type = INPUT_MOUSE;
  switch (button) {
    case 0: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
    case 1: in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
    case 2: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case 3:
    case 4:
      in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
      in.mi.mouseData = button == 3 ? XBUTTON1 : XBUTTON2;
      break;
    default: return;
  }
  if (down) held_buttons_.insert(button);
  else held_buttons_.erase(button);
  SendInput(1, &in, sizeof in);
}

void InputInjector::mouse_wheel(int16_t dx, int16_t dy) {
  INPUT in[2]{};
  UINT n = 0;
  if (dy != 0) {
    in[n].type = INPUT_MOUSE;
    in[n].mi.dwFlags = MOUSEEVENTF_WHEEL;
    in[n].mi.mouseData = DWORD(int(dy));
    ++n;
  }
  if (dx != 0) {
    in[n].type = INPUT_MOUSE;
    in[n].mi.dwFlags = MOUSEEVENTF_HWHEEL;
    in[n].mi.mouseData = DWORD(int(dx));
    ++n;
  }
  if (n) SendInput(n, in, sizeof(INPUT));
}

static bool is_extended(uint16_t vk) {
  switch (vk) {
    case VK_RCONTROL: case VK_RMENU: case VK_INSERT: case VK_DELETE:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_LWIN: case VK_RWIN: case VK_APPS: case VK_DIVIDE: case VK_NUMLOCK:
      return true;
    default:
      return false;
  }
}

void InputInjector::key(uint16_t vk, bool down) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  // Send scan codes as well as the VK: some apps (and games) only read scan codes.
  UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
  in.ki.wVk = vk;
  in.ki.wScan = WORD(sc & 0xFF);
  in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP);
  if (is_extended(vk) || (sc & 0xE000) == 0xE000) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  if (down) held_keys_.insert(vk);
  else held_keys_.erase(vk);
  SendInput(1, &in, sizeof in);
}

void InputInjector::release_all() {
  std::vector<uint16_t> keys(held_keys_.begin(), held_keys_.end());
  for (uint16_t vk : keys) key(vk, false);
  std::vector<uint8_t> buttons(held_buttons_.begin(), held_buttons_.end());
  for (uint8_t b : buttons) mouse_button(b, false);
}

}  // namespace orc
