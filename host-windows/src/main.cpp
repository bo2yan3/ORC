// orc-host: streams one Windows monitor to an ORC client and applies its input.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "capture.h"
#include "encoder.h"
#include "input.h"
#include "net.h"
#include "protocol.h"

using namespace orc;
using Clock = std::chrono::steady_clock;

struct Options {
  std::string token;
  std::string bind;  // empty = all interfaces
  uint16_t port = kDefaultPort;
  int bitrate_mbps = 30;
  int fps = 60;
  int adapter = 0;
  int output = 0;
};

static void usage() {
  std::printf(
      "usage: orc-host --token SECRET [--bind ADDR] [--port N] [--bitrate MBPS]\n"
      "                [--fps N] [--adapter N] [--output N]\n"
      "  --bind     address to listen on (use your Tailscale IP, e.g. 100.x.y.z)\n"
      "  --adapter  GPU index, --output monitor index on that GPU (default 0 0)\n");
}

static bool parse(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    const char* v = nullptr;
    if (a == "--token" && (v = val())) o.token = v;
    else if (a == "--bind" && (v = val())) o.bind = v;
    else if (a == "--port" && (v = val())) o.port = uint16_t(std::atoi(v));
    else if (a == "--bitrate" && (v = val())) o.bitrate_mbps = std::atoi(v);
    else if (a == "--fps" && (v = val())) o.fps = std::atoi(v);
    else if (a == "--adapter" && (v = val())) o.adapter = std::atoi(v);
    else if (a == "--output" && (v = val())) o.output = std::atoi(v);
    else return false;
  }
  return !o.token.empty() && o.fps > 0 && o.bitrate_mbps > 0;
}

static bool constant_time_equal(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= uint8_t(a[i] ^ b[i]);
  return diff == 0;
}

static void set_recv_timeout(SOCKET s, DWORD ms) {
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms);
}

// Returns true if the client is allowed in.
static bool handshake(Peer& peer, const Options& opt, int w, int h) {
  auto m = peer.recv();
  HelloStatus status = HelloStatus::BadVersion;
  if (m && m->type == MsgType::Hello && m->payload.size() >= 8 &&
      std::memcmp(m->payload.data(), "ORC1", 4) == 0) {
    uint16_t version = get_u16(&m->payload[4]);
    uint16_t tlen = get_u16(&m->payload[6]);
    if (version != kProtocolVersion) {
      status = HelloStatus::BadVersion;
    } else if (m->payload.size() >= 8u + tlen) {
      std::string token(reinterpret_cast<const char*>(&m->payload[8]), tlen);
      status = constant_time_equal(token, opt.token) ? HelloStatus::Ok : HelloStatus::BadToken;
    } else {
      status = HelloStatus::BadToken;
    }
  } else if (!m) {
    return false;
  }
  std::vector<uint8_t> ack;
  ack.push_back(uint8_t(status));
  put_u16(ack, uint16_t(w));
  put_u16(ack, uint16_t(h));
  ack.push_back(kCodecHEVC);
  peer.send(MsgType::HelloAck, 0, ack);
  return status == HelloStatus::Ok;
}

static void run_session(SOCKET client, const Options& opt) {
  Peer peer(client);
  std::printf("session: connection from %s\n", peer.address().c_str());

  DesktopCapture cap;
  if (!cap.init(opt.adapter, opt.output)) {
    std::fprintf(stderr, "session: capture init failed\n");
    return;
  }

  set_recv_timeout(client, 5000);
  if (!handshake(peer, opt, cap.width(), cap.height())) {
    std::printf("session: handshake rejected\n");
    return;
  }
  set_recv_timeout(client, 0);
  std::printf("session: streaming %dx%d\n", cap.width(), cap.height());

  InputInjector input;
  input.set_monitor(cap.monitor_rect());

  std::atomic<bool> running{true};
  std::atomic<bool> want_keyframe{true};

  std::thread video([&] {
    Encoder enc;
    int enc_w = 0, enc_h = 0;
    Frame frame;
    bool have_frame = false;
    const auto t0 = Clock::now();
    const auto interval = std::chrono::microseconds(1000000 / opt.fps);
    auto next_slot = Clock::now();

    auto sink = [&](const uint8_t* data, size_t size, bool key, int64_t pts) {
      uint8_t pts_be[8];
      for (int i = 0; i < 8; ++i) pts_be[i] = uint8_t(uint64_t(pts) >> (56 - 8 * i));
      return peer.send2(MsgType::Video, key ? kFlagKeyframe : 0, pts_be, 8, data, size);
    };

    while (running) {
      // Pace to the target fps; the duplication API keeps the latest image for us.
      std::this_thread::sleep_until(next_slot);
      next_slot = std::max(next_slot + interval, Clock::now());

      auto r = cap.next(frame, 2);
      if (r == DesktopCapture::Result::Error) break;
      bool fresh = (r == DesktopCapture::Result::NewFrame);
      if (fresh) have_frame = true;
      bool key = want_keyframe.exchange(false);
      if (!have_frame || (!fresh && !key)) {
        if (key) want_keyframe = true;  // try again once we have a frame
        continue;
      }

      if (frame.width != enc_w || frame.height != enc_h) {
        EncoderConfig cfg;
        cfg.width = frame.width;
        cfg.height = frame.height;
        cfg.fps = opt.fps;
        cfg.bitrate_mbps = opt.bitrate_mbps;
        if (!enc.init(cfg)) break;
        enc_w = frame.width;
        enc_h = frame.height;
        input.set_monitor(cap.monitor_rect());
        key = true;
      }

      auto pts = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
      if (!enc.encode(frame, pts, key, sink)) break;
    }
    running = false;
    peer.shutdown();  // unblocks recv() on the main thread
  });

  while (running) {
    auto m = peer.recv();
    if (!m) break;
    switch (m->type) {
      case MsgType::KeyframeReq:
        want_keyframe = true;
        break;
      case MsgType::Ping:
        peer.send(MsgType::Pong, 0, m->payload);
        break;
      case MsgType::MouseMove:
      case MsgType::MouseButton:
      case MsgType::MouseWheel:
      case MsgType::Key:
      case MsgType::ReleaseAll:
        input.apply(*m);
        break;
      default:
        break;  // unknown types are ignored by design
    }
  }
  running = false;
  peer.shutdown();
  video.join();
  input.release_all();
  std::printf("session: ended\n");
}

int main(int argc, char** argv) {
  Options opt;
  if (!parse(argc, argv, opt)) {
    usage();
    return 2;
  }
  // Real pixel coordinates on high-DPI monitors.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  WinsockInit wsa;
  SOCKET ls = listen_on(opt.bind, opt.port);
  if (ls == INVALID_SOCKET) {
    std::fprintf(stderr, "could not listen on %s:%u\n", opt.bind.empty() ? "*" : opt.bind.c_str(),
                 opt.port);
    return 1;
  }
  std::printf("orc-host listening on %s:%u\n", opt.bind.empty() ? "*" : opt.bind.c_str(), opt.port);

  for (;;) {
    SOCKET c = accept(ls, nullptr, nullptr);
    if (c == INVALID_SOCKET) continue;
    run_session(c, opt);  // one client at a time
  }
}
