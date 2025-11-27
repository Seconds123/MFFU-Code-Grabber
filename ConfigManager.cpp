#include "ConfigManager.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void save_config(const Config& config) {
    json j;
    j["upscale_factor"] = config.upscale_factor;
    j["deviceSessionId"] = config.deviceSessionId;
    j["cookie"] = config.cookie;
    j["accountId"] = config.accountId;
    j["resetEnabled"] = config.resetEnabled;
    j["accountEnabled"] = config.accountEnabled;
    j["planId"] = config.planId;
    j["offsetX"] = config.offsetX;
    j["offsetY"] = config.offsetY;
    j["width"] = config.width;
    j["height"] = config.height;
    j["cropPercentage"] = config.cropPercentage;
    j["regex"] = config.regex;
    j["validateRegex"] = config.validateRegex;
    j["method_id"] = config.method_id;
    j["cvv"] = config.cvv;
    j["user_agent"] = config.user_agent;
    j["sec_ch_ua"] = config.sec_ch_ua;
    j["selectedBrokerage"] = config.selectedBrokerage;
    j["selectedPlatform"] = config.selectedPlatform;
    j["platformSelection"] = config.platformSelection;
    j["clientSessionId"] = config.clientSessionId;
    j["debugMode"] = config.debugMode;

    std::ofstream file("config.json");
    file << j.dump(4);
}

Config load_config() {
    Config config;
    try {
        std::ifstream file("config.json");
        if (file.is_open()) {
            json j;
            file >> j;
            // --- FIX: Loading ALL config values now ---
            config.upscale_factor = j.value("upscale_factor", 3.0f);
            strncpy_s(config.deviceSessionId, j.value("deviceSessionId", "").c_str(), 255);
            strncpy_s(config.cookie, j.value("cookie", "").c_str(), 8192);
            strncpy_s(config.accountId, j.value("accountId", "").c_str(), 255);
            strncpy_s(config.clientSessionId, j.value("clientSessionId", "").c_str(), 255);
            config.resetEnabled = j.value("resetEnabled", false);
            config.accountEnabled = j.value("accountEnabled", true);
            config.debugMode = j.value("debugMode", false);
            config.planId = j.value("planId", 0);
            config.offsetX = j.value("offsetX", 50);
            config.offsetY = j.value("offsetY", 790);
            config.width = j.value("width", 1800);
            config.height = j.value("height", 100);
            config.cropPercentage = j.value("cropPercentage", 0.75f);
            config.method_id = j.value("method_id", 0);
            strncpy_s(config.cvv, j.value("cvv", "").c_str(), 4);
            config.platformSelection = j.value("platformSelection", 0);
            strncpy_s(config.selectedBrokerage, j.value("selectedBrokerage", "Tradovate").c_str(), 63);
            strncpy_s(config.selectedPlatform, j.value("selectedPlatform", "Tradovate").c_str(), 63);
            strncpy_s(config.regex, j.value("regex", "^[A-Z0-9?:_]+$").c_str(), 255);
            strncpy_s(config.validateRegex, j.value("validateRegex", R"(?:(?:USE\s*)?CODE|RESETS|ACCOUNT)[\s:-]*([A-Z_]+[0-9]+)|^([A-Z_]+[0-9]+)(?=-\d)").c_str(), 255);
            strncpy_s(config.sec_ch_ua, j.value("sec_ch_ua", "\"Google Chrome\";v=\"140\", \"Chromium\";v=\"140\", \"Not/A)Brand\";v=\"24\"").c_str(), 255);
            strncpy_s(config.user_agent, j.value("user_agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36").c_str(), 255);
        }
    }
    catch (...) {
        // On any error, return a default config
    }
    return config;
}