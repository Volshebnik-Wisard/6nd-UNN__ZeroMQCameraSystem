// config_tester.cpp
#include <iostream>
#include <iomanip>
#include "video_addresses.h"

void print_config() {
    std::cout << "=== CONFIGURATION TESTER ===" << std::endl;

    // Сетевые адреса
    std::cout << "\n--- NETWORK ADDRESSES ---" << std::endl;
    std::cout << "Capturer bind addresses:" << std::endl;
    for (const auto& addr : capturer_bind_addresses) {
        std::cout << "  - " << addr << std::endl;
    }

    std::cout << "Worker to Capturer connect addresses:" << std::endl;
    for (const auto& addr : worker_to_capturer_connect_addresses) {
        std::cout << "  - " << addr << std::endl;
    }

    std::cout << "Worker to Composer connect addresses:" << std::endl;
    for (const auto& addr : worker_to_composer_connect_addresses) {
        std::cout << "  - " << addr << std::endl;
    }

    std::cout << "Composer bind addresses:" << std::endl;
    for (const auto& addr : composer_bind_addresses) {
        std::cout << "  - " << addr << std::endl;
    }

    // Настройки Capturer
    std::cout << "\n--- CAPTURER SETTINGS ---" << std::endl;
    std::cout << "Queue size: " << queue_size << std::endl;
    std::cout << "Frame width: " << cap_frame_width << std::endl;
    std::cout << "Frame height: " << cap_frame_height << std::endl;
    std::cout << "FPS: " << cap_fps << std::endl;
    std::cout << "JPEG quality: " << cap_quality << std::endl;

    // Настройки Worker
    std::cout << "\n--- WORKER SETTINGS ---" << std::endl;
    std::cout << "Canny low threshold: " << effect_canny_low_threshold << std::endl;
    std::cout << "Canny high threshold: " << effect_canny_high_threshold << std::endl;
    std::cout << "Gaussian kernel size: " << effect_gaussian_kernel_size << std::endl;
    std::cout << "Dilation kernel size: " << effect_dilation_kernel_size << std::endl;
    std::cout << "Color quantization levels: " << effect_color_quantization_levels << std::endl;
    std::cout << "Black contours: " << (effect_black_contours ? "true" : "false") << std::endl;

    // Настройки Composer
    std::cout << "\n--- COMPOSER SETTINGS ---" << std::endl;
    std::cout << "Frame gap: " << frame_gap << std::endl;
    std::cout << "Buffer size: " << buffer_size << std::endl;

    // Форматы данных
    std::cout << "\n--- DATA FORMATS ---" << std::endl;
    std::cout << "Pixel format: ";
    switch (proto_pixel_format) {
    case video_processing::BGR: std::cout << "BGR"; break;
    case video_processing::RGB: std::cout << "RGB"; break;
    case video_processing::GRAY: std::cout << "GRAY"; break;
    default: std::cout << "UNKNOWN"; break;
    }
    std::cout << std::endl;

    std::cout << "Image encoding: ";
    switch (proto_image_encoding) {
    case video_processing::JPEG: std::cout << "JPEG"; break;
    case video_processing::PNG: std::cout << "PNG"; break;
    case video_processing::RAW: std::cout << "RAW"; break;
    default: std::cout << "UNKNOWN"; break;
    }
    std::cout << std::endl;

    std::cout << "\n=== CONFIGURATION TEST COMPLETE ===" << std::endl;
}

int main() {
    try {
        print_config();
        return 0;
    }
    catch (const std::exception& e) {
        std::cout << "- [FAIL] Configuration test error: " << e.what() << std::endl;
        return -1;
    }
}