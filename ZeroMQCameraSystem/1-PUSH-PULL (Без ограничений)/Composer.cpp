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

class Composer {
private:
    zmq::context_t context;     // Контекст ZeroMQ
    zmq::socket_t pull_socket;  // Сокет для получения сообщений от Workers
    std::string composer_id;    // Уникальный идентификатор компоновщика
    std::string temp_original_dir;   // Директория для временного хранения исходных кадров
    std::string temp_processed_dir;  // Директория для временного хранения обработанных кадров
    cv::VideoWriter video_writer_original;   // Запись видео из исходных кадров
    cv::VideoWriter video_writer_processed;  // Запись видео из обработанных кадров
    uint64_t expected_frame_id;  // Ожидаемый порядковый номер следующего кадра
    std::map<uint64_t, std::pair<cv::Mat, cv::Mat>> frame_buffer;  // Буфер для хранения кадров до упорядочивания
    bool recording;  // Флаг активности записи видео
    cv::Size last_frame_size;  // Размер последнего обработанного кадра
    std::chrono::steady_clock::time_point last_frame_time;  // Время получения последнего кадра
    bool first_frame_received;  // Флаг получения первого кадра

public:

    Composer() : context(1), pull_socket(context, ZMQ_PULL),  // Инициализация контекста и PULL-сокета
        expected_frame_id(0), recording(false), first_frame_received(false) {  // Начальные значения переменных

        // Очистка старых файлов перед началом работы
        cleanup_old_video_files();
        std::cout << "=== Composer Initialization ===" << std::endl;
        // Привязка к сетевым интерфейсам
        std::cout << "1. Available network interfaces:" << std::endl;
        // Попытка привязки к каждому адресу из списка
        for (const auto& address : composer_bind_addresses) {
            try {
                pull_socket.bind(address);  // Привязка сокета к адресу
                std::cout << "- [ OK ] Composer bound to: " << address << std::endl;  // Сообщение об успешной привязке
                break;  // Выход из цикла после успешной привязки
            }
            catch (const zmq::error_t& e) {  // Обработка ошибок привязки
                std::cout << "- [FAIL] Failed to bind to " << address << ": " << e.what() << std::endl;  // Сообщение об ошибке
            }
        }

        // Создание директорий для временного хранения кадров
        //temp_original_dir = "Temp_Original";  // Директория для исходных кадров
        //temp_processed_dir = "Temp_Processed";  // Директория для обработанных кадров
        //_mkdir(temp_original_dir.c_str());
        //_mkdir(temp_processed_dir.c_str());

        composer_id = "composer_" + std::to_string(time(nullptr)); // Генерация ID
        last_frame_time = std::chrono::steady_clock::now();
        std::cout << "2. Composer ID: " << composer_id << std::endl;
        std::cout << "======================================================" << std::endl;
    }

private:
    // Проверка существования файла (безопасная версия)
    bool file_exists(const std::string& filename) {
        FILE* file = nullptr;  // Указатель на файл
        if (fopen_s(&file, filename.c_str(), "r") == 0 && file != nullptr) {  // Попытка открыть файл для чтения
            fclose(file);  // Закрытие файла
            return true;  // Файл существует
        }
        return false;  // Файл не существует
    }

    // Очистка старых видеофайлов перед началом работы
    void cleanup_old_video_files() {
        // Список файлов для очистки
        std::vector<std::string> video_files = {
            "output_original.avi",  // Файл исходного видео
            "output_processed.avi"  // Файл обработанного видео
        };

        std::cout << "=== Cleaning up old video files ===" << std::endl;  // Заголовок процесса очистки

        int deleted_count = 0;  // Счетчик удаленных файлов
        for (const auto& filename : video_files) {  // Перебор всех файлов
            if (file_exists(filename)) {  // Проверка существования файла
                if (std::remove(filename.c_str()) == 0) {  // Попытка удаления файла
                    std::cout << "- [ OK ] Deleted: " << filename << std::endl;  // Сообщение об успешном удалении
                    deleted_count++;  // Увеличение счетчика
                }
                else {
                    std::cout << "- [FAIL] Failed to delete: " << filename << std::endl;  // Сообщение об ошибке удаления
                }
            }
            else {
                std::cout << "- [ OK ] Not found: " << filename << std::endl;  // Сообщение об отсутствии файла
            }
        }
        std::cout << "- [ OK ] Deleted " << deleted_count << " old video files" << std::endl;  // Итог очистки
        std::cout << "======================================================" << std::endl;
    }

    // Извлечение изображения из protobuf сообщения
    cv::Mat extract_image(const video_processing::ImageData& image_data) {
        const std::string& data = image_data.image_data();  // Получение данных изображения
        std::vector<uchar> buffer(data.begin(), data.end());  // Конвертация в вектор байтов

        if (image_data.encoding() == proto_image_encoding) {  // Если изображение в формате JPEG
            return cv::imdecode(buffer, cv::IMREAD_COLOR);  // Декодирование JPEG
        }
        else {  // Для RAW формата
            return cv::Mat(  // Создание матрицы OpenCV из raw данных
                image_data.height(),  // Высота изображения
                image_data.width(),   // Ширина изображения
                CV_8UC3,              // Формат: 3 канала по 8 бит
                (void*)data.data()    // Указатель на данные
            ).clone();  // Создание копии данных
        }
    }

    // Инициализация видео-рекордеров
    void initialize_video_writers(const cv::Mat& first_frame) {
        std::string video_original_path = "output_original.avi";  // Путь для исходного видео
        std::string video_processed_path = "output_processed.avi";  // Путь для обработанного видео

        // Кодек и параметры видео
        int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');  // Кодек MJPEG
        double fps = cap_fps;  // Частота кадров
        cv::Size frame_size(first_frame.cols, first_frame.rows);  // Размер кадра
        last_frame_size = frame_size;  // Сохранение размера для будущего использования

        // Открытие видео-рекордеров
        video_writer_original.open(video_original_path, fourcc, fps, frame_size);
        video_writer_processed.open(video_processed_path, fourcc, fps, frame_size);

        // Проверка успешности инициализации
        if (video_writer_original.isOpened() && video_writer_processed.isOpened()) {
            recording = true;  // Установка флага записи
            std::cout << "- [ OK ] Video recording started: " << fps << " FPS, "  // Сообщение о начале записи
                << frame_size.width << "x" << frame_size.height << std::endl;
            std::cout << "- [ OK ] Output files: " << video_original_path << ", " << video_processed_path << std::endl;
        }
        else {
            std::cout << "- [FAIL] Failed to initialize video writers!" << std::endl;  // Сообщение об ошибке
            std::cout << "Original writer: " << (video_writer_original.isOpened() ? "OK" : "FAILED") << std::endl;
            std::cout << "Processed writer: " << (video_writer_processed.isOpened() ? "OK" : "FAILED") << std::endl;
        }
    }

    // Обработка полученного кадра
    void process_frame_for_video(const video_processing::VideoFrame& frame) {
        // Обрабатываем только кадры с изображениями
        if (frame.has_image_pair()) {  // Проверка наличия пары изображений
            const auto& pair = frame.image_pair();  // Получение пары изображений
            cv::Mat original_image = extract_image(pair.original());  // Извлечение исходного изображения
            cv::Mat processed_image = extract_image(pair.processed());  // Извлечение обработанного изображения

            if (!original_image.empty() && !processed_image.empty()) {  // Проверка валидности изображений
                // Обновляем время последнего кадра
                last_frame_time = std::chrono::steady_clock::now();
                first_frame_received = true;  // Установка флага получения первого кадра

                // Инициализируем VideoWriter при первом кадре
                if (!recording) {
                    initialize_video_writers(original_image);
                }

                // Сохраняем в буфер с ключом по frame_id
                frame_buffer[frame.frame_id()] = { original_image, processed_image };

                // Пытаемся записать упорядоченные кадры
                write_ordered_frames();
            }
        }
    }

    // Запись упорядоченных кадров в видео
    void write_ordered_frames() {
        // Ищем следующий ожидаемый кадр в буфере
        while (frame_buffer.find(expected_frame_id) != frame_buffer.end()) {
            auto& frames = frame_buffer[expected_frame_id];  // Получение пары кадров

            // Записываем в видео
            if (recording) {
                video_writer_original.write(frames.first);   // Запись исходного кадра
                video_writer_processed.write(frames.second); // Запись обработанного кадра
            }

            // Сохраняем отдельные файлы для отладки
            //save_frame_pair(expected_frame_id, frames.first, frames.second);

            // Удаляем из буфера и увеличиваем счетчик
            frame_buffer.erase(expected_frame_id);
            expected_frame_id++;

            // Выводим прогресс каждые 10 кадров ...% 10 == 0
            if (expected_frame_id) {
                std::cout << "- [ OK ] Written to video: frame " << (expected_frame_id - 1)
                    << " (buffer: " << frame_buffer.size() << ")" << std::endl;
            }
        }
    }

    // Сохранение пары кадров как отдельных файлов
    void save_frame_pair(uint64_t frame_id, const cv::Mat& original, const cv::Mat& processed) {
        // Формирование имен файлов
        std::string original_filename = temp_original_dir + "\\frame_" +
            std::to_string(frame_id) + ".jpg";
        std::string processed_filename = temp_processed_dir + "\\frame_" +
            std::to_string(frame_id) + ".jpg";

        // Сохранение изображений
        cv::imwrite(original_filename, original);
        cv::imwrite(processed_filename, processed);
    }

    // Проверка условия остановки компоновщика
    bool should_stop() {
        if (!first_frame_received) {
            return false; // Ждем первый кадр перед проверкой таймаута
        }

        auto now = std::chrono::steady_clock::now();  // Текущее время
        auto time_since_last_frame = std::chrono::duration_cast<std::chrono::seconds>(now - last_frame_time);

        // Останавливаемся если нет кадров 10 секунд и буфер пуст
        if (time_since_last_frame > std::chrono::seconds(10) && frame_buffer.empty()) {
            std::cout << "- [ OK ] No frames for 10 seconds. Finishing..." << std::endl;
            return true;
        }

        return false;
    }

    // Обработка пропущенных кадров (вставка черных кадров)
    void handle_missing_frames() {
        // Вставляем черные кадры для пропущенных номеров, если буфер не пуст
        // и следующий ожидаемый кадр сильно отстает от максимального в буфере
        if (!frame_buffer.empty()) {
            uint64_t max_buffered_id = frame_buffer.rbegin()->first;  // Максимальный ID в буфере

            // Если разрыв больше 50 кадров, вставляем черные кадры
            if (max_buffered_id - expected_frame_id > 50) {
                std::cout << "- [WARN] Large gap detected. Inserting black frames..." << std::endl;

                // Вставляем до следующего доступного кадра
                while (expected_frame_id < max_buffered_id &&
                    frame_buffer.find(expected_frame_id) == frame_buffer.end()) {

                    if (recording && last_frame_size.width > 0) {
                        cv::Mat black_frame = cv::Mat::zeros(last_frame_size, CV_8UC3);  // Создание черного кадра
                        video_writer_original.write(black_frame);  // Запись черного кадра в исходное видео
                        video_writer_processed.write(black_frame); // Запись черного кадра в обработанное видео
                    }

                    std::cout << "- [WARN] Inserted black frame for missing frame " << expected_frame_id << std::endl;
                    expected_frame_id++;  // Переход к следующему кадру
                }
            }
        }
    }

public:
    // Основной цикл работы компоновщика
    void run() {
        std::cout << "=== Composer Started ===" << std::endl;
        std::cout << std::endl << "=== 1-No buffer limits ===" << std::endl << std::endl;
        std::cout << "3. Will auto-stop after 10 seconds of inactivity." << std::endl;

        uint64_t total_frames_received = 0;  // Счетчик всех полученных кадров

        while (true) {
            try {
                zmq::message_t message;  // Сообщение ZeroMQ

                // Настройка poll для неблокирующего receive с таймаутом 1 секунда
                zmq::pollitem_t items[] = { { static_cast<void*>(pull_socket), 0, ZMQ_POLLIN, 0 } };
                zmq::poll(items, 1, 1000); // Таймаут 1 секунда

                if (items[0].revents & ZMQ_POLLIN) {  // Проверка наличия сообщения
                    // Есть сообщение - получаем его
                    if (pull_socket.recv(&message)) {
                        video_processing::VideoFrame frame;  // Создание объекта для protobuf сообщения
                        if (frame.ParseFromArray(message.data(), message.size())) {  // Парсинг protobuf
                            process_frame_for_video(frame);  // Обработка кадра
                            total_frames_received++;  // Увеличение счетчика
                        }
                    }
                }

                // Проверяем пропущенные кадры периодически ... % 20 == 0
                if (total_frames_received) {
                    handle_missing_frames();
                }

                // Проверяем условие остановки
                if (should_stop()) {
                    break;  // Выход из основного цикла
                }

                // Показываем прогресс каждые 10 кадров
                if (expected_frame_id > 0 && expected_frame_id % 10 == 0) {
                    std::cout << "=== Progress: processed " << expected_frame_id << " frames. "
                        << "Buffered: " << frame_buffer.size() << " frames. ===" << std::endl;
                }

            }
            catch (const zmq::error_t& e) {  // Обработка ошибок ZeroMQ
                if (e.num() != EINTR) {  // Игнорирование прерванных системных вызовов
                    std::cout << "- [FAIL] ZMQ error: " << e.what() << std::endl;
                }
                break;  // Выход при ошибке
            }
            catch (const std::exception& e) {  // Обработка общих исключений
                std::cout << "- [FAIL] Processing error: " << e.what() << std::endl;
            }
        }

        // Финальная статистика работы
        std::cout << "\n=== COMPOSER FINISHED ===" << std::endl;
        std::cout << "Total frames received: " << total_frames_received << std::endl;
        std::cout << "Total frames written: " << expected_frame_id << std::endl;
        std::cout << "Frames remaining in buffer: " << frame_buffer.size() << std::endl;

        // При завершении закрываем видеофайлы
        if (recording) {
            // Записываем оставшиеся кадры в буфере
            while (!frame_buffer.empty()) {
                write_ordered_frames();
            }

            video_writer_original.release();  // Закрытие рекордера исходного видео
            video_writer_processed.release(); // Закрытие рекордера обработанного видео
            std::cout << "Video files finalized: output_original.avi, output_processed.avi" << std::endl;
        }

        std::cout << "=== Thank you for using ZeroMQ Camera System ===" << std::endl;
    }
};

// Точка входа в программу
int main() {
    try {
        Composer composer;  // Создание экземпляра компоновщика
        composer.run();     // Запуск основного цикла
        return 0;          // Успешное завершение
    }
    catch (const std::exception& e) {  // Обработка исключений на верхнем уровне
        std::cout << "- [FAIL] Composer error: " << e.what() << std::endl;
        return -1;  // Завершение с ошибкой
    }
}