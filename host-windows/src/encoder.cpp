#include "encoder.h"

#include <cstdio>
#include <cstring>

#include "capture.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

namespace orc {

static void log_av_error(const char* what, int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(err, buf, sizeof buf);
  std::fprintf(stderr, "encoder: %s: %s\n", what, buf);
}

Encoder::~Encoder() { close(); }

void Encoder::close() {
  if (pkt_) av_packet_free(&pkt_);
  if (frame_) av_frame_free(&frame_);
  if (ctx_) avcodec_free_context(&ctx_);
}

bool Encoder::init(const EncoderConfig& cfg) {
  close();
  const AVCodec* codec = avcodec_find_encoder_by_name(cfg.codec.c_str());
  if (!codec) {
    std::fprintf(stderr, "encoder: %s not available. Was FFmpeg built with nvcodec?\n",
                 cfg.codec.c_str());
    return false;
  }
  ctx_ = avcodec_alloc_context3(codec);
  ctx_->width = cfg.width;
  ctx_->height = cfg.height;
  ctx_->time_base = AVRational{1, 1000000};  // PTS in microseconds
  ctx_->framerate = AVRational{cfg.fps, 1};
  // NVENC takes BGRx directly and converts to YUV 4:2:0 on the GPU.
  ctx_->pix_fmt = AV_PIX_FMT_BGR0;
  ctx_->max_b_frames = 0;
  ctx_->gop_size = cfg.fps * 10;  // periodic refresh; client can also ask for keyframes
  ctx_->bit_rate = int64_t(cfg.bitrate_mbps) * 1000000;
  ctx_->rc_max_rate = ctx_->bit_rate;
  // ~1 frame of VBV buffer keeps frame sizes even, which keeps latency low.
  ctx_->rc_buffer_size = int(ctx_->bit_rate / cfg.fps);
  // Colour signalling is left at FFmpeg/NVENC defaults for M1. If colours look
  // slightly washed out or too contrasty on the Mac, it's a range/matrix mismatch:
  // see docs/ROADMAP.md (M2).

  av_opt_set(ctx_->priv_data, "preset", "p1", 0);    // fastest
  av_opt_set(ctx_->priv_data, "tune", "ull", 0);     // ultra-low latency
  av_opt_set(ctx_->priv_data, "rc", "cbr", 0);
  av_opt_set(ctx_->priv_data, "zerolatency", "1", 0);
  av_opt_set(ctx_->priv_data, "delay", "0", 0);
  av_opt_set(ctx_->priv_data, "forced-idr", "1", 0);  // I-frame requests become IDR
  av_opt_set(ctx_->priv_data, "aud", "1", 0);         // access unit delimiters

  int err = avcodec_open2(ctx_, codec, nullptr);
  if (err < 0) {
    log_av_error("avcodec_open2", err);
    return false;
  }

  frame_ = av_frame_alloc();
  frame_->format = ctx_->pix_fmt;
  frame_->width = cfg.width;
  frame_->height = cfg.height;
  if ((err = av_frame_get_buffer(frame_, 0)) < 0) {
    log_av_error("av_frame_get_buffer", err);
    return false;
  }
  pkt_ = av_packet_alloc();
  std::printf("encoder: %s %dx%d @ %d fps, %d Mbps\n", cfg.codec.c_str(), cfg.width, cfg.height,
              cfg.fps, cfg.bitrate_mbps);
  return true;
}

bool Encoder::encode(const Frame& frame, int64_t pts_us, bool force_keyframe,
                     const PacketSink& sink) {
  int err = av_frame_make_writable(frame_);
  if (err < 0) return false;
  for (int y = 0; y < frame.height; ++y)
    std::memcpy(frame_->data[0] + size_t(y) * frame_->linesize[0],
                frame.bgra.data() + size_t(y) * frame.stride, size_t(frame.width) * 4);

  frame_->pts = pts_us;
  frame_->pict_type = force_keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
  if (force_keyframe) frame_->flags |= AV_FRAME_FLAG_KEY;
  else frame_->flags &= ~AV_FRAME_FLAG_KEY;

  if ((err = avcodec_send_frame(ctx_, frame_)) < 0) {
    log_av_error("avcodec_send_frame", err);
    return false;
  }
  for (;;) {
    err = avcodec_receive_packet(ctx_, pkt_);
    if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) return true;
    if (err < 0) {
      log_av_error("avcodec_receive_packet", err);
      return false;
    }
    bool key = (pkt_->flags & AV_PKT_FLAG_KEY) != 0;
    bool ok = sink(pkt_->data, size_t(pkt_->size), key, pkt_->pts);
    av_packet_unref(pkt_);
    if (!ok) return false;
  }
}

}  // namespace orc
