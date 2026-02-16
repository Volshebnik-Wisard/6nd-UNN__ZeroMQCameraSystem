#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include "video_processing.pb.h"
#include "scanner_darkly_effect.hpp"
#include ".\video_addresses.h"
#include <direct.h>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <process.h> // Для _getpid

class Worker {
private:
    zmq::context_t context;  // Контекст ZeroMQ для управления сокетами
    zmq::socket_t dealer_socket;  // DEALER сокет для подключения к Capturer
    zmq::socket_t push_socket;    // PUSH сокет для отправки результатов в Composer
    std::string worker_id;        // Уникальный идентификатор Worker'а
    //std::string temp_dir;         // Временная директория для сохранения файлов
    ScannerDarklyEffect effect;   // Объект для применения визуального эффекта
    std::string capturer_address; // Адрес Capturer'а
    std::string composer_address; // Адрес Composer'а
    uint64_t processed_count;     // Счетчик успешно обработанных кадров
    uint64_t failed_count;        // Счетчик неудачных обработок
    std::chrono::steady_clock::time_point start_time; // Время начала работы
    std::atomic<bool> stop_requested; // Флаг для запроса остановки

public:
    Worker() : context(1),  // Инициализация контекста ZeroMQ с 1 IO thread
        dealer_socket(context, ZMQ_DEALER),  // Инициализация DEALER сокета
        push_socket(context, ZMQ_PUSH),      // Инициализация PUSH сокета
        processed_count(0), failed_count(0), stop_requested(false) { // Инициализация счетчиков и флагов

        std::cout << "=== Worker Initialization ===" << std::endl;
        std::cout << "1. Available capturer network interfaces:" << std::endl;

        // Генерируем уникальный ID для Worker'а (используем _getpid вместо getpid)
        worker_id = "worker_" + std::to_string(_getpid()) + "_" +  // Базовый ID с PID
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()); // Добавляем временную метку

        // Устанавливаем идентификатор для DEALER сокета
        dealer_socket.setsockopt(ZMQ_IDENTITY, worker_id.c_str(), worker_id.size());

        // Настраиваем High Water Mark (максимальный размер очереди)
        int rcvhwm = 1;  // Маленький буфер - берем задания когда готовы
        dealer_socket.setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));

        // Подключение к Capturer (DEALER)
        // Попытка подключения к одному из адресов
        for (const auto& address : worker_to_capturer_connect_addresses) {
            try {
                dealer_socket.connect(address);  // Пытаемся подключиться
                capturer_address = address;      // Сохраняем успешный адрес
                std::cout << "- [ OK ] DEALER connected to Capturer: " << address << std::endl;
                break;  // Выходим из цикла при успешном подключении
            }
            catch (const zmq::error_t& e) {  // Обработка ошибок подключения
                std::cout << "- [FAIL] Failed to connect DEALER to " << address << ": " << e.what() << std::endl;
            }
        }

        // Подключение к Composer (PUSH)
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

        // Настройка эффекта Scanner Darkly
        effect.setCannyThresholds(effect_canny_low_threshold, effect_canny_high_threshold);        // Пороги для детектора границ Кэнни
        effect.setGaussianKernelSize(effect_gaussian_kernel_size);           // Размер ядра размытия Гаусса
        effect.setDilationKernelSize(effect_dilation_kernel_size);           // Размер ядра дилатации (0 = нет дилатации)
        effect.setColorQuantizationLevels(effect_color_quantization_levels);      // Уровни квантования цвета
        effect.setBlackContours(effect_black_contours);             // Использовать черные контуры

        start_time = std::chrono::steady_clock::now();  // Запоминаем время начала

        // Вывод информации о Worker'е
        std::cout << "2. Worker initialized with Scanner Darkly effect" << std::endl;
        std::cout << "3. Worker ID: " << worker_id << std::endl;
        std::cout << "======================================================" << std::endl;
    }

    // Деструктор - устанавливает флаг остановки
    ~Worker() {
        stop_requested = true;  // Устанавливаем флаг остановки
    }

private:
    // Извлечение изображения из protobuf сообщения
    cv::Mat extract_image(const video_processing::ImageData& image_data) {
        const std::string& data = image_data.image_data();  // Получаем бинарные данные
        std::vector<uchar> buffer(data.begin(), data.end());  // Конвертируем в вектор байт

        // Проверяем формат кодирования
        if (image_data.encoding() == proto_image_encoding) {
            // Декодируем JPEG изображение
            cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
            if (decoded.empty()) {  // Проверяем успешность декодирования
                throw std::runtime_error("- [FAIL] Failed to decode JPEG image");
            }
            return decoded;  // Возвращаем декодированное изображение
        }
        else {
            // Для RAW формата создаем матрицу из бинарных данных
            return cv::Mat(
                image_data.height(),    // Высота изображения
                image_data.width(),     // Ширина изображения  
                CV_8UC3,                // Формат: 3 канала по 8 бит
                (void*)data.data()      // Указатель на данные
            ).clone();  // Создаем копию данных
        }
    }

    // Создание protobuf сообщения с изображением
    video_processing::ImageData create_image_data(const cv::Mat& image) {
        video_processing::ImageData image_data;  // Создаем объект для данных изображения

        // Кодируем изображение в JPEG
        std::vector<uchar> buffer;  // Буфер для сжатых данных
        std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality };  // Параметры сжатия (качество 80%)
        cv::imencode(".jpg", image, buffer, compression_params);  // Кодируем в JPEG

        // Заполняем поля protobuf сообщения
        image_data.set_width(image.cols);        // Ширина изображения
        image_data.set_height(image.rows);       // Высота изображения
        image_data.set_pixel_format(proto_pixel_format);  // Формат пикселей BGR
        image_data.set_encoding(proto_image_encoding);     // Кодирование JPEG
        image_data.set_image_data(buffer.data(), buffer.size());  // Данные изображения

        return image_data;  // Возвращаем заполненный объект
    }

    // Вывод статистики работы
    void show_statistics() {
        auto now = std::chrono::steady_clock::now();  // Текущее время
        

        // Выводим статистику
        std::cout << "=== Worker " << worker_id << " stats: "
            << processed_count << " processed, "    // Обработано кадров
            << failed_count << " failed "          // Неудачных обработок
            << std::fixed << std::setprecision(1) <<  "" << std::endl;  // FPS с одним знаком после запятой
    }

    // Отправка результата в Composer
    bool send_to_composer(const video_processing::VideoFrame& output_frame) {
        try {
            std::string serialized = output_frame.SerializeAsString();  // Сериализуем protobuf сообщение
            zmq::message_t output_message(serialized.size());  // Создаем ZeroMQ сообщение
            memcpy(output_message.data(), serialized.data(), serialized.size());  // Копируем данные

            // Отправляем сообщение без блокировки
            bool sent = push_socket.send(output_message, ZMQ_DONTWAIT);
            return sent;  // Возвращаем результат отправки
        }
        catch (const std::exception& e) {  // Обработка ошибок
            std::cout << "- [FAIL] Error sending to Composer: " << e.what() << std::endl;
            return false;  // Возвращаем false при ошибке
        }
    }

    // Запрос нового кадра у Capturer'а
    void request_frame() {
        try {
            // Отправляем запрос на получение кадра
            zmq::message_t request(3);  // Создаем сообщение размером 3 байта
            memcpy(request.data(), "GET", 3);  // Копируем строку "GET"
            dealer_socket.send(request, ZMQ_DONTWAIT);  // Отправляем без блокировки
        }
        catch (const zmq::error_t& e) {  // Обработка ошибок ZeroMQ
            if (e.num() != EAGAIN) {  // Игнорируем ошибку "resource temporarily unavailable"
                std::cout << "- [FAIL] Error requesting frame: " << e.what() << std::endl;
            }
        }
    }

public:
    // Основной цикл работы Worker'а
    void run() {
        // Вывод информации о запуске
        std::cout << "=== Worker Started ===" << std::endl;
        std::cout << std::endl << "=== 3-ROUTER-DEALER with frame skips ===" << std::endl << std::endl;
        std::cout << "DEALER pattern: Request-based load balancing. Request frames when ready" << std::endl;
        std::cout << "4. Listening from: " << capturer_address << std::endl;  // Адрес источника
        std::cout << "5. Sending to: " << composer_address << std::endl;      // Адрес назначения


        // Запрашиваем первый кадр
        request_frame();

        // Основной цикл обработки
        while (!stop_requested) {  // Пока не запрошена остановка
            try {
                zmq::message_t message;  // Сообщение для приема данных

                // Проверяем есть ли кадр от Capturer (без блокировки)
                if (dealer_socket.recv(&message, ZMQ_DONTWAIT)) {
                    // Десериализуем сообщение от Capturer
                    video_processing::VideoFrame input_frame;
                    if (!input_frame.ParseFromArray(message.data(), message.size())) {  // Парсим protobuf
                        std::cout << "- [FAIL] Failed to parse message from Capturer" << std::endl;
                        failed_count++;  // Увеличиваем счетчик ошибок
                        // Запрашиваем следующий кадр
                        request_frame();
                        continue;  // Переходим к следующей итерации
                    }

                    // Вывод информации о полученном кадре
                    std::cout << "- [ OK ] " << worker_id << " processing frame: " << input_frame.frame_id() << std::endl;

                    // Извлекаем исходное изображение
                    if (input_frame.has_single_image()) {  // Проверяем наличие изображения
                        cv::Mat original_image;  // Переменная для исходного изображения
                        try {
                            original_image = extract_image(input_frame.single_image());  // Извлекаем изображение
                        }
                        catch (const std::exception& e) {  // Обработка ошибок извлечения
                            std::cout << "- [FAIL] Failed to extract image: " << e.what() << std::endl;
                            failed_count++;  // Увеличиваем счетчик ошибок
                            request_frame();  // Запрашиваем следующий кадр
                            continue;  // Переходим к следующей итерации
                        }

                        // Проверяем что изображение не пустое
                        if (!original_image.empty()) {
                            // Применяем эффект Scanner Darkly
                            cv::Mat processed_image;  // Переменная для обработанного изображения
                            try {
                                processed_image = effect.applyEffect(original_image);  // Применяем эффект
                            }
                            catch (const std::exception& e) {  // Обработка ошибок эффекта
                                std::cout << "- [FAIL] Failed to apply effect: " << e.what() << std::endl;
                                failed_count++;  // Увеличиваем счетчик ошибок
                                request_frame();  // Запрашиваем следующий кадр
                                continue;  // Переходим к следующей итерации
                            }

                            // Создаем сообщение для Composer
                            video_processing::VideoFrame output_frame;
                            output_frame.set_frame_id(input_frame.frame_id());  // Сохраняем ID кадра
                            output_frame.set_timestamp(input_frame.timestamp());  // Сохраняем временную метку
                            output_frame.set_sender_id(worker_id);  // Устанавливаем ID отправителя
                            output_frame.set_frame_type(video_processing::PROCESSED_FRAME);  // Тип: обработанный кадр

                            // Добавляем оба изображения (оригинал и обработанное)
                            auto* image_pair = output_frame.mutable_image_pair();  // Получаем указатель на пару изображений
                            *image_pair->mutable_original() = create_image_data(original_image);  // Добавляем оригинал
                            *image_pair->mutable_processed() = create_image_data(processed_image);  // Добавляем обработанное

                            // Отправляем результат в Composer
                            if (send_to_composer(output_frame)) {  // Если отправка успешна
                                processed_count++;  // Увеличиваем счетчик обработанных
                                std::cout << "- [ OK ] " << worker_id << " sent to Composer: " << output_frame.frame_id() << std::endl;
                            }
                            else {  // Если отправка не удалась
                                failed_count++;  // Увеличиваем счетчик ошибок
                                std::cout << "- [FAIL] " << worker_id << " failed to send: " << output_frame.frame_id() << std::endl;
                            }

                            // Показываем статистику каждые 50 кадров
                            if (processed_count % 50 == 0) {
                                show_statistics();  // Вывод статистики
                            }
                        }
                        else {  // Если изображение пустое
                            failed_count++;  // Увеличиваем счетчик ошибок
                            std::cout << "- [FAIL] Empty image from frame: " << input_frame.frame_id() << std::endl;
                        }
                    }
                    else {  // Если в кадре нет данных изображения
                        failed_count++;  // Увеличиваем счетчик ошибок
                        std::cout << "- [FAIL] Frame has no image data: " << input_frame.frame_id() << std::endl;
                    }

                    // Запрашиваем следующий кадр сразу после обработки
                    request_frame();
                }
                else {  // Если нет сообщений от Capturer'а
                    // Нет сообщений, небольшая пауза чтобы не нагружать CPU
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            catch (const zmq::error_t& e) {  // Обработка ошибок ZeroMQ
                if (e.num() != EAGAIN && e.num() != EINTR) {  // Игнорируем временные ошибки и прерывания
                    std::cout << "- [FAIL] ZMQ error: " << e.what() << std::endl;
                    failed_count++;  // Увеличиваем счетчик ошибок
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Пауза при ошибке
            }
            catch (const std::exception& e) {  // Обработка общих исключений
                std::cout << "- [FAIL] Processing error: " << e.what() << std::endl;
                failed_count++;  // Увеличиваем счетчик ошибок
                std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Пауза при ошибке
                // Пытаемся запросить следующий кадр после ошибки
                request_frame();
            }
        }

    }
};

// Главная функция программы
int main() {
    try {
        Worker worker;  // Создаем экземпляр Worker'а
        worker.run();  // Запускаем основной цикл
        return 0;  // Успешное завершение
    }
    catch (const std::exception& e) {  // Обработка исключений в main
        std::cout << "- [FAIL] Worker error: " << e.what() << std::endl;
        return -1;  // Завершение с ошибкой
    }
}