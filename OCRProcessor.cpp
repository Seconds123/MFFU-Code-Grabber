#include "OCRProcessor.h"
#include "GraphicsCapture.h"

// All necessary includes
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <regex>
#include <algorithm>
#include <unordered_set>
#include <fstream>
#include <windows.h>
#include <gdiplus.h>
#include <opencv2/opencv.hpp>
//#include <opencv2/imgproc.hpp>*/
#include <tesseract/baseapi.h>
#include "utilities.h"
#include "string_utils.h"

#pragma comment (lib,"Gdiplus.lib")

bool containsTwoUnderscores(const std::string& str) {
    int underscoreCount = 0;
    for (char c : str) {
        if (c == '_') {
            underscoreCount++;
        }
    }
    return underscoreCount >= 2;
}

void run_ocr_process(AppState& state, const Config& config, const std::unordered_set<std::string>& dictionary) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    GraphicsCapture capture;

    tesseract::TessBaseAPI* ocr = new tesseract::TessBaseAPI();
    if (ocr->Init("./tessdata", "eng", tesseract::OEM_LSTM_ONLY)) {
        std::lock_guard<std::mutex> lock(state.mtx);
        state.status_text = "Error: Tesseract init failed. Check tessdata folder.";
        state.is_running = false;
        return;
    }
    ocr->SetPageSegMode(tesseract::PSM_SINGLE_LINE);
    ocr->SetVariable("tessedit_char_whitelist", "-_:abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

    HWND hwnd_to_capture = state.target_hwnd.load();

    if (!capture.StartCapture(hwnd_to_capture)) {
        std::lock_guard<std::mutex> lock(state.mtx);
        state.status_text = "Error: Failed to start Graphics Capture on this window.";
        state.is_running = false;
        ocr->End();
        delete ocr;
        return;
    }


    // --- MAIN OCR LOOP ---
    while (state.is_running) {


        cv::Mat frame_bgra = capture.GetLatestFrame();
        if (frame_bgra.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        cv::Rect desired_roi(config.offsetX, config.offsetY, config.width, config.height);
        cv::Rect full_image_bounds(0, 0, frame_bgra.cols, frame_bgra.rows);
        cv::Rect clamped_roi = desired_roi & full_image_bounds;

        if (clamped_roi.width <= 0 || clamped_roi.height <= 0) {
            {
                std::lock_guard<std::mutex> lock(state.mtx);
                state.status_text = "Warning: ROI is outside the window bounds.";
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        cv::Mat roi_frame = frame_bgra(clamped_roi);

        int horizontal_cropped_width = static_cast<int>(roi_frame.cols * config.cropPercentage);
        if (horizontal_cropped_width > 0 && horizontal_cropped_width <= roi_frame.cols) {
            roi_frame = roi_frame(cv::Rect(0, 0, horizontal_cropped_width, roi_frame.rows));
        }

        cv::Mat gray;
        cv::cvtColor(roi_frame, gray, cv::COLOR_BGRA2GRAY);

        cv::Mat binary_img;
        cv::threshold(gray, binary_img, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

        cv::Mat final_img_for_ocr;
        cv::bitwise_not(binary_img, final_img_for_ocr);

        const int OPTIMAL_CHAR_HEIGHT = 40;
        int current_height = final_img_for_ocr.rows;
        if (current_height > 0 && current_height < OPTIMAL_CHAR_HEIGHT) {
            float scale = static_cast<float>(OPTIMAL_CHAR_HEIGHT) / current_height;
            cv::resize(final_img_for_ocr, final_img_for_ocr, cv::Size(), scale, scale, cv::INTER_CUBIC);
        }

        if (config.debugMode) {
            cv::imwrite("debug_01_binary.png", binary_img);
            cv::imwrite("debug_03_final_for_ocr.png", final_img_for_ocr);
        }


        // STEP 4: RUN OCR
        ocr->SetImage(final_img_for_ocr.data,
            static_cast<int>(final_img_for_ocr.cols),
            static_cast<int>(final_img_for_ocr.rows),
            1,
            static_cast<int>(final_img_for_ocr.step));
        std::string raw_ocr_text = std::string(ocr->GetUTF8Text());



        raw_ocr_text.erase(std::remove(raw_ocr_text.begin(), raw_ocr_text.end(), '\n'), raw_ocr_text.end());
        raw_ocr_text.erase(std::remove(raw_ocr_text.begin(), raw_ocr_text.end(), ' '), raw_ocr_text.end());
        std::transform(raw_ocr_text.begin(), raw_ocr_text.end(), raw_ocr_text.begin(), ::toupper);

        if (!raw_ocr_text.empty()) {
            bool is_reset = false;
            bool is_flex = false;
            std::string account_type = "CORE";
            std::string account_size = "50K";

            if (raw_ocr_text.find("RESET") != std::string::npos) is_reset = true;
            if (raw_ocr_text.find("CORE") != std::string::npos) account_type = "CORE";
            if (raw_ocr_text.find("SCALE") != std::string::npos) account_type = "SCALE";
            if (raw_ocr_text.find("PRO") != std::string::npos) account_type = "PRO";

            if (raw_ocr_text.find("FLEX") != std::string::npos) is_flex = true;

            std::cout << config.regex << std::endl;
            std::cout << raw_ocr_text << std::endl;

            std::regex code_regex(config.regex, std::regex::icase);
            std::smatch match;

            if (std::regex_search(raw_ocr_text, match, code_regex) && match.size() > 1) {

                std::string final_code = "";

                if (match[1].matched) {
                    final_code = match[1].str();
                }

                if (match[2].matched) {
                    final_code = match[2].str();
                }

                
                std::cout << "-> Extracted Raw: '" << final_code << "'" << std::flush;

                std::regex validate_regex(config.validateRegex, std::regex::icase);
                if (std::regex_match(final_code, validate_regex)) {
                    if (containsTwoUnderscores(final_code)) {
                        std::lock_guard<std::mutex> lock(state.mtx);
                        state.last_copied_code = final_code;
                        state.api_status = "Code contains two underscore, API call skipped.";
                        continue; 
                    }

                    size_t underscore_pos = final_code.find('_');

                    if (underscore_pos != std::string::npos) {

                        struct MatchResult {
                            std::string keyword = "";
                            int distance = 1000;
                            size_t index = 0;
                        };

                        MatchResult best_match;

                        for (const auto& keyword : dictionary) {
                            if (keyword.empty() || keyword.length() < 3 || keyword.length() > final_code.length()) {
                                continue;
                            }

                            for (size_t i = 0; i <= final_code.length() - keyword.length(); ++i) {
                                std::string ocr_substring = final_code.substr(i, keyword.length());

                                if (ocr_substring.find('_') == std::string::npos) {
                                    continue;
                                }

                                int current_distance = levenshtein_distance(keyword, ocr_substring);

                                if (current_distance < best_match.distance) {
                                    best_match.distance = current_distance;
                                    best_match.keyword = keyword;
                                    best_match.index = i;
                                }
                                else if (current_distance == best_match.distance) {
                                    if (keyword.length() > best_match.keyword.length()) {
                                        best_match.keyword = keyword;
                                        best_match.index = i;
                                    }
                                }
                            }
                        }

                        const int DISTANCE_THRESHOLD = 3;
                        if (!best_match.keyword.empty() && best_match.distance <= DISTANCE_THRESHOLD) {
                            std::cout << "\n[DEBUG] Anchor Found. Best match: '" << best_match.keyword << "' (distance: " << best_match.distance << "). Reconstructing." << std::endl;

                            std::string prefix = final_code.substr(0, best_match.index);
                            std::string suffix = final_code.substr(best_match.index + best_match.keyword.length());
                            final_code = prefix + best_match.keyword + suffix;
                        }
                        else {
                            std::cout << "\n[DEBUG] Anchor Found, but no high-quality match nearby. Best was '" << best_match.keyword << "' with distance " << best_match.distance << ". Keeping original." << std::endl;
                        }

                    }

                    final_code = fix_ocr_suffix_errors(final_code);

                    if (containsTwoUnderscores(final_code))
                    {
                        std::lock_guard<std::mutex> lock(state.mtx);
                        state.last_copied_code = final_code;
                        state.api_status = "Code contains two underscore, API call skipped.";
                        continue; 
                    }


                    bool already_processed = false;
                    {
                        std::lock_guard<std::mutex> lock(state.mtx);
                        if (final_code == state.last_copied_code) {
                            already_processed = true;
                        }
                    }


                    if (!already_processed) {
                        if (final_code.size() < 5) {
                            std::lock_guard<std::mutex> lock(state.mtx);
                            state.last_copied_code = final_code;
                            state.api_status = "Code < 5 chars, API call skipped.";
                            continue; 
                        }

                        to_clipboard(final_code);
                        std::cout << "\n>>> IS RESET: " << is_reset << std::endl;
                        std::cout << "\n>>> ACC TYPE: " << account_type << std::endl;
                        std::cout << "\n>>> ACC SIZE: " << account_size << std::endl;
                        std::cout << "\n>>> CODE FOUND & COPIED: " << final_code << " <<<" << std::endl;

                        int api_status_code = 0;

                        if (!config.debugMode) {
                            api_status_code = make_api_call(final_code, is_reset, account_type, account_size, config, state, is_flex);
                        }
                        else {
                            std::lock_guard<std::mutex> lock(state.mtx);
                            state.debug_msg = "Debug mode on, so not calling API.";
                            state.api_status = "Skipped (Debug Mode)";
                        }

                        if (api_status_code == 1 || api_status_code == 2 || api_status_code == 3 || api_status_code == 4 || api_status_code == 422) {
                            state.needs_restart = true;
                            state.restart_wait_seconds = 5;
                        }
                        else if (api_status_code == 302)
                        {
                            state.needs_restart = true;
                            state.restart_wait_seconds = 5;
                        }

                        {
                            std::lock_guard<std::mutex> lock(state.mtx);
                            state.last_copied_code = final_code;
                            state.last_found_code = final_code;
                        }

                        state.is_running = false; 
                    }
                }
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(state.mtx);
            state.status_text = "Scanning...";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // --- CLEANUP ---
    capture.StopCapture();
    ocr->End();
    delete ocr;
    {
        std::lock_guard<std::mutex> lock(state.mtx);
        state.status_text = "Stopped.";
    }
}
