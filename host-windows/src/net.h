#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>
#include <optional>
#include <string>

#include "protocol.h"

namespace orc {

class WinsockInit {
 public:
  WinsockInit();
  ~WinsockInit();
};

// Listening socket bound to bind_addr:port. Returns INVALID_SOCKET on failure.
SOCKET listen_on(const std::string& bind_addr, uint16_t port);

// A connected peer. send() may be called from several threads; recv() from one.
class Peer {
 public:
  explicit Peer(SOCKET s);
  ~Peer();
  Peer(const Peer&) = delete;
  Peer& operator=(const Peer&) = delete;

  bool send(MsgType type, uint8_t flags, const uint8_t* payload, size_t len);
  bool send(MsgType type, uint8_t flags, const std::vector<uint8_t>& payload) {
    return send(type, flags, payload.data(), payload.size());
  }
  // Two-part payload (e.g. 8-byte PTS + encoder packet) without an extra copy.
  bool send2(MsgType type, uint8_t flags, const uint8_t* a, size_t alen, const uint8_t* b,
             size_t blen);

  // Blocks until a full message arrives. nullopt = disconnected or protocol error.
  std::optional<Message> recv();

  void shutdown();
  std::string address() const { return addr_; }

 private:
  bool send_all(const uint8_t* p, size_t n);
  bool recv_all(uint8_t* p, size_t n);

  SOCKET s_;
  std::mutex send_mu_;
  std::string addr_;
};

}  // namespace orc
