#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include "video_processing.pb.h"
#include "..\video_addresses.h"
#include <direct.h>
#include <chrono>
#include <map>
#include <cstdio>
#include <iomanip>
#include <atomic>
#include <queue>

class Composer {
private:
    zmq::context_t context; // Контекст ZeroMQ для работы с сокетами
    zmq::socket_t pull_socket; // Сокет для получения сообщений от Workers
    std::string composer_id; // Уникальный идентификатор компоновщика
    std::string temp_original_dir; // Временная директория для исходных кадров
    std::string temp_processed_dir; // Временная директория для обработанных кадров
    cv::VideoWriter video_writer_original; // Видео-записыватель для исходного видео
    cv::VideoWriter video_writer_processed; // Видео-записыватель для обработанного видео
    uint64_t expected_frame_id; // Ожидаемый порядковый номер следующего кадра
    uint64_t last_written_frame_id; // Номер последнего записанного кадра
    uint64_t highest_received_frame_id; // Наибольший полученный номер кадра
    std::map<uint64_t, std::pair<cv::Mat, cv::Mat>> frame_buffer; // Буфер для хранения кадров (номер → (оригинал, обработанный))
    bool recording; // Флаг активности записи видео
    cv::Size last_frame_size; // Размер последнего обработанного кадра
    std::chrono::steady_clock::time_point last_frame_time; // Время получения последнего кадра
    bool first_frame_received; // Флаг получения первого кадра
    uint64_t max_frame_gap; // Максимально допустимый разрыв между кадрами
    std::chrono::steady_clock::time_point last_frame_received_time; // Время получения последнего сообщения
    std::atomic<uint64_t> total_frames_received; // Счетчик всех полученных кадров
    std::atomic<uint64_t> black_frames_inserted; // Счетчик вставленных черных кадров
    std::atomic<uint64_t> total_frames_written; // Счетчик записанных кадров
    std::atomic<bool> stop_requested; // Флаг запроса остановки
    std::chrono::steady_clock::time_point start_time; // Время начала работы
    uint64_t max_buffer_size; // Максимальный размер буфера кадров

public:
    // Конструктор класса Composer
    Composer() : context(1), pull_socket(context, ZMQ_PULL), // Инициализация контекста и PULL-сокета
        expected_frame_id(0), last_written_frame_id(0), highest_received_frame_id(0), // Инициализация счетчиков кадров
        recording(false), first_frame_received(false), max_frame_gap(frame_gap), // Инициализация флагов и параметров
        total_frames_received(0), black_frames_inserted(0), // Инициализация атомарных счетчиков
        total_frames_written(0), stop_requested(false), max_buffer_size(buffer_size) { // Инициализация остальных параметров

        cleanup_old_video_files(); // Очистка старых видеофайлов
        std::cout << "=== Composer Initialization ===" << std::endl;
        // Привязка к сетевым интерфейсам
        std::cout << "1. Available network interfaces:" << std::endl;
        // Попытка привязаться к одному из адресов
        for (const auto& address : composer_bind_addresses) {
            try {
                pull_socket.bind(address); // Привязка сокета к адресу
                std::cout << "- [ OK ] Composer bound to: " << address << std::endl; // Сообщение об успешной привязке
                break; // Выход из цикла после успешной привязки
            }
            catch (const zmq::error_t& e) { // Обработка ошибок привязки
                std::cout << "- [FAIL] Failed to bind to " << address << ": " << e.what() << std::endl; // Сообщение об ошибке
            }
        }

        composer_id = "composer_" + std::to_string(time(nullptr)); // Генерация уникального ID
        last_frame_time = std::chrono::steady_clock::now(); // Инициализация времени последнего кадра
        last_frame_received_time = std::chrono::steady_clock::now(); // Инициализация времени получения
        start_time = std::chrono::steady_clock::now(); // Запись времени начала работы

        std::cout << "2. Composer ID: " << composer_id << std::endl;
        std::cout << "======================================================" << std::endl;
        //std::cout << "Conservative black frame insertion (gap > " << max_frame_gap << " frames)" << std::endl; // Информация о стратегии
        //std::cout << "Will preserve all received frames" << std::endl; // Информация о сохранении кадров
    }

private:
    // Проверка существования файла
    bool file_exists(const std::string& filename) {
        FILE* file = nullptr; // Указатель на файл
        if (fopen_s(&file, filename.c_str(), "r") == 0 && file != nullptr) { // Попытка открыть файл для чтения
            fclose(file); // Закрытие файла
            return true; // Файл существует
        }
        return false; // Файл не существует
    }

    // Очистка старых видеофайлов
    void cleanup_old_video_files() {
        std::vector<std::string> video_files = { // Список видеофайлов для удаления
            "output_original.avi", // Исходное видео
            "output_processed.avi" // Обработанное видео
        };

        std::cout << "=== Cleaning up old video files ===" << std::endl; // Заголовок процесса очистки
        int deleted_count = 0; // Счетчик удаленных файлов
        for (const auto& filename : video_files) { // Перебор всех файлов
            if (file_exists(filename)) {  // Проверка существования файла
                if (std::remove(filename.c_str()) == 0) {  // Попытка удаления файла
                    std::cout << "- [ OK ] Deleted: " << filename << std::endl;  // Сообщение об успешном удалении
                    deleted_count++;  // Увеличение счетчика удаленных файлов
                }
                else {
                    std::cout << "- [FAIL] Failed to delete: " << filename << std::endl;  // Сообщение об ошибке удаления
                }
            }
            else {
                std::cout << "- [ OK ] Not found: " << filename << std::endl;  // Сообщение об отсутствии файла
            }
        }
        std::cout << "- [ OK ] Deleted " << deleted_count << " old video files" << std::endl; // Итоговое сообщение
        std::cout << "======================================================" << std::endl;
    }

    // Извлечение изображения из protobuf-сообщения
    cv::Mat extract_image(const video_processing::ImageData& image_data) {
        const std::string& data = image_data.image_data(); // Получение данных изображения
        std::vector<uchar> buffer(data.begin(), data.end()); // Конвертация в вектор байтов

        if (image_data.encoding() == proto_image_encoding) { // Если изображение в формате JPEG
            return cv::imdecode(buffer, cv::IMREAD_COLOR); // Декодирование JPEG
        }
        else { // Если сырые данные
            return cv::Mat( // Создание матрицы OpenCV
                image_data.height(), // Высота изображения
                image_data.width(), // Ширина изображения
                CV_8UC3, // Тип данных (3-канальное 8-битное)
                (void*)data.data() // Указатель на данные
            ).clone(); // Клонирование матрицы
        }
    }

    // Инициализация видео-записывателей
    void initialize_video_writers(const cv::Mat& first_frame) {
        std::string video_original_path = "output_original.avi"; // Путь для исходного видео
        std::string video_processed_path = "output_processed.avi"; // Путь для обработанного видео

        int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G'); // Кодек MJPG
        double fps = cap_fps; // Частота кадров
        cv::Size frame_size(first_frame.cols, first_frame.rows); // Размер кадра
        last_frame_size = frame_size; // Сохранение размера кадра

        video_writer_original.open(video_original_path, fourcc, fps, frame_size); // Открытие записи исходного видео
        video_writer_processed.open(video_processed_path, fourcc, fps, frame_size); // Открытие записи обработанного видео

        if (video_writer_original.isOpened() && video_writer_processed.isOpened()) { // Проверка успешного открытия
            recording = true; // Установка флага записи
            std::cout << "- [ OK ] Video recording started: " << fps << " FPS, " // Сообщение о начале записи
                << frame_size.width << "x" << frame_size.height << std::endl; // Информация о параметрах
        }
    }

    // Вставка черного кадра с номером пропущенного кадра
    void insert_black_frame(uint64_t frame_id) {
        if (!recording || last_frame_size.width == 0) return; // Проверка возможности записи

        cv::Mat black_frame = cv::Mat::zeros(last_frame_size, CV_8UC3); // Создание черного кадра
        std::string text = "MISSING FRAME " + std::to_string(frame_id); // Текст с номером пропущенного кадра
        cv::putText(black_frame, text, // Добавление текста на кадр
            cv::Point(50, last_frame_size.height / 2), // Позиция текста
            cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2); // Параметры шрифта

        video_writer_original.write(black_frame); // Запись в исходное видео
        video_writer_processed.write(black_frame); // Запись в обработанное видео
        total_frames_written++; // Увеличение счетчика записанных кадров
        last_written_frame_id = frame_id; // Обновление номера последнего записанного кадра

        std::cout << "- [ -- ] Inserted black frame: " << frame_id << std::endl; // Сообщение о вставке черного кадра
    }

    // Основная функция записи - пытается записать как можно больше кадров
    void write_available_frames() {
        if (frame_buffer.empty() || !recording) return; // Проверка наличия кадров и активности записи

        bool wrote_any_frame = false; // Флаг записи хотя бы одного кадра

        // Пытаемся записать все доступные кадры начиная с expected_frame_id
        while (frame_buffer.find(expected_frame_id) != frame_buffer.end()) { // Пока есть ожидаемый кадр
            auto& frames = frame_buffer[expected_frame_id]; // Получение пары кадров

            video_writer_original.write(frames.first); // Запись исходного кадра
            video_writer_processed.write(frames.second); // Запись обработанного кадра
            total_frames_written++; // Увеличение счетчика
            last_written_frame_id = expected_frame_id; // Обновление номера последнего кадра

            frame_buffer.erase(expected_frame_id); // Удаление кадра из буфера
            expected_frame_id++; // Увеличение ожидаемого номера
            wrote_any_frame = true; // Установка флага записи

            if (expected_frame_id) { // Каждые 50 кадров ... % 50 == 0
                std::cout << "- [ OK ] Written to video: frame " << (expected_frame_id - 1) // Сообщение о записи
                    << " (buffer: " << frame_buffer.size() << ")" << std::endl; // Информация о размере буфера
            }
        }

        // Если мы что-то записали, проверяем не нужно ли вставить черные кадры
        if (wrote_any_frame) {
            check_for_missing_frames_conservative(); // Проверка пропусков
        }
    }

    // Консервативная проверка пропусков - вставляет черные кадры только при явных разрывах
    void check_for_missing_frames_conservative() {
        if (frame_buffer.empty()) return; // Проверка наличия кадров в буфере

        // Находим минимальный frame_id в буфере
        uint64_t min_buffered_id = frame_buffer.begin()->first; // Первый элемент в отсортированном map

        // Вычисляем разрыв
        uint64_t gap = min_buffered_id - expected_frame_id; // Разница между минимальным в буфере и ожидаемым

        // Вставляем черные кадры только если:
        // 1. Очень большой разрыв (больше max_frame_gap)
        // 2. И в буфере есть кадры с намного большими номерами (значит это не временная задержка)
        if (gap > max_frame_gap) { // Проверка на большой разрыв
            uint64_t max_buffered_id = frame_buffer.rbegin()->first; // Максимальный номер в буфере

            // Если максимальный кадр в буфере намного больше минимального, 
            // значит мы действительно пропустили кадры
            if (max_buffered_id - min_buffered_id > 50) { // Проверка разброса в буфере
                std::cout << "- [ -- ] Large gap detected: " << gap << " frames from " // Сообщение о большом разрыве
                    << expected_frame_id << " to " << (min_buffered_id - 1) << std::endl;

                for (uint64_t frame_id = expected_frame_id; frame_id < min_buffered_id; frame_id++) { // Заполнение пропусков
                    insert_black_frame(frame_id); // Вставка черного кадра
                    black_frames_inserted++; // Увеличение счетчика черных кадров
                }
                expected_frame_id = min_buffered_id; // Обновление ожидаемого номера

                // После вставки черных кадров пытаемся записать дальше
                write_available_frames(); // Продолжение записи
            }
        }
    }

    // Обработка полученного кадра
    void process_frame_for_video(const video_processing::VideoFrame& frame) {
        last_frame_received_time = std::chrono::steady_clock::now(); // Обновление времени получения

        if (frame.has_image_pair()) { // Проверка наличия пары изображений
            const auto& pair = frame.image_pair(); // Получение пары изображений
            cv::Mat original_image = extract_image(pair.original()); // Извлечение исходного изображения
            cv::Mat processed_image = extract_image(pair.processed()); // Извлечение обработанного изображения

            if (!original_image.empty() && !processed_image.empty()) { // Проверка валидности изображений
                last_frame_time = std::chrono::steady_clock::now(); // Обновление времени последнего кадра

                if (!first_frame_received) { // Если это первый кадр
                    first_frame_received = true; // Установка флага
                    std::cout << "- [ OK ] First frame received: " << frame.frame_id() << std::endl; // Сообщение
                }

                if (!recording) { // Если запись еще не начата
                    initialize_video_writers(original_image); // Инициализация записи
                }

                uint64_t received_frame_id = frame.frame_id(); // Получение номера кадра

                // Всегда обновляем highest_received_frame_id
                if (received_frame_id > highest_received_frame_id) { // Если номер больше текущего максимального
                    highest_received_frame_id = received_frame_id; // Обновление максимального номера
                }

                // Никогда не пропускаем старые кадры - сохраняем все!
                if (received_frame_id < expected_frame_id) { // Если кадр устаревший
                    // Кадр устарел, но мы его все равно сохраняем в буфер
                    std::cout << "- [ -- ] Late frame: " << received_frame_id // Сообщение об устаревшем кадре
                        << " (expected: " << expected_frame_id << ")" << std::endl;
                }

                // Сохраняем в буфер
                if (frame_buffer.size() < max_buffer_size) { // Проверка переполнения буфера
                    frame_buffer[received_frame_id] = std::make_pair(original_image, processed_image); // Сохранение в буфер

                    std::cout << "- [ OK ] Received frame: " << received_frame_id // Сообщение о получении
                        << " (highest: " << highest_received_frame_id // Информация о максимальном номере
                        << ", buffer: " << frame_buffer.size() << ")" << std::endl; // Информация о размере буфера

                    // Пытаемся записать доступные кадры
                    write_available_frames(); // Запись доступных кадров
                }
            }
        }
    }

    // Проверка необходимости остановки
    bool should_stop() {
        if (!first_frame_received) return false; // Не останавливаемся если нет ни одного кадра

        auto now = std::chrono::steady_clock::now(); // Текущее время
        auto time_since_last_frame = std::chrono::duration_cast<std::chrono::seconds>(now - last_frame_received_time); // Время с последнего кадра

        // Останавливаемся если нет кадров 10 секунд
        if (time_since_last_frame > std::chrono::seconds(10)) { // Проверка таймаута
            std::cout << "- [ OK ] No frames for 10 seconds. Starting final processing..." << std::endl; // Сообщение
            return true; // Сигнал остановки
        }

        return false; // Продолжение работы
    }

    // Отображение статистики
    void show_statistics() {
        auto now = std::chrono::steady_clock::now(); // Текущее время
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time); // Прошедшее время
        double fps = (elapsed.count() > 0) ? total_frames_written / elapsed.count() : 0; // Расчет FPS

        std::cout << "=== Composer stats: " // Заголовок статистики
            << total_frames_received << " received, " // Полученные кадры
            << total_frames_written << " written, " // Записанные кадры
            << black_frames_inserted << " black inserted, " // Вставленные черные кадры
            << frame_buffer.size() << " buffered, " // Кадры в буфере
            << "expected: " << expected_frame_id << ", " // Ожидаемый номер
            << "highest: " << highest_received_frame_id << ", " // Максимальный номер
            << std::fixed << std::setprecision(1) << "" << std::endl; // FPS
            //fps << " fps" << std::endl; // FPS
    }

    // Финальная обработка оставшихся кадров
    void final_processing() {
        std::cout << "- [ OK ] Final processing: writing all remaining frames..." << std::endl; // Сообщение

        // Сначала записываем все последовательные кадры
        write_available_frames(); // Запись доступных кадров

        // Если в буфере еще остались кадры (не последовательные)
        if (!frame_buffer.empty()) { // Проверка наличия кадров в буфере
            std::cout << "- [ -- ] Processing " << frame_buffer.size() << " remaining frames in buffer..." << std::endl; // Сообщение

            // Создаем временную карту для сортировки (она уже отсортирована по frame_id)
            // Проходим по всем кадрам в буфере по порядку
            for (auto it = frame_buffer.begin(); it != frame_buffer.end(); ++it) { // Перебор всех кадров
                uint64_t frame_id = it->first; // Номер кадра
                std::pair<cv::Mat, cv::Mat>& frames = it->second; // Пара изображений

                // Вставляем черные кадры для пропусков до этого кадра
                while (expected_frame_id < frame_id) { // Пока есть пропуски
                    insert_black_frame(expected_frame_id); // Вставка черного кадра
                    black_frames_inserted++; // Увеличение счетчика
                    expected_frame_id++; // Увеличение ожидаемого номера
                }

                // Записываем сам кадр
                if (recording) { // Если запись активна
                    video_writer_original.write(frames.first); // Запись исходного
                    video_writer_processed.write(frames.second); // Запись обработанного
                    total_frames_written++; // Увеличение счетчика
                    last_written_frame_id = frame_id; // Обновление последнего номера
                }
                expected_frame_id = frame_id + 1; // Обновление ожидаемого номера

                std::cout << "- [ OK ] Final write: frame " << frame_id << std::endl; // Сообщение о записи
            }

            frame_buffer.clear(); // Очистка буфера
        }
    }

public:
    // Основной цикл работы Composer
    void run() {
        std::cout << "=== Composer Started ===" << std::endl;  // Сообщение о запуске
        std::cout << std::endl << "=== 5-PULL-PUSH with dynamic balancing ===" << std::endl << std::endl;
        std::cout << "3. Preserves all frames from multiple workers" << std::endl; // Информация

        while (!stop_requested) { // Основной цикл
            try {
                zmq::message_t message; // Сообщение ZeroMQ
                zmq::pollitem_t items[] = { { static_cast<void*>(pull_socket), 0, ZMQ_POLLIN, 0 } }; // Элементы для опроса
                zmq::poll(items, 1, 500); // Опрос сокета с таймаутом 500 мс

                if (items[0].revents & ZMQ_POLLIN) { // Если есть входящее сообщение
                    if (pull_socket.recv(&message, ZMQ_DONTWAIT)) { // Неблокирующее получение
                        video_processing::VideoFrame frame; // Создание объекта для protobuf
                        if (frame.ParseFromArray(message.data(), message.size())) { // Парсинг protobuf
                            process_frame_for_video(frame); // Обработка кадра
                            total_frames_received++; // Увеличение счетчика
                        }
                    }
                }

                // Периодически пытаемся записать кадры (даже если нет новых сообщений)
                if (total_frames_received % 20 == 0) { // Каждые 20 кадров
                    write_available_frames(); // Запись доступных кадров
                }

                if (total_frames_received % 50 == 0 && total_frames_received > 0) { // Каждые 50 кадров
                    show_statistics(); // Показать статистику
                }

                if (should_stop()) break; // Проверка необходимости остановки

                if (cv::waitKey(1) == 27) { // Проверка нажатия ESC
                    stop_requested = true; // Установка флага остановки
                }

            }
            catch (const zmq::error_t& e) { // Обработка ошибок ZeroMQ
                if (e.num() != EAGAIN && e.num() != EINTR) { // Игнорирование стандартных ошибок
                    std::cout << "- [FAIL] ZMQ error: " << e.what() << std::endl; // Сообщение об ошибке
                }
            }
            catch (const std::exception& e) { // Обработка общих исключений
                std::cout << "- [FAIL] Processing error: " << e.what() << std::endl; // Сообщение об ошибке
            }
        }

        std::cout << "- [ OK ] Starting final processing..." << std::endl; // Сообщение
        final_processing(); // Финальная обработка

        // Финальная статистика
        auto end_time = std::chrono::steady_clock::now(); // Время окончания
        auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time); // Общее время работы

        std::cout << "\n=== COMPOSER FINISHED ===" << std::endl; // Заголовок
        std::cout << "Total frames received: " << total_frames_received << std::endl; // Итоговая статистика
        std::cout << "Total frames written: " << total_frames_written << std::endl; // Итоговая статистика
        std::cout << "Black frames inserted: " << black_frames_inserted << std::endl; // Итоговая статистика
        std::cout << "Frames remaining in buffer: " << frame_buffer.size() << std::endl; // Итоговая статистика

        if (total_frames_written > 0) { // Если были записаны кадры
            double black_rate = (double)black_frames_inserted / total_frames_written * 100; // Расчет процента черных кадров
            std::cout << "Black frame rate: " << std::fixed << std::setprecision(1) << black_rate << "%" << std::endl; // Вывод процента

            double fps = (total_elapsed.count() > 0) ? total_frames_written / total_elapsed.count() : 0; // Расчет среднего FPS
            std::cout << "Average FPS: " << std::fixed << std::setprecision(1) << fps << std::endl; // Вывод FPS
        }

        if (recording) { // Если запись была активна
            video_writer_original.release(); // Закрытие записи исходного видео
            video_writer_processed.release(); // Закрытие записи обработанного видео
            std::cout << "Video files finalized" << std::endl; // Сообщение
        }

        std::cout << "=== Thank you for using ZeroMQ Camera System ===" << std::endl; // Завершающее сообщение
    }
};

// Главная функция
int main() {
    try {
        Composer composer; // Создание объекта Composer
        composer.run(); // Запуск основного цикла
        return 0; // Успешное завершение
    }
    catch (const std::exception& e) { // Обработка исключений
        std::cout << "- [FAIL] Composer error: " << e.what() << std::endl; // Сообщение об ошибке
        return -1; // Завершение с ошибкой
    }
}