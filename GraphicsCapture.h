#ifndef GRAPHICS_CAPTURE_H
#define GRAPHICS_CAPTURE_H

#include <windows.h>
#include <opencv2/opencv.hpp>
#include <atomic>
#include <mutex>

// --- All necessary WinRT and DirectX headers ---
#include <d3d11.h>
#include <dxgi1_2.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

// Helper for converting WinRT surfaces to DXGI interfaces
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

// --- Use a clear namespace alias ---
namespace winrt {
    using namespace Windows::Foundation;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
}

class GraphicsCapture {
public:
    ~GraphicsCapture();
    bool StartCapture(HWND hwnd);
    void StopCapture();
    cv::Mat GetLatestFrame();
    bool IsCapturing() { return m_session != nullptr; }

private:
    void OnFrameArrived(winrt::Direct3D11CaptureFramePool const& sender, winrt::IInspectable const&);
    bool CreateD3DDevice();

    std::mutex m_d3dMutex;

    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
    winrt::IDirect3DDevice m_device{ nullptr };
    winrt::GraphicsCaptureItem m_item{ nullptr };
    winrt::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::GraphicsCaptureSession m_session{ nullptr };
    winrt::SizeInt32 m_lastSize;

    std::mutex m_frameMutex;
    cv::Mat m_latestFrame;
};

#endif // GRAPHICS_CAPTURE_H