#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>
#include <vector>
#include <windows.h> 
#include <unordered_set>
#include "ConfigManager.h"
#include "OCRProcessor.h"

struct WindowInfo {
    std::string title;
    HWND handle;
};

// --- Function Declarations ---
std::vector<WindowInfo> get_all_visible_windows();
std::string fix_ocr_suffix_errors(const std::string& input_string);
void to_clipboard(const std::string& s);

int make_api_call(
    const std::string& code,
    bool is_reset,
    const std::string& account_type,
    const std::string& account_size,
    const Config& config, 
    AppState& state,
    bool is_flex
);

std::unordered_set<std::string> load_dictionary(const std::string& filename);

#endif // UTILITIES_H