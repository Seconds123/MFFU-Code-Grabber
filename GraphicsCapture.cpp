#include "GraphicsCapture.h"
#include <iostream>

// You must call this ONCE in your main() function:
// winrt::init_apartment(winrt::apartment_type::multi_threaded);

GraphicsCapture::~GraphicsCapture() {
    StopCapture();
}

bool GraphicsCapture::StartCapture(HWND hwnd) {
    if (!CreateD3DDevice()) {
        std::cerr << "Failed to create D3D11 device." << std::endl;
        return false;
    }

    try {
        auto interop = winrt::get_activation_factory<winrt::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        interop->CreateForWindow(hwnd, winrt::guid_of<winrt::IGraphicsCaptureItem>(), winrt::put_abi(m_item));
        if (!m_item) { return false; }

        m_lastSize = m_item.Size();

        m_framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(m_device, winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_lastSize);
        m_session = m_framePool.CreateCaptureSession(m_item);
        m_framePool.FrameArrived({ this, &GraphicsCapture::OnFrameArrived });
        m_session.StartCapture();
    }
    catch (winrt::hresult_error const& e) {
        std::wcerr << L"Capture initialization failed: " << e.message().c_str() << std::endl;
        StopCapture();
        return false;
    }
    return true;
}

void GraphicsCapture::StopCapture() {
    std::lock_guard<std::mutex> lock(m_d3dMutex);

    if (m_session) {
        m_session.Close();
        m_session = nullptr;
    }
    if (m_framePool) {
        m_framePool.Close();
        m_framePool = nullptr;
    }
    m_item = nullptr;
    m_d3dDevice = nullptr;
    m_d3dContext = nullptr;
    m_device = nullptr;
}

cv::Mat GraphicsCapture::GetLatestFrame() {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    return m_latestFrame.clone();
}

void GraphicsCapture::OnFrameArrived(winrt::Direct3D11CaptureFramePool const& sender, winrt::IInspectable const&) {
    std::lock_guard<std::mutex> lock(m_d3dMutex);

    if (!m_session) {
        return;
    }

    try {
        auto frame = sender.TryGetNextFrame();
        if (!frame) return;

        auto surface = frame.Surface();

        // Fallback conversion: use IDirect3DDxgiInterfaceAccess to get ID3D11Texture2D
        winrt::com_ptr<ID3D11Texture2D> frameTexture;
        auto access = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), frameTexture.put_void()));

        D3D11_TEXTURE2D_DESC desc;
        frameTexture->GetDesc(&desc);

        winrt::com_ptr<ID3D11Texture2D> stagingTexture;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        winrt::check_hresult(m_d3dDevice->CreateTexture2D(&desc, nullptr, stagingTexture.put()));

        m_d3dContext->CopyResource(stagingTexture.get(), frameTexture.get());

        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = m_d3dContext->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_latestFrame = cv::Mat(desc.Height, desc.Width, CV_8UC4, mapped.pData, mapped.RowPitch).clone();
            }
            m_d3dContext->Unmap(stagingTexture.get(), 0);
        }
    }
    catch (winrt::hresult_error const& e) {
        std::wcerr << L"Frame processing failed: " << e.message().c_str() << std::endl;
        StopCapture();
    }
}

bool GraphicsCapture::CreateD3DDevice() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, m_d3dDevice.put(), nullptr, m_d3dContext.put());
    if (FAILED(hr)) return false;

    winrt::com_ptr<IDXGIDevice> dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), reinterpret_cast<IInspectable**>(winrt::put_abi(m_device))));

    return true;
}