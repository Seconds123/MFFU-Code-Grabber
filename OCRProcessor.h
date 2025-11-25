#ifndef OCR_PROCESSOR_H
#define OCR_PROCESSOR_H

#include <string>
#include <atomic>
#include <mutex>
#include <windows.h>
#include <unordered_set>
#include <chrono>
#include "ConfigManager.h" 

struct AppState {
    Config config; 

    std::atomic<bool> is_running = false; 
    std::atomic<bool> needs_restart = false;

    std::atomic<bool> is_waiting_for_restart = false;
    std::chrono::steady_clock::time_point restart_wait_start_time; 
    long long restart_wait_seconds = 60;

    std::atomic<HWND> target_hwnd = NULL;

    std::string status_text = "Ready.";
    int status_code = 0;
    std::string last_found_code = "None";
    std::string debug_msg = "";
    std::string api_status = "N/A";

    std::string last_copied_code = "";
    
    std::mutex mtx; 
};

void run_ocr_process(AppState& state, const Config& config, const std::unordered_set<std::string>& dictionary);

#endif // OCR_PROCESSOR_H