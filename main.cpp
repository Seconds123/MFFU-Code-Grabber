#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <winrt/base.h>
#include <d3d11.h>
#include <tchar.h>
#include <thread>
#include <string>
#include <vector>
#include <string.h>
#include <iostream>

#include "OCRProcessor.h"
#include "ConfigManager.h"
#include "utilities.h"
#include <gdiplus.h>
#include <winrt/base.h> // Required for winrt::init_apartment

#pragma comment (lib,"Gdiplus.lib")

// Global state for our application
AppState app_state;
std::thread ocr_thread;

// All windows
static std::vector<WindowInfo> available_windows;

static std::unordered_set<std::string> global_dictionary;

// --- DirectX Boilerplate Data ---
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// --- Forward declarations of helper functions ---
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

void start_ocr_thread(AppState& state, const std::unordered_set<std::string>& dictionary) {

    // Save the config and set the running flag.
    save_config(state.config);
    state.is_running = true;
    // Don't reset last_copied_code, so we don't re-process a bad code
    state.debug_msg = "";

    {
        std::lock_guard<std::mutex> lock(state.mtx);
        state.status_text = "Starting OCR...";
    }

    // Start the new thread
    ocr_thread = std::thread(run_ocr_process, std::ref(state), state.config, std::ref(dictionary));
}

// --- Main Application Entry Point ---
int main(int, char**)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    app_state.config = load_config();

    available_windows = get_all_visible_windows();

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("MFFU Code Grabber"), NULL };
    ::RegisterClassEx(&wc);
    HWND hwnd = ::CreateWindow(wc.lpszClassName, _T("MFFU Code Grabber UI"), WS_OVERLAPPEDWINDOW, 100, 100, 600, 450, NULL, NULL, wc.hInstance, NULL);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    global_dictionary = load_dictionary("dictionary.txt");
    if (global_dictionary.empty()) {
        MessageBoxA(NULL, "dictionary.txt not found or is empty.\nThe application will now close.", "Critical Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::cout << "Successfully loaded " << global_dictionary.size() << " words from dictionary." << std::endl;

    // --- Main UI Loop ---
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::Begin("MainPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Window Selection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Button("Refresh Window List")) {
                available_windows = get_all_visible_windows();
            }

            const char* current_selection_label = "(None selected)";
            for (const auto& window : available_windows) {
                if (window.handle == app_state.target_hwnd) {
                    current_selection_label = window.title.c_str();
                    break;
                }
            }

            if (ImGui::BeginCombo("Select Window to Attach", current_selection_label))
            {
                for (const auto& window : available_windows)
                {
                    bool is_selected = (window.handle == app_state.target_hwnd);
                    // When an item is selected, store its HWND
                    if (ImGui::Selectable(window.title.c_str(), is_selected))
                    {
                        app_state.target_hwnd = window.handle;
                    }
                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Account Configuration", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::InputText("Device Session ID", app_state.config.deviceSessionId, 256);
            ImGui::InputText("Client Session ID", app_state.config.clientSessionId, 256);
            ImGui::Text("Cookie");
            ImGui::InputTextMultiline("##CookieInput", app_state.config.cookie, 8192, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4), ImGuiInputTextFlags_NoHorizontalScroll);
            ImGui::InputText("Account ID", app_state.config.accountId, 256);
            ImGui::InputInt("Plan ID", &app_state.config.planId);
            ImGui::InputInt("Method ID", &app_state.config.method_id);
            ImGui::InputText("CVV", app_state.config.cvv, 3);
            ImGui::Checkbox("Reset Enabled", &app_state.config.resetEnabled);

            ImGui::Text("Platform Selection:");
            if (ImGui::RadioButton("Tradovate", &app_state.config.platformSelection, 0)) {
                // User just clicked Tradovate. Set the associated values.
                strncpy_s(app_state.config.selectedBrokerage, "Tradovate", 63);
                strncpy_s(app_state.config.selectedPlatform, "Tradovate", 63);
            }
            ImGui::SameLine(); // Puts the next widget on the same line
            if (ImGui::RadioButton("DXFeed", &app_state.config.platformSelection, 1)) {
                // User just clicked DXFeed. Set the associated values.
                strncpy_s(app_state.config.selectedBrokerage, "DXFeed", 63);
                strncpy_s(app_state.config.selectedPlatform, "Quantower", 63);
            }

            ImGui::Text("Auto-Set Values:");
            ImGui::InputText("Selected Brokerage", app_state.config.selectedBrokerage, 64, ImGuiInputTextFlags_ReadOnly);
            ImGui::InputText("Selected Platform", app_state.config.selectedPlatform, 64, ImGuiInputTextFlags_ReadOnly);
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Capture Configuration", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::InputFloat("Upscale Factor", &app_state.config.upscale_factor, 1.0f, 1.0f, "%.2f");
            ImGui::InputInt("Offset X", &app_state.config.offsetX);
            ImGui::InputInt("Offset Y", &app_state.config.offsetY);
            ImGui::InputInt("Width", &app_state.config.width);
            ImGui::InputInt("Height", &app_state.config.height);
            ImGui::InputFloat("Crop Percentage", &app_state.config.cropPercentage, 0.5f, 0.5f, "%.2f");
            ImGui::InputText("Regex Pattern", app_state.config.regex, 256); // <--- FIX 2: Missing semicolon
            ImGui::InputText("Validate Regex Pattern", app_state.config.validateRegex, 256);
        }

        if (ImGui::Button("Save Config")) {
            std::lock_guard<std::mutex> lock(app_state.mtx);
            save_config(app_state.config);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Config")) {
            std::lock_guard<std::mutex> lock(app_state.mtx);
            app_state.config = load_config();
        }


        ImGui::Separator();

        ImGui::Checkbox("Debug Mode", &app_state.config.debugMode);

        ImGui::SameLine();

        ImGui::Checkbox("Account Enabled", &app_state.config.accountEnabled);

        ImGui::Text("Controls");

        if (!app_state.is_running && ocr_thread.joinable()) {
            ocr_thread.join();

            if (app_state.needs_restart.load()) {
                app_state.needs_restart = false;
                app_state.status_text = "API indicated invalid code. Auto-restarting in 60 seconds...";

                app_state.is_waiting_for_restart = true;
                app_state.restart_wait_start_time = std::chrono::steady_clock::now();
            }
        }

        if (app_state.is_waiting_for_restart.load()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - app_state.restart_wait_start_time).count();

            if (elapsed >= app_state.restart_wait_seconds) {
                // Time is up! Reset the waiting flag and start the new thread.
                app_state.is_waiting_for_restart = false;
                start_ocr_thread(app_state, global_dictionary);
            }
        }

        if (app_state.is_running.load()) {
            // --- STOP BUTTON ---
            if (ImGui::Button("Stop OCR Process")) {
                app_state.is_running = false; // Signal the thread to stop
                // The block above will join it on the next UI frame.
                app_state.needs_restart = false;
                app_state.is_waiting_for_restart = false;
            }
        }
        else {
            // --- START BUTTON ---
            bool should_disable_start = app_state.is_running.load() || app_state.is_waiting_for_restart.load();
            if (should_disable_start) {
                ImGui::BeginDisabled();
            }

            // 1. Check if a window has been selected.
            if (app_state.target_hwnd.load() == NULL) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Please select a window to attach to first!");
            }
            else
            {
                // 2. If a window is selected, show the Start button.
                if (ImGui::Button("Start OCR Process")) {
                    app_state.last_copied_code = "INIT";
                    start_ocr_thread(app_state, global_dictionary);

                }
            }

            if (should_disable_start) {
                ImGui::EndDisabled();
            }
        }

        ImGui::SameLine();

        ImGui::Separator();

        ImGui::Text("Status");
        ImGui::BeginChild("StatusRegion", ImVec2(0, 0), true);
        {
            std::lock_guard<std::mutex> lock(app_state.mtx);

            if (app_state.is_waiting_for_restart.load()) {
                // If waiting, show a countdown.
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - app_state.restart_wait_start_time).count();
                long long wait_duration = 60;
                long long remaining = wait_duration - elapsed;
                if (remaining < 0) remaining = 0;

                app_state.status_text = "API Error. Restarting in " + std::to_string(remaining) + " seconds...";
            }

            ImGui::Text("Current Status: %s", app_state.status_text.c_str());
            ImGui::Text("Last Found Code: %s", app_state.last_found_code.c_str());
            ImGui::Text("Last API Status: %s", app_state.api_status.c_str()); // <-- DISPLAY API STATUS
        }
        ImGui::EndChild();

        ImGui::End();
        // --- END OF UI DEFINITION ---

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.1f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0); // Present with vsync
    }

    // --- Cleanup ---
    if (ocr_thread.joinable()) {
        app_state.is_running = false; // Signal thread to stop
        ocr_thread.join(); // Wait for it to finish
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);
    Gdiplus::GdiplusShutdown(gdiplusToken);

    return 0;
}

// --- BOILERPLATE WIN32 AND DIRECTX IMPLEMENTATIONS ---
// (You can collapse these in your IDE, you don't need to edit them)

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}