// config_loader.h
#pragma once
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include "video_processing.pb.h"

class ConfigLoader {
private:
    std::map<std::string, std::string> config_map;

    std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        size_t end = str.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        return str.substr(start, end - start + 1);
    }

public:
    ConfigLoader(const std::string& filename = "config.txt") {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cout << "- [FAIL] Cannot open config file: " << filename << std::endl;
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = trim(line.substr(0, pos));
                std::string value = trim(line.substr(pos + 1));
                config_map[key] = value;
            }
        }

        std::cout << "- [ OK ] Loaded config from: " << filename << std::endl;
    }

    std::vector<std::string> get_string_array(const std::string& key,
        const std::vector<std::string>& default_value = {}) {
        if (config_map.find(key) != config_map.end()) {
            std::vector<std::string> result;
            std::string value = config_map[key];
            std::stringstream ss(value);
            std::string item;

            while (std::getline(ss, item, ',')) {
                result.push_back(trim(item));
            }
            return result;
        }
        return default_value;
    }

    int get_int(const std::string& key, int default_value = 0) {
        if (config_map.find(key) != config_map.end()) {
            try {
                return std::stoi(config_map[key]);
            }
            catch (...) {
                std::cout << "- [WARN] Invalid integer for key: " << key << std::endl;
            }
        }
        return default_value;
    }

    bool get_bool(const std::string& key, bool default_value = false) {
        if (config_map.find(key) != config_map.end()) {
            std::string value = config_map[key];
            std::transform(value.begin(), value.end(), value.begin(), ::tolower);
            return (value == "true" || value == "1" || value == "yes");
        }
        return default_value;
    }

    video_processing::PixelFormat get_pixel_format(const std::string& key,
        video_processing::PixelFormat default_value = video_processing::BGR) {
        if (config_map.find(key) != config_map.end()) {
            std::string value = config_map[key];
            std::transform(value.begin(), value.end(), value.begin(), ::toupper);

            if (value == "BGR") return video_processing::BGR;
            if (value == "RGB") return video_processing::RGB;
            if (value == "GRAY") return video_processing::GRAY;

            std::cout << "- [WARN] Unknown pixel format: " << value << ", using default" << std::endl;
        }
        return default_value;
    }

    video_processing::ImageEncoding get_image_encoding(const std::string& key,
        video_processing::ImageEncoding default_value = video_processing::JPEG) {
        if (config_map.find(key) != config_map.end()) {
            std::string value = config_map[key];
            std::transform(value.begin(), value.end(), value.begin(), ::toupper);

            if (value == "JPEG") return video_processing::JPEG;
            if (value == "PNG") return video_processing::PNG;
            if (value == "RAW") return video_processing::RAW;

            std::cout << "- [WARN] Unknown image encoding: " << value << ", using default" << std::endl;
        }
        return default_value;
    }
};