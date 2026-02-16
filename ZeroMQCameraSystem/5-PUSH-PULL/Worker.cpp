#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include "video_processing.pb.h"
#include "scanner_darkly_effect.hpp"
#include "..\video_addresses.h"
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <process.h>

class PullWorker {
private:
    zmq::context_t context;  // Контекст ZeroMQ для управления сокетами
    zmq::socket_t pull_socket;  // Сокет для получения кадров от Capturer
    zmq::socket_t push_socket;  // Сокет для отправки результатов в Composer
    std::string worker_id;  // Уникальный идентификатор воркера
    ScannerDarklyEffect effect;  // Объект для применения визуального эффекта
    std::string capturer_address; // Адрес Capturer'а
    std::string composer_address; // Адрес Composer'а
    std::atomic<uint64_t> processed_count;  // Счетчик успешно обработанных кадров
    std::atomic<uint64_t> failed_count;  // Счетчик неудачных обработок
    std::chrono::steady_clock::time_point start_time;  // Время начала работы
    std::atomic<bool> stop_requested;  // Флаг для graceful shutdown

public:
    PullWorker() : context(1),  // Инициализация контекста с 1 IO thread
        pull_socket(context, ZMQ_PULL),  // Создание PULL сокета
        push_socket(context, ZMQ_PUSH),  // Создание PUSH сокета
        processed_count(0), failed_count(0), stop_requested(false) {  // Инициализация счетчиков и флагов

        std::cout << "=== Worker Initialization ===" << std::endl;
        std::cout << "1. Available capturer network interfaces:" << std::endl;

        // Генерация уникального ID воркера на основе PID процесса
        worker_id = "worker_" + std::to_string(_getpid());

        int linger = 0;  // Не задерживать сообщения при закрытии сокета
        int rcvhwm = 5;  // Максимум 5 сообщений в очереди приема

        // Применение настроек сокетов
        pull_socket.setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
        pull_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        push_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

        // подключение к capturer (pull)
        for (const auto& address : worker_to_capturer_connect_addresses) {
            try {
                pull_socket.connect(address);  // Пытаемся подключиться
                capturer_address = address;      // Сохраняем успешный адрес
                std::cout << "- [ OK ] PULL connected to Capturer: " << address << std::endl;
                break;  // Выходим из цикла при успешном подключении
            }
            catch (const zmq::error_t& e) {  // Обработка ошибок подключения
                std::cout << "- [FAIL] Failed to connect PULL to " << address << ": " << e.what() << std::endl;
            }
        }

        // подключение к composer (push)
        for (const auto& address : worker_to_composer_connect_addresses) {
            try {
                push_socket.connect(address);  // Подключаем PUSH сокет
                composer_address = address;    // Сохраняем успешный адрес
                std::cout << "- [ OK ] PUSH connected to Composer: " << address << std::endl;
                break;  // Выходим при успехе
            }
            catch (const zmq::error_t& e) {  // Обработка ошибок
                std::cout << "- [FAIL] Failed to connect PUSH to " << address << ": " << e.what() << std::endl;
            }
        }

        effect.setCannyThresholds(effect_canny_low_threshold, effect_canny_high_threshold);    // Пороги для детектора границ Кэнни
        effect.setGaussianKernelSize(effect_gaussian_kernel_size);       // Размер ядра размытия Гаусса
        effect.setDilationKernelSize(effect_dilation_kernel_size);       // Отключение утолщения контуров
        effect.setColorQuantizationLevels(effect_color_quantization_levels);  // Количество цветовых кластеров
        effect.setBlackContours(effect_black_contours);         // Использовать черные контуры

        // Запись времени начала работы для статистики
        start_time = std::chrono::steady_clock::now();

        std::cout << "2. Worker initialized with Scanner Darkly effect" << std::endl;
        std::cout << "3. Worker ID: " << worker_id << std::endl;
        std::cout << "======================================================" << std::endl;
    }

    ~PullWorker() {
        // Установка флага остановки при разрушении объекта
        stop_requested = true;
    }

private:
    // Извлечение изображения из protobuf сообщения
    cv::Mat extract_image(const video_processing::ImageData& image_data) {
        // Получение данных изображения как строки
        const std::string& data = image_data.image_data();
        // Конвертация в вектор байтов для OpenCV
        std::vector<uchar> buffer(data.begin(), data.end());

        // Декодирование JPEG изображения
        if (image_data.encoding() == video_processing::JPEG) {
            cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
            if (decoded.empty()) {
                throw std::runtime_error("- [FAIL] Failed to decode JPEG image");
            }
            return decoded;
        }
        else {
            // Создание матрицы из raw данных
            return cv::Mat(
                image_data.height(),      // Высота изображения
                image_data.width(),       // Ширина изображения
                CV_8UC3,                  // 3-канальное 8-битное изображение
                (void*)data.data()        // Указатель на данные
            ).clone();                    // Создание копии данных
        }
    }

    // Создание protobuf сообщения с изображением
    video_processing::ImageData create_image_data(const cv::Mat& image) {
        video_processing::ImageData image_data;

        // Буфер для сжатого изображения
        std::vector<uchar> buffer;
        // Параметры сжатия JPEG (качество 65%)
        std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality };
        // Кодирование изображения в JPEG формат
        cv::imencode(".jpg", image, buffer, compression_params);

        // Заполнение полей protobuf сообщения
        image_data.set_width(image.cols);      // Ширина
        image_data.set_height(image.rows);     // Высота
        image_data.set_pixel_format(proto_pixel_format);  // Формат пикселей
        image_data.set_encoding(proto_image_encoding);     // Кодирование
        image_data.set_image_data(buffer.data(), buffer.size());  // Данные изображения

        return image_data;
    }

    // Обработка одного кадра
    bool process_frame(const video_processing::VideoFrame& input_frame) {
        // Логирование каждые 20 кадров ...% 20 == 0
        if (processed_count) {
            std::cout << "- [ OK ] " << worker_id << " processing: " << input_frame.frame_id() << std::endl;
        }

        // Проверка наличия изображения в сообщении
        if (!input_frame.has_single_image()) {
            failed_count++;  // Увеличение счетчика ошибок
            return false;
        }

        cv::Mat original_image;
        try {
            // Извлечение изображения из сообщения
            original_image = extract_image(input_frame.single_image());
        }
        catch (const std::exception& e) {
            failed_count++;  // Увеличение счетчика ошибок
            return false;
        }

        // Проверка валидности изображения
        if (original_image.empty()) {
            failed_count++;
            return false;
        }

        cv::Mat processed_image;
        try {
            // Применение визуального эффекта к изображению
            processed_image = effect.applyEffect(original_image);
        }
        catch (const std::exception& e) {
            failed_count++;
            return false;
        }

        video_processing::VideoFrame output_frame;
        // Копирование метаданных из входного сообщения
        output_frame.set_frame_id(input_frame.frame_id());
        output_frame.set_timestamp(input_frame.timestamp());
        output_frame.set_sender_id(worker_id);
        output_frame.set_frame_type(video_processing::PROCESSED_FRAME);

        // Создание пары изображений (оригинал + обработанное)
        auto* image_pair = output_frame.mutable_image_pair();
        *image_pair->mutable_original() = create_image_data(original_image);
        *image_pair->mutable_processed() = create_image_data(processed_image);

        // Отправка результата и обновление статистики
        if (send_to_composer(output_frame)) {
            processed_count++;  // Успешная обработка
            return true;
        }
        else {
            failed_count++;  // Ошибка отправки
            return false;
        }
    }

    // Отправка результата в Composer
    bool send_to_composer(const video_processing::VideoFrame& output_frame) {
        try {
            // Сериализация protobuf сообщения
            std::string serialized = output_frame.SerializeAsString();
            // Создание ZeroMQ сообщения
            zmq::message_t output_message(serialized.size());
            // Копирование данных в сообщение
            memcpy(output_message.data(), serialized.data(), serialized.size());

            // Отправка сообщения без блокировки
            return push_socket.send(output_message, ZMQ_DONTWAIT);
        }
        catch (const std::exception& e) {
            return false;
        }
    }

    // Вывод статистики производительности
    void show_statistics() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
        double fps = (elapsed.count() > 0) ? processed_count / elapsed.count() : 0;

        std::cout << "=== Worker " << worker_id << ": "
            << processed_count << " processed, "  // Обработано кадров
            << failed_count << " failed, "        // Ошибок
            << std::fixed << std::setprecision(1) << fps << " fps"  // FPS
            << std::endl;
    }

public:
    // Основной цикл работы воркера
    void run() {
        std::cout << "=== Worker Started ===" << std::endl;
        std::cout << std::endl << "=== 5-PULL-PUSH with dynamic balancing ===" << std::endl << std::endl;
        std::cout << "PULL pattern: Automatic load balancing" << std::endl;
        std::cout << "4. Listening from: " << capturer_address << std::endl;  // Адрес источника
        std::cout << "5. Sending to: " << composer_address << std::endl;      // Адрес назначения

        // Настройка poll для асинхронного приема сообщений
        zmq::pollitem_t items[] = {
            { static_cast<void*>(pull_socket), 0, ZMQ_POLLIN, 0 }  // Ожидание входящих сообщений
        };

        // Основной цикл обработки
        while (!stop_requested) {
            // ждем кадр с таймаутом (100ms)
            zmq::poll(items, 1, 100);

            // Проверка наличия входящего сообщения
            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t message;
                // Неблокирующее получение сообщения
                if (pull_socket.recv(&message, ZMQ_DONTWAIT)) {
                    video_processing::VideoFrame input_frame;
                    // Парсинг protobuf сообщения
                    if (input_frame.ParseFromArray(message.data(), message.size())) {
                        process_frame(input_frame);  // Обработка кадра
                    }
                }
            }

            // Вывод статистики каждые 50 обработанных кадров
            if (processed_count > 0 && processed_count % 50 == 0) {
                show_statistics();
            }
        }
             
    }
};

int main() {
    try {
        PullWorker worker;  // Создание воркера
        worker.run();       // Запуск основного цикла
        return 0;
    }
    catch (const std::exception& e) {
        std::cout << "- [FAIL] Worker error: " << e.what() << std::endl;
        return -1;
    }
}