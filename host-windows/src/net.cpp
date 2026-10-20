#include "net.h"

#include <cstdio>

namespace orc {

WinsockInit::WinsockInit() {
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) std::fprintf(stderr, "WSAStartup failed\n");
}
WinsockInit::~WinsockInit() { WSACleanup(); }

SOCKET listen_on(const std::string& bind_addr, uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  addrinfo* res = nullptr;
  const char* node = bind_addr.empty() ? nullptr : bind_addr.c_str();
  std::string port_s = std::to_string(port);
  if (getaddrinfo(node, port_s.c_str(), &hints, &res) != 0) return INVALID_SOCKET;

  SOCKET ls = INVALID_SOCKET;
  for (addrinfo* ai = res; ai; ai = ai->ai_next) {
    ls = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (ls == INVALID_SOCKET) continue;
    BOOL yes = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&yes), sizeof yes);
    if (bind(ls, ai->ai_addr, int(ai->ai_addrlen)) == 0 && listen(ls, 1) == 0) break;
    closesocket(ls);
    ls = INVALID_SOCKET;
  }
  freeaddrinfo(res);
  return ls;
}

Peer::Peer(SOCKET s) : s_(s) {
  BOOL yes = TRUE;
  setsockopt(s_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&yes), sizeof yes);
  int sndbuf = 4 * 1024 * 1024;
  setsockopt(s_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&sndbuf), sizeof sndbuf);

  sockaddr_storage ss{};
  int sl = sizeof ss;
  if (getpeername(s_, reinterpret_cast<sockaddr*>(&ss), &sl) == 0) {
    char host[NI_MAXHOST] = {};
    getnameinfo(reinterpret_cast<sockaddr*>(&ss), sl, host, sizeof host, nullptr, 0, NI_NUMERICHOST);
    addr_ = host;
  }
}

Peer::~Peer() {
  if (s_ != INVALID_SOCKET) closesocket(s_);
}

void Peer::shutdown() { ::shutdown(s_, SD_BOTH); }

bool Peer::send_all(const uint8_t* p, size_t n) {
  while (n > 0) {
    int chunk = int(n > (1 << 30) ? (1 << 30) : n);
    int r = ::send(s_, reinterpret_cast<const char*>(p), chunk, 0);
    if (r <= 0) return false;
    p += r;
    n -= size_t(r);
  }
  return true;
}

bool Peer::recv_all(uint8_t* p, size_t n) {
  while (n > 0) {
    int r = ::recv(s_, reinterpret_cast<char*>(p), int(n), 0);
    if (r <= 0) return false;
    p += r;
    n -= size_t(r);
  }
  return true;
}

bool Peer::send(MsgType type, uint8_t flags, const uint8_t* payload, size_t len) {
  return send2(type, flags, payload, len, nullptr, 0);
}

bool Peer::send2(MsgType type, uint8_t flags, const uint8_t* a, size_t alen, const uint8_t* b,
                 size_t blen) {
  size_t total = alen + blen;
  if (total > kMaxPayload) return false;
  uint8_t hdr[kHeaderSize];
  write_header(hdr, type, flags, uint32_t(total));
  std::lock_guard<std::mutex> lock(send_mu_);
  if (!send_all(hdr, kHeaderSize)) return false;
  if (alen && !send_all(a, alen)) return false;
  if (blen && !send_all(b, blen)) return false;
  return true;
}

std::optional<Message> Peer::recv() {
  uint8_t hdr[kHeaderSize];
  if (!recv_all(hdr, kHeaderSize)) return std::nullopt;
  uint32_t len = get_u32(hdr + 4);
  if (len > kMaxPayload) return std::nullopt;
  Message m;
  m.type = MsgType(hdr[0]);
  m.flags = hdr[1];
  m.payload.resize(len);
  if (len && !recv_all(m.payload.data(), len)) return std::nullopt;
  return m;
}

}  // namespace orc
