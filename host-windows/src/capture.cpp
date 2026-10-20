#include "capture.h"

#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace orc {

bool DesktopCapture::init(int adapter_index, int output_index) {
  adapter_index_ = adapter_index;
  output_index_ = output_index;

  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

  ComPtr<IDXGIAdapter1> adapter;
  if (FAILED(factory->EnumAdapters1(UINT(adapter_index), &adapter))) {
    std::fprintf(stderr, "capture: no adapter %d\n", adapter_index);
    return false;
  }
  ComPtr<IDXGIOutput> output;
  if (FAILED(adapter->EnumOutputs(UINT(output_index), &output))) {
    std::fprintf(stderr, "capture: adapter %d has no output %d (is a monitor plugged into this GPU?)\n",
                 adapter_index, output_index);
    return false;
  }
  if (FAILED(output.As(&output_))) return false;

  DXGI_OUTPUT_DESC desc{};
  output_->GetDesc(&desc);
  rect_ = {desc.DesktopCoordinates.left, desc.DesktopCoordinates.top,
           desc.DesktopCoordinates.right, desc.DesktopCoordinates.bottom};

  D3D_FEATURE_LEVEL fl;
  HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &device_, &fl, &context_);
  if (FAILED(hr)) {
    std::fprintf(stderr, "capture: D3D11CreateDevice failed 0x%08lx\n", hr);
    return false;
  }
  return create_duplication();
}

bool DesktopCapture::create_duplication() {
  dupl_.Reset();
  staging_.Reset();
  HRESULT hr = output_->DuplicateOutput(device_.Get(), &dupl_);
  if (FAILED(hr)) {
    // E_ACCESSDENIED happens on the secure desktop (UAC / lock screen); retry later.
    std::fprintf(stderr, "capture: DuplicateOutput failed 0x%08lx\n", hr);
    return false;
  }
  DXGI_OUTDUPL_DESC dd{};
  dupl_->GetDesc(&dd);
  width_ = int(dd.ModeDesc.Width);
  height_ = int(dd.ModeDesc.Height);

  D3D11_TEXTURE2D_DESC td{};
  td.Width = dd.ModeDesc.Width;
  td.Height = dd.ModeDesc.Height;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_STAGING;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  return SUCCEEDED(device_->CreateTexture2D(&td, nullptr, &staging_));
}

DesktopCapture::Result DesktopCapture::next(Frame& out, unsigned timeout_ms) {
  if (!dupl_ && !create_duplication()) {
    Sleep(timeout_ms);
    return Result::NoChange;
  }

  DXGI_OUTDUPL_FRAME_INFO info{};
  ComPtr<IDXGIResource> res;
  HRESULT hr = dupl_->AcquireNextFrame(timeout_ms, &info, &res);
  if (hr == DXGI_ERROR_WAIT_TIMEOUT) return Result::NoChange;
  if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
    // Mode change, UAC prompt, fullscreen app switch... rebuild next time.
    dupl_.Reset();
    return Result::NoChange;
  }
  if (FAILED(hr)) return Result::Error;

  Result result = Result::NoChange;
  // LastPresentTime == 0 means only the mouse moved; we draw the cursor on the client.
  if (info.LastPresentTime.QuadPart != 0) {
    ComPtr<ID3D11Texture2D> tex;
    if (SUCCEEDED(res.As(&tex))) {
      context_->CopyResource(staging_.Get(), tex.Get());
      D3D11_MAPPED_SUBRESOURCE map{};
      if (SUCCEEDED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &map))) {
        out.width = width_;
        out.height = height_;
        out.stride = width_ * 4;
        out.bgra.resize(size_t(out.stride) * size_t(height_));
        const auto* src = static_cast<const uint8_t*>(map.pData);
        for (int y = 0; y < height_; ++y)
          std::memcpy(out.bgra.data() + size_t(y) * out.stride, src + size_t(y) * map.RowPitch,
                      size_t(out.stride));
        context_->Unmap(staging_.Get(), 0);
        result = Result::NewFrame;
      }
    }
  }
  dupl_->ReleaseFrame();
  return result;
}

}  // namespace orc
