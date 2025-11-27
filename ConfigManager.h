#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>

struct Config {
    // UI Configurable Settings
    bool debugMode = false;
    float upscale_factor = 5.0f;
    char deviceSessionId[256] = "";
    char clientSessionId[256] = "";
    char cookie[8192] = "";
    char accountId[256] = "";
    bool resetEnabled = false;
    bool accountEnabled = true;
    int planId = 0;
    int offsetX = 50;
    int offsetY = 790;
    int width = 1800;
    int height = 100;
    float cropPercentage = 0.75f;
    char regex[256] = "(?:USE\\s+)?CODE:?\\s*([A-Z0-9_]+)(?=\\s*(?:FOR|FREE|RESETS|X50|GET|BONUS)|$)";
    char validateRegex[256] = "^[A-Z0-9?:_]+$";

    int platformSelection = 0;
    char selectedBrokerage[64] = "Tradovate";
    char selectedPlatform[64] = "Tradovate";

    int method_id = 0;
    char cvv[4] = "";

    char sec_ch_ua[256] = "\"Google Chrome\";v=\"140\", \"Chromium\";v=\"140\", \"Not/A)Brand\";v=\"24\"";
    char user_agent[256] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";
};

void save_config(const Config& config);
Config load_config();

#endif // CONFIG_MANAGER_H