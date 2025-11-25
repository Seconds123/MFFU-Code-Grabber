#include "utilities.h"
#include <iostream>
#include <string>
#include <map>
#include <regex>
#include <windows.h> 
#include <cpr/cpr.h> 
#include <nlohmann/json.hpp> 
#include <utility>

using json = nlohmann::json;

std::string fix_ocr_suffix_errors(const std::string& input_string) {
    if (input_string.length() < 2) {
        return input_string;
    }

    const static std::vector<std::pair<std::string, std::string>> suffix_replacements = {
        {"AOV2", "40V2"},

        {"10O", "10"}, {"50O", "50"}, {"S0O", "50"}, {"S0S", "50"},
        {"501", "50"}, {"257", "25"}, {"SO2", "50"}, {"S07", "50"},
        {"SOA", "50"}, {"S5H", "55"}, {"OS7", "50"}, {"207", "20"},

        {"5O", "50"}, {"SO", "50"}, {"S0", "50"}, {"IS", "15"},
        {"OO", "00"}, {"S3", "53"}, {"S2", "52"}, {"S4", "54"},
        {"3D", "30"}, {"3C", "30"}, {"AO", "40"}, {"A0", "40"}
    };

    std::string result = input_string;

    for (const auto& rule : suffix_replacements) {
        const std::string& error_pattern = rule.first;
        const std::string& correction = rule.second;

        if (result.length() >= error_pattern.length() &&
            result.substr(result.length() - error_pattern.length()) == error_pattern) {

            result.replace(result.length() - error_pattern.length(), error_pattern.length(), correction);

            break;
        }
    }

    return result;
}

void to_clipboard(const std::string& s) {
    OpenClipboard(nullptr);
    EmptyClipboard();
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
    if (!hg) {
        CloseClipboard();
        return;
    }
    memcpy(GlobalLock(hg), s.c_str(), s.size() + 1);
    GlobalUnlock(hg);
    SetClipboardData(CF_TEXT, hg);
    CloseClipboard();
    GlobalFree(hg);
}

int make_api_call(const std::string& code, bool is_reset, const std::string& account_type, const std::string& account_size, const Config& config, AppState& state, bool is_flex) {
    if (code.length() < 4) {
        std::cout << "--Code is less than 4 chars, stopping API" << std::endl;
        std::lock_guard<std::mutex> lock(state.mtx);
        state.api_status = "Error: Code too short.";
        return 1;
    }

    cpr::Header headers = {
        {"cookie", config.cookie },
        {"content-type", "application/json"},
        {"origin", "https://myfundedfutures.com"},
        {"sec-ch-ua-mobile", "?0"},
        {"sec-ch-ua-platform", "Windows"},
        {"sec-fetch-dest", "empty"},
        {"sec-fetch-mode", "cors"},
        {"sec-ch-ua", config.sec_ch_ua },
        {"user-agent", config.user_agent }
    };

    int plan_id = 59; 

    if (account_type == "CORE") {
        plan_id = 59;

        if (is_flex) {
            plan_id = 63;
        }
    }
    else if (account_type == "SCALE") {
        if (account_size == "50K") plan_id = 58;
        else if (account_size == "100K") plan_id = 57;
        else if (account_size == "150K") plan_id = 56;
    }
    else if (account_type == "PRO") {
        if (account_size == "50K") plan_id = 48;
        else if (account_size == "100K") plan_id = 49;
        else if (account_size == "150K") plan_id = 50;
    }

    std::cout << "--- Step 1: Creating Checkout Session ---" << std::endl;

    json create_session_payload;
    create_session_payload["client_session_key"] = config.clientSessionId;
    create_session_payload["couponCode"] = code;
    create_session_payload["isActivation"] = false;
    create_session_payload["isSimFundedReset"] = false;
    create_session_payload["selectedBrokerage"] = config.selectedBrokerage;
    create_session_payload["selectedPlatform"] = config.selectedPlatform;
    create_session_payload["static_processor_id"] = 1; 

    if (is_reset && config.resetEnabled) {
        create_session_payload["isReset"] = true;
        create_session_payload["originalAccountId"] = config.accountId;
        create_session_payload["plan_id"] = config.planId;
    }
    else if (!is_reset) {
        create_session_payload["isReset"] = false;
        create_session_payload["plan_id"] = plan_id;
    }
    else {
        std::cerr << "Error: Reset detected but is not enabled in config. Aborting." << std::endl;
        std::lock_guard<std::mutex> lock(state.mtx);
        state.api_status = "Aborted: Reset not enabled.";
        return 2;
    }

    if (!config.accountEnabled) {
        std::cerr << "Error: Account code detected but is not enabled in config. Aborting." << std::endl;
        std::lock_guard<std::mutex> lock(state.mtx);
        state.api_status = "Aborted: Account not enabled.";
        return 2;
    }

    cpr::Response create_session_response = cpr::Post(cpr::Url{ "https://api.myfundedfutures.com/api/createCheckoutSession/" },
        cpr::Body{ create_session_payload.dump() },
        headers);

    if (create_session_response.status_code != 200) {
        std::cerr << "Error in Step 1: Failed to create checkout session. Status: " << create_session_response.status_code << std::endl;
        std::cerr << "Response: " << create_session_response.text << std::endl;
        return 4;
    }

    std::string public_id;

    try {
        json session_data = json::parse(create_session_response.text);
        public_id = session_data["ok"]["public_id"];
        std::cout << "Successfully created session. Public ID: " << public_id << std::endl;
    }
    catch (const json::exception& e) {
        std::cerr << "Error in Step 1: Failed to parse JSON or find public_id. " << e.what() << std::endl;
        std::cerr << "Response: " << create_session_response.text << std::endl;
        std::lock_guard<std::mutex> lock(state.mtx);
        state.api_status = "Failed to parse checkout session ID from the API";
        return 4;
    }

    std::cout << "\n--- Step 2: Initiating Payment with Session ID ---" << std::endl;
    std::cout << "\n-- Session ID: " << public_id << "--" << std::endl;


    json payload;
    payload["checkout_session_id"] = public_id;
    payload["client_session_key"] = config.clientSessionId;
    payload["colorDepth"] = 24;
    payload["screenHeight"] = 1080;
    payload["screenWidth"] = 1920;
    payload["timezone"] = -330;
    payload["affiliateCampaign"] = "";
    payload["affiliateId"] = "";
    payload["affiliateSource"] = "";
    payload["competitionId"] = nullptr;
    payload["couponCode"] = code;
    payload["deviceSessionId"] = config.deviceSessionId;
    payload["isActivation"] = false;
    payload["isMarketData"] = false;
    payload["isSimFundedReset"] = false;
    payload["method_id"] = config.method_id;
    payload["static_processor_id"] = 1;
    payload["marketDataProductId"] = nullptr;
    payload["tradingAccountUserId"] = nullptr;
    payload["cvv"] = std::stoi(config.cvv);
    payload["selectedBrokerage"] = config.selectedBrokerage;
    payload["selectedPlatform"] = config.selectedPlatform;

    if (is_reset && config.resetEnabled) {
        payload["isReset"] = true;
        payload["originalAccountId"] = config.accountId;
        payload["plan_id"] = config.planId;
    }

    else if (!is_reset) {
        payload["isReset"] = false;
        payload["originalAccountId"] = nullptr;
        payload["plan_id"] = plan_id;
    }
    else {
        std::cout << "Reset detected but not allowed. No API call made." << std::endl;
        std::lock_guard<std::mutex> lock(state.mtx);
        return 3;
    }


    cpr::Response r = cpr::Post(cpr::Url{ "https://api.myfundedfutures.com/api/initiatePayment/" },
        cpr::Body{ payload.dump() },
        headers);

    std::cout << "Final Status Code: " << r.status_code << std::endl;
    std::cout << "--------------------" << std::endl;
    std::cout << "JSON Response Body:" << std::endl;

    try {
        std::cout << json::parse(r.text).dump(2) << std::endl;
    }
    catch (const json::exception&) {
        std::cout << "--- (Response was not valid JSON) --- \n" << r.text << std::endl;
    }


    {
        std::lock_guard<std::mutex> lock(state.mtx);
        state.api_status = "API Call Finished. Status: " + std::to_string(r.status_code);
    }

    return r.status_code; 
}

BOOL CALLBACK EnumWindowsProcPopulate(HWND hwnd, LPARAM lParam) {
    auto* windows = reinterpret_cast<std::vector<WindowInfo>*>(lParam);

    const int title_size = 256;
    char window_title[title_size];
    GetWindowTextA(hwnd, window_title, title_size);
    std::string title_str(window_title);

    if (IsWindowVisible(hwnd) && !title_str.empty()) {
        // --- FIX #5: Initialize in the correct order ---
        windows->push_back({ title_str, hwnd });
    }

    return TRUE;
}

std::vector<WindowInfo> get_all_visible_windows() {
    std::vector<WindowInfo> windows;
    EnumWindows(EnumWindowsProcPopulate, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

std::unordered_set<std::string> load_dictionary(const std::string& filename) {
    std::unordered_set<std::string> dictionary_set;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Error: Could not open dictionary file '" << filename << "'." << std::endl;
        return dictionary_set;
    }
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        line_number++;

        const char* whitespace = " \t\n\r\f\v";
        line.erase(0, line.find_first_not_of(whitespace)); 
        line.erase(line.find_last_not_of(whitespace) + 1); 

        std::transform(line.begin(), line.end(), line.begin(), ::toupper);

        if (!line.empty()) {
            dictionary_set.insert(line);
        }
    }

    file.close();

    std::cout << "--- DICTIONARY LOAD COMPLETE ---" << std::endl;

    return dictionary_set;
}