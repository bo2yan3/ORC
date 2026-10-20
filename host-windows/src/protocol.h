// ORC wire protocol v1 — see docs/PROTOCOL.md
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace orc {

constexpr uint16_t kDefaultPort = 47800;
constexpr uint16_t kProtocolVersion = 1;
constexpr uint32_t kMaxPayload = 16u * 1024u * 1024u;
constexpr size_t kHeaderSize = 8;

enum class MsgType : uint8_t {
  Hello = 1,
  HelloAck = 2,
  Video = 3,
  KeyframeReq = 4,
  Ping = 5,
  Pong = 6,
  MouseMove = 16,
  MouseButton = 17,
  MouseWheel = 18,
  Key = 19,
  ReleaseAll = 20,
};

enum : uint8_t { kFlagKeyframe = 0x01 };

enum class HelloStatus : uint8_t { Ok = 0, BadToken = 1, BadVersion = 2 };
enum : uint8_t { kCodecHEVC = 1 };

struct Message {
  MsgType type{};
  uint8_t flags = 0;
  std::vector<uint8_t> payload;
};

// --- big-endian helpers --------------------------------------------------

inline void put_u16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(uint8_t(v >> 8));
  b.push_back(uint8_t(v));
}
inline void put_u32(std::vector<uint8_t>& b, uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) b.push_back(uint8_t(v >> s));
}
inline void put_u64(std::vector<uint8_t>& b, uint64_t v) {
  for (int s = 56; s >= 0; s -= 8) b.push_back(uint8_t(v >> s));
}
inline uint16_t get_u16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
inline uint32_t get_u32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline uint64_t get_u64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}

inline void write_header(uint8_t out[kHeaderSize], MsgType t, uint8_t flags, uint32_t len) {
  out[0] = uint8_t(t);
  out[1] = flags;
  out[2] = out[3] = 0;
  out[4] = uint8_t(len >> 24);
  out[5] = uint8_t(len >> 16);
  out[6] = uint8_t(len >> 8);
  out[7] = uint8_t(len);
}

}  // namespace orc
