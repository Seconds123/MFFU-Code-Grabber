cmake_minimum_required(VERSION 3.10)
project(MFFU_Grabber)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find necessary packages
find_package(OpenCV REQUIRED)
find_package(Tesseract REQUIRED)
find_package(cpr REQUIRED)
find_package(imgui REQUIRED) # This will now work because of vcpkg
find_package(nlohmann_json REQUIRED)
find_package(cppwinrt REQUIRED)

message(STATUS "Tesseract libraries found: ${Tesseract_LIBRARIES}")
message(STATUS "Tesseract include directories: ${Tesseract_INCLUDE_DIRS}")


# Add source files
add_executable(MFFU_Grabber 
    main.cpp 
    GraphicsCapture.cpp 
    OCRProcessor.cpp 
    ConfigManager.cpp 
    utilities.cpp)

# --- Link Libraries (The New, Forceful Method) ---

# First, add the include directories so the compiler finds the headers
target_include_directories(MFFU_Grabber
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${Tesseract_INCLUDE_DIRS}
)

# Now, link the actual library files using their full paths.
# This bypasses any issues with linker search paths.
target_link_libraries(MFFU_Grabber
    PRIVATE
    # For OpenCV
    ${OpenCV_LIBS}

    # For Tesseract (THE MAIN FIX)
    # Use the 'debug' keyword for the debug library
    debug E:/Dev/vcpkg/installed/x64-windows/debug/lib/tesseract55d.lib
    # Use the 'optimized' keyword for the release (non-debug) library
    optimized E:/Dev/vcpkg/installed/x64-windows/lib/tesseract55.lib


    # For other libraries
    cpr::cpr
    nlohmann_json::nlohmann_json
    imgui::imgui
    gdi32.lib
    gdiplus.lib
    user32.lib
    d3d11.lib
    dxgi.lib
    dwmapi.lib
    windowsapp.lib
)