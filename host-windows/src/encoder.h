#pragma once
#include <cstdint>
#include <functional>
#include <string>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace orc {

struct Frame;

struct EncoderConfig {
  int width = 0;
  int height = 0;
  int fps = 60;
  int bitrate_mbps = 30;
  std::string codec = "hevc_nvenc";
};

// Hardware HEVC encoder (NVENC through FFmpeg), tuned for minimum latency.
class Encoder {
 public:
  using PacketSink = std::function<bool(const uint8_t* data, size_t size, bool keyframe, int64_t pts_us)>;

  ~Encoder();
  bool init(const EncoderConfig& cfg);
  // Encodes one frame and passes every resulting packet to `sink`.
  // Returns false if the sink failed (peer gone) or the encoder errored.
  bool encode(const Frame& frame, int64_t pts_us, bool force_keyframe, const PacketSink& sink);

 private:
  void close();
  AVCodecContext* ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* pkt_ = nullptr;
};

}  // namespace orc
