#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace orc {

struct Frame {
  int width = 0;
  int height = 0;
  int stride = 0;             // bytes per row in `bgra`
  std::vector<uint8_t> bgra;  // B, G, R, X per pixel
};

// Desktop rectangle of the captured monitor, in virtual-desktop pixels.
struct MonitorRect {
  long left = 0, top = 0, right = 0, bottom = 0;
};

// Captures one monitor with DXGI Desktop Duplication.
class DesktopCapture {
 public:
  // adapter/output index: 0/0 is normally the primary monitor on the main GPU.
  bool init(int adapter_index = 0, int output_index = 0);

  enum class Result { NewFrame, NoChange, Error };

  // Waits up to timeout_ms for the screen to change. On NewFrame, `out` holds
  // the full current frame. Recovers from ACCESS_LOST internally.
  Result next(Frame& out, unsigned timeout_ms);

  int width() const { return width_; }
  int height() const { return height_; }
  MonitorRect monitor_rect() const { return rect_; }

 private:
  bool create_duplication();

  int adapter_index_ = 0, output_index_ = 0;
  int width_ = 0, height_ = 0;
  MonitorRect rect_;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGIOutput1> output_;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> dupl_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
};

}  // namespace orc
