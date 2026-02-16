#pragma once
#include <vector>
#include <string>
//#include "scanner_darkly_effect.hpp"
#include "video_processing.pb.h"
#include "./config_loader.h"

// Создаем экземпляр загрузчика конфигурации
ConfigLoader g_config("./config.txt");

// Инициализируем все переменные напрямую
const std::vector<std::string> capturer_bind_addresses =
g_config.get_string_array("capturer_bind_addresses", { "tcp://*:5555" });

const std::vector<std::string> worker_to_capturer_connect_addresses =
g_config.get_string_array("worker_to_capturer_connect_addresses", { "tcp://localhost:5555" });

const std::vector<std::string> worker_to_composer_connect_addresses =
g_config.get_string_array("worker_to_composer_connect_addresses", { "tcp://localhost:5556" });

const std::vector<std::string> composer_bind_addresses =
g_config.get_string_array("composer_bind_addresses", { "tcp://*:5556" });

// Настройки Capturer
int camera_id = g_config.get_int("camera_id", 0);  // <-- Добавлено: ID камеры
int queue_size = g_config.get_int("queue_size", 100);
int cap_frame_width = g_config.get_int("cap_frame_width", 640);
int cap_frame_height = g_config.get_int("cap_frame_height", 480);
int cap_fps = g_config.get_int("cap_fps", 30);
int cap_quality = g_config.get_int("cap_quality", 80);

// Настройки Worker
int effect_canny_low_threshold = g_config.get_int("effect_canny_low_threshold", 60);
int effect_canny_high_threshold = g_config.get_int("effect_canny_high_threshold", 160);
int effect_gaussian_kernel_size = g_config.get_int("effect_gaussian_kernel_size", 3);
int effect_dilation_kernel_size = g_config.get_int("effect_dilation_kernel_size", 0);
int effect_color_quantization_levels = g_config.get_int("effect_color_quantization_levels", 8);
bool effect_black_contours = g_config.get_bool("effect_black_contours", true);

// Настройки Composer
int frame_gap = g_config.get_int("frame_gap", 500);
int buffer_size = g_config.get_int("buffer_size", 2000);

// Форматы данных
video_processing::PixelFormat proto_pixel_format =
g_config.get_pixel_format("proto_pixel_format", video_processing::BGR);

video_processing::ImageEncoding proto_image_encoding =
g_config.get_image_encoding("proto_image_encoding", video_processing::JPEG);

// До конфига присваивал в video_addresses.h, но удалять жалко так что.

////---------- 0. Начальные условия (входные данные) для всех компонентов ----------
//// 
//// Адреса для Capturer (PUSH socket - привязка)
//const std::vector<std::string> capturer_bind_addresses = {
//	//"tcp://192.168.9.47:5555",  // 516 - стол - право
//	/*"tcp://192.168.9.62:5555",  // 516 - стол - лево
//	"tcp://192.168.9.68:5555",  // 516 - окно - право*/
//	//"tcp://192.168.9.50:5555",  // 528 - стол - право	
//	"tcp://localhost:5555",   // Локальный хост
//	"tcp://*:5555"            // Все интерфейсы
//};
//
//// Адреса для Worker (PULL socket - подключение к Capturer)
//const std::vector<std::string> worker_to_capturer_connect_addresses = {
//	/*"tcp://192.168.9.47:5555",  // 516 - стол - право*/
//	//"tcp://192.168.9.62:5555",  // 516 - стол - лево
//	//"tcp://192.168.9.68:5555",  // 516 - окно - право
//	//"tcp://192.168.9.50:5555",  // 528 - стол - право	
//	//"tcp://192.168.9.51:5555",  // 528 - окно - право	
//	//"tcp://192.168.9.45:5555",  // 528 - стол - лево
//	//"tcp://192.168.9.48:5555",  // 528 - окно - лево
//	"tcp://localhost:5555",   // Локальный хост
//	"tcp://*:5555"            // Все интерфейсы
//};
//
//// Адреса для Worker (PUSH socket - подключение к Composer)
//const std::vector<std::string> worker_to_composer_connect_addresses = {
//	/*"tcp://192.168.9.47:5556",  // 516 - стол - право*/
//	//"tcp://192.168.9.62:5556",  // 516 - стол - лево
//	//"tcp://192.168.9.68:5556",  // 516 - окно - право
//	//"tcp://192.168.9.50:5556",  // 528 - стол - право	
// 	//"tcp://192.168.9.51:5555",  // 528 - окно - право	
//	//"tcp://192.168.9.45:5556",  // 528 - стол - лево
//	//"tcp://192.168.9.48:5556",  // 528 - окно - лево
//	"tcp://localhost:5556",   // Локальный хост
//	"tcp://*:5556"            // Все интерфейсы
//};
//
//// Адреса для Composer (PULL socket - привязка)
//const std::vector<std::string> composer_bind_addresses = {
//	//"tcp://192.168.9.47:5556",  // 516 - стол - право
//	/*"tcp://192.168.9.62:5556",  // 516 - стол - лево
//	"tcp://192.168.9.68:5556",  // 516 - окно - право*/
//	//"tcp://192.168.9.50:5556",  // 528 - стол - право	
//	"tcp://localhost:5556",   // Локальный хост
//	"tcp://*:5556"            // Все интерфейсы
//};
//
//// 
////---------- 1. Начальные условия (входные данные) для Capturer ----------
//// 
//int queue_size = 100;       // Максимум кадров в очереди // (2-5)
//int cap_frame_width = 640;  // Ширина кадра
//int cap_frame_height = 480; // Высота кадра  
//int cap_fps = 30;           // Частота кадров           // + Composer // (4) = 15       // (5) = 12
//int cap_quality = 80;       // Качество сжатия (1-100)  // + Worker   // (4) = 70/75    // (5) = 60/65
//enum video_processing::PixelFormat proto_pixel_format = video_processing::BGR;      // Формат пикселей (BGR) // + Worker
//enum video_processing::ImageEncoding proto_image_encoding = video_processing::JPEG; // Кодирование (JPEG)    // + Worker
//
//// 
////----------- 2. Начальные условия (входные данные) для Worker -----------
//// 
//int  effect_canny_low_threshold = 60;      // Нижний порог Кэнни        // (4) = 50  // (5) = 80
//int  effect_canny_high_threshold = 160;    // Верхний порог Кэнни       // (4) = 150
//int  effect_gaussian_kernel_size = 3;      // Размер ядра Гаусса
//int  effect_dilation_kernel_size = 0;      // Размер ядра дилатации
//int  effect_color_quantization_levels = 8; // Уровни квантования цвета  // (4) = 6   // (5) = 4
//bool effect_black_contours = true;         // Черные контуры
//
//// 
////---------- 3. Начальные условия (входные данные) для Composer ----------
////
//int frame_gap = 500;    // (2-5)
//int buffer_size = 2000; // (3-5)
