#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include "video_processing.pb.h"
#include "scanner_darkly_effect.hpp"
#include "..\video_addresses.h"
#include <direct.h>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <process.h>
#include <sstream>

class Worker {
private:
    zmq::context_t context;

    // PULL сокет для получения кадров от Capturer
    zmq::socket_t pull_socket;

    // PUSH сокет для отправки результатов в Composer
    zmq::socket_t push_socket;

    // PUSH сокет для отправки подтверждений Capturer'у
    zmq::socket_t ack_push_socket;

    // REQ сокет для регистрации и heartbeat
    zmq::socket_t req_socket;

    std::string worker_id;
    ScannerDarklyEffect effect;

    // Адреса
    std::string capturer_address;
    std::string composer_address;
    std::string capturer_ack_address;
    std::string capturer_control_address;

    // Статистика
    std::atomic<uint64_t> processed_count;
    std::atomic<uint64_t> failed_count;
    std::atomic<uint64_t> ack_sent_count;
    std::atomic<uint64_t> heartbeat_count;
    std::atomic<bool> stop_requested;
    std::atomic<bool> registered;

    // Время обработки
    std::chrono::steady_clock::time_point start_time;
    double average_processing_time_ms;
    uint64_t frames_for_average;

    // Heartbeat поток
    std::thread heartbeat_thread;

public:
    Worker() : context(1),
        pull_socket(context, ZMQ_PULL),
        push_socket(context, ZMQ_PUSH),
        ack_push_socket(context, ZMQ_PUSH),
        req_socket(context, ZMQ_REQ),
        processed_count(0), failed_count(0), ack_sent_count(0),
        heartbeat_count(0), stop_requested(false), registered(false),
        average_processing_time_ms(0), frames_for_average(0) {

        std::cout << "=== Worker Initialization (Hybrid PUSH-PULL with Feedback) ===" << std::endl;
        std::cout << "1. Configuring feedback channels..." << std::endl;

        // Генерация ID Worker'а
        worker_id = "worker_hybrid_" + std::to_string(_getpid()) + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

        // Настройка PULL сокета (кадры от Capturer) - ОЧЕНЬ маленький буфер!
        int pull_hwm = 1;
        pull_socket.setsockopt(ZMQ_RCVHWM, &pull_hwm, sizeof(pull_hwm));
        int pull_linger = 0;
        pull_socket.setsockopt(ZMQ_LINGER, &pull_linger, sizeof(pull_linger));

        // Настройка PUSH сокета (результаты в Composer)
        int push_hwm = 1;
        push_socket.setsockopt(ZMQ_SNDHWM, &push_hwm, sizeof(push_hwm));
        int immediate = 1;
        push_socket.setsockopt(ZMQ_IMMEDIATE, &immediate, sizeof(immediate));

        // Настройка ACK PUSH сокета (подтверждения Capturer'у)
        int ack_linger = 0;
        ack_push_socket.setsockopt(ZMQ_LINGER, &ack_linger, sizeof(ack_linger));

        // Настройка REQ сокета для контроля
        int req_timeout = 500; // 0.5 секунда таймаута
        req_socket.setsockopt(ZMQ_RCVTIMEO, &req_timeout, sizeof(req_timeout));
        req_socket.setsockopt(ZMQ_LINGER, &ack_linger, sizeof(ack_linger));

        // Подключение к Capturer (получение кадров)
        bool capturer_connected = false;
        for (const auto& address : worker_to_capturer_connect_addresses) {
            try {
                pull_socket.connect(address);
                capturer_address = address;
                std::cout << "- [ OK ] PULL connected to Capturer (data): " << address << std::endl;
                capturer_connected = true;
                break;
            }
            catch (const zmq::error_t& e) {
                std::cout << "- [FAIL] Failed to connect PULL to " << address << ": " << e.what() << std::endl;
            }
        }

        if (!capturer_connected) {
            throw std::runtime_error("Failed to connect to Capturer data channel");
        }

        // Подключение к Composer (отправка результатов)
        bool composer_connected = false;
        for (const auto& address : worker_to_composer_connect_addresses) {
            try {
                push_socket.connect(address);
                composer_address = address;
                std::cout << "- [ OK ] PUSH connected to Composer: " << address << std::endl;
                composer_connected = true;
                break;
            }
            catch (const zmq::error_t& e) {
                std::cout << "- [FAIL] Failed to connect PUSH to " << address << ": " << e.what() << std::endl;
            }
        }

        if (!composer_connected) {
            throw std::runtime_error("Failed to connect to Composer");
        }

        // Подключение для ACK (обратно к Capturer)
        capturer_ack_address = "tcp://localhost:5558";  // Порт для ACK
        try {
            ack_push_socket.connect(capturer_ack_address);
            std::cout << "- [ OK ] ACK PUSH connected to Capturer: " << capturer_ack_address << std::endl;
        }
        catch (const zmq::error_t& e) {
            std::cout << "- [FAIL] Failed to connect ACK to " << capturer_ack_address << ": " << e.what() << std::endl;
        }

        // Подключение для контроля (регистрация и heartbeat)
        capturer_control_address = "tcp://localhost:5559";  // Порт для контроля
        try {
            req_socket.connect(capturer_control_address);
            std::cout << "- [ OK ] REQ connected to Capturer (control): " << capturer_control_address << std::endl;
        }
        catch (const zmq::error_t& e) {
            std::cout << "- [WARN] Failed to connect REQ to " << capturer_control_address
                << ": " << e.what() << " (heartbeat disabled)" << std::endl;
        }

        // Настройка эффекта
        effect.setCannyThresholds(effect_canny_low_threshold, effect_canny_high_threshold);
        effect.setGaussianKernelSize(effect_gaussian_kernel_size);
        effect.setDilationKernelSize(effect_dilation_kernel_size);
        effect.setColorQuantizationLevels(effect_color_quantization_levels);
        effect.setBlackContours(effect_black_contours);

        start_time = std::chrono::steady_clock::now();

        std::cout << "2. Worker initialized with Scanner Darkly effect" << std::endl;
        std::cout << "3. Worker ID: " << worker_id << std::endl;
        std::cout << "======================================================" << std::endl;

        // Регистрация Worker'а
        register_with_capturer();

        // Запуск heartbeat потока
        heartbeat_thread = std::thread(&Worker::heartbeat_loop, this);
    }

    ~Worker() {
        stop_requested = true;
        if (heartbeat_thread.joinable()) {
            heartbeat_thread.join();
        }
    }

private:
    void register_with_capturer() {
        try {
            std::string register_msg = "REGISTER " + worker_id;
            zmq::message_t request(register_msg.size());
            memcpy(request.data(), register_msg.data(), register_msg.size());

            if (req_socket.send(request, ZMQ_DONTWAIT)) {
                zmq::message_t reply;
                if (req_socket.recv(&reply, ZMQ_DONTWAIT)) {
                    std::string reply_str(static_cast<char*>(reply.data()), reply.size());
                    if (reply_str == "OK") {
                        registered = true;
                        std::cout << "- [ OK ] Registered with Capturer as: " << worker_id << std::endl;
                    }
                }
            }
        }
        catch (...) {
            // Игнорируем ошибки регистрации
        }
    }

    void heartbeat_loop() {
        while (!stop_requested) {
            if (registered) {
                try {
                    std::string heartbeat_msg = "HEARTBEAT " + worker_id;
                    zmq::message_t request(heartbeat_msg.size());
                    memcpy(request.data(), heartbeat_msg.data(), heartbeat_msg.size());

                    if (req_socket.send(request, ZMQ_DONTWAIT)) {
                        zmq::message_t reply;
                        if (req_socket.recv(&reply, ZMQ_DONTWAIT)) {
                            heartbeat_count++;
                        }
                    }
                }
                catch (...) {
                    // Игнорируем heartbeat ошибки
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    cv::Mat extract_image(const video_processing::ImageData& image_data) {
        const std::string& data = image_data.image_data();
        std::vector<uchar> buffer(data.begin(), data.end());

        if (image_data.encoding() == proto_image_encoding) {
            cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
            if (decoded.empty()) {
                throw std::runtime_error("- [FAIL] Failed to decode JPEG image");
            }
            return decoded;
        }
        else {
            return cv::Mat(
                image_data.height(),
                image_data.width(),
                CV_8UC3,
                (void*)data.data()
            ).clone();
        }
    }

    video_processing::ImageData create_image_data(const cv::Mat& image) {
        video_processing::ImageData image_data;

        std::vector<uchar> buffer;
        std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality };
        cv::imencode(".jpg", image, buffer, compression_params);

        image_data.set_width(image.cols);
        image_data.set_height(image.rows);
        image_data.set_pixel_format(proto_pixel_format);
        image_data.set_encoding(proto_image_encoding);
        image_data.set_image_data(buffer.data(), buffer.size());

        return image_data;
    }

    // Отправка подтверждения Capturer'у
    bool send_ack_to_capturer(uint64_t frame_id) {
        try {
            std::string ack_message = "ACK " + std::to_string(frame_id) + " " + worker_id;
            zmq::message_t ack_msg(ack_message.size());
            memcpy(ack_msg.data(), ack_message.data(), ack_message.size());

            bool sent = ack_push_socket.send(ack_msg, ZMQ_DONTWAIT);

            if (sent) {
                ack_sent_count++;
                return true;
            }
        }
        catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) {
                // Логируем только не-EAGAIN ошибки
            }
        }
        catch (...) {
            // Игнорируем другие ошибки
        }

        return false;
    }

    // Попытка отправить результат в Composer
    bool send_to_composer(const video_processing::VideoFrame& output_frame) {
        try {
            std::string serialized = output_frame.SerializeAsString();
            zmq::message_t msg(serialized.size());
            memcpy(msg.data(), serialized.data(), serialized.size());

            return push_socket.send(msg, ZMQ_DONTWAIT);
        }
        catch (const zmq::error_t& e) {
            return false;
        }
        catch (...) {
            return false;
        }
    }

    void update_processing_time_stats(double processing_time_ms) {
        if (frames_for_average < 10) {
            frames_for_average++;
            average_processing_time_ms = (average_processing_time_ms * (frames_for_average - 1) + processing_time_ms) / frames_for_average;
        }
        else {
            average_processing_time_ms = average_processing_time_ms * 0.9 + processing_time_ms * 0.1;
        }
    }

    void show_statistics() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        if (elapsed >= 5) {
            double fps = (elapsed > 0) ? (double)processed_count / elapsed : 0;

            std::cout << "\n=== Hybrid Worker Statistics ===" << std::endl;
            std::cout << "Worker: " << worker_id << std::endl;
            std::cout << "Status: " << (registered ? "REGISTERED" : "UNREGISTERED") << std::endl;
            std::cout << "Time window: " << elapsed << " seconds" << std::endl;
            std::cout << "Frames processed: " << processed_count << std::endl;
            std::cout << "Frames failed: " << failed_count << std::endl;
            std::cout << "ACK sent: " << ack_sent_count << std::endl;
            std::cout << "Heartbeats: " << heartbeat_count << std::endl;
            std::cout << "Processing rate: " << std::fixed << std::setprecision(2) << fps << " FPS" << std::endl;
            std::cout << "Avg processing time: " << std::fixed << std::setprecision(1)
                << average_processing_time_ms << " ms/frame" << std::endl;

            if (fps > 0 && average_processing_time_ms > 0) {
                double efficiency = 1000.0 / (fps * average_processing_time_ms) * 100;
                std::cout << "System efficiency: " << std::fixed << std::setprecision(1)
                    << efficiency << "%" << std::endl;
            }

            std::cout << "================================\n" << std::endl;

            // Сбрасываем статистику для следующего окна
            start_time = now;
            processed_count = 0;
            failed_count = 0;
            ack_sent_count = 0;
        }
    }

    // Анализ ACK сообщения (для отладки)
    void analyze_ack_pattern() {
        static uint64_t last_frame_id = 0;
        static int consecutive_acks = 0;
        static int missed_acks = 0;

        if (processed_count > 0 && last_frame_id > 0) {
            if (ack_sent_count == consecutive_acks + 1) {
                consecutive_acks++;
            }
            else {
                missed_acks++;
                if (missed_acks % 10 == 0) {
                    std::cout << "- [WARN] ACK pattern: " << consecutive_acks
                        << " consecutive, " << missed_acks << " missed" << std::endl;
                }
            }
        }

        last_frame_id = processed_count;
    }

public:
    void run() {
        std::cout << "=== Worker Started ===" << std::endl;
        std::cout << "\n=== 6-Feedback PUSH-PULL ===" << std::endl;
        //std::cout << "This implementation provides:" << std::endl;
        //std::cout << "1. PUSH-PULL efficiency for data transfer" << std::endl;
        //std::cout << "2. ROUTER-DEALER-like feedback mechanism" << std::endl;
        //std::cout << "3. True runtime flow control" << std::endl;
        //std::cout << "4. No internal buffering issues" << std::endl;
        //std::cout << "5. Frame-by-frame processing guarantee" << std::endl;
        //std::cout << "\nProtocol flow:" << std::endl;
        //std::cout << "1. Capturer sends frame via PUSH" << std::endl;
        //std::cout << "2. Worker receives via PULL, sends ACK via PUSH" << std::endl;
        //std::cout << "3. Capturer waits for ACK before next send" << std::endl;
        //std::cout << "4. Worker processes frame, sends to Composer" << std::endl;
        //std::cout << "5. Repeat with proper backpressure" << std::endl;
        std::cout << "==================================================\n" << std::endl;

        uint64_t total_processed = 0;
        uint64_t total_failed = 0;
        uint64_t total_ack_sent = 0;
        auto global_start_time = std::chrono::steady_clock::now();

        // Ожидаем регистрации
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::cout << "- [ OK ] Worker ready. Waiting for frames from Capturer..." << std::endl;
        std::cout << "Expected processing rate: ~" << (1000.0 / average_processing_time_ms)
            << " FPS (based on " << average_processing_time_ms << "ms/frame)\n" << std::endl;

        while (!stop_requested) {
            try {
                // 1. Проверяем наличие кадров с минимальным таймаутом
                zmq::pollitem_t items[] = {
                    { static_cast<void*>(pull_socket), 0, ZMQ_POLLIN, 0 }
                };

                int rc = zmq::poll(items, 1, 5);  // 5ms таймаут для быстрого реагирования

                if (items[0].revents & ZMQ_POLLIN) {
                    auto frame_start_time = std::chrono::steady_clock::now();

                    // 2. Получаем кадр
                    zmq::message_t message;
                    if (pull_socket.recv(&message, ZMQ_DONTWAIT)) {
                        video_processing::VideoFrame input_frame;
                        if (input_frame.ParseFromArray(message.data(), message.size())) {
                            uint64_t frame_id = input_frame.frame_id();

                            std::cout << "- [ OK ] [FRAME " << frame_id << "] Received from Capturer" << std::endl;

                            // 3. НЕМЕДЛЕННО отправляем ACK обратно
                            bool ack_sent = send_ack_to_capturer(frame_id);

                            if (ack_sent) {
                                std::cout << "- [ OK ] [FRAME " << frame_id << "] ACK sent to Capturer" << std::endl;
                            }
                            else {
                                std::cout << "- [FAIL] [FRAME " << frame_id << "] Failed to send ACK" << std::endl;
                            }

                            // 4. Обрабатываем кадр
                            if (input_frame.has_single_image()) {
                                try {
                                    cv::Mat original_image = extract_image(input_frame.single_image());

                                    if (!original_image.empty()) {
                                        // 5. Применяем эффект (это занимает время)
                                        cv::Mat processed_image = effect.applyEffect(original_image);

                                        // 6. Создаем сообщение для Composer
                                        video_processing::VideoFrame output_frame;
                                        output_frame.set_frame_id(frame_id);
                                        output_frame.set_timestamp(input_frame.timestamp());
                                        output_frame.set_sender_id(worker_id);
                                        output_frame.set_frame_type(video_processing::PROCESSED_FRAME);

                                        auto* image_pair = output_frame.mutable_image_pair();
                                        *image_pair->mutable_original() = create_image_data(original_image);
                                        *image_pair->mutable_processed() = create_image_data(processed_image);

                                        // 7. Отправляем результат в Composer
                                        if (send_to_composer(output_frame)) {
                                            processed_count++;
                                            total_processed++;

                                            auto frame_end_time = std::chrono::steady_clock::now();
                                            auto processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                frame_end_time - frame_start_time).count();

                                            update_processing_time_stats((double)processing_time);

                                            std::cout << "- [ OK ] [FRAME " << frame_id << "] Sent to Composer ("
                                                << processing_time << "ms total)" << std::endl;

                                            // Анализируем паттерн ACK
                                            analyze_ack_pattern();
                                        }
                                        else {
                                            failed_count++;
                                            total_failed++;
                                            std::cout << "- [FAIL] [FRAME " << frame_id << "] Failed to send to Composer" << std::endl;
                                        }
                                    }
                                    else {
                                        failed_count++;
                                        total_failed++;
                                        std::cout << "- [FAIL] [FRAME " << frame_id << "] Empty image" << std::endl;
                                    }
                                }
                                catch (const std::exception& e) {
                                    failed_count++;
                                    total_failed++;
                                    std::cout << "- [FAIL] [FRAME " << frame_id << "] Processing error: " << e.what() << std::endl;
                                }
                            }
                            else {
                                failed_count++;
                                total_failed++;
                                std::cout << "- [FAIL] [FRAME " << frame_id << "] No image data" << std::endl;
                            }
                        }
                        else {
                            failed_count++;
                            total_failed++;
                            std::cout << "- [FAIL] Failed to parse frame message" << std::endl;
                        }
                    }
                }

                // 8. Показываем статистику
                //show_statistics();

                // 9. Короткая пауза для снижения нагрузки на CPU
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

            }
            catch (const std::exception& e) {
                std::cout << "- [FAIL] Runtime error: " << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
};

int main() {
    try {
        Worker worker;
        worker.run();
        return 0;
    }
    catch (const std::exception& e) {
        std::cout << "- [FAIL] Worker error: " << e.what() << std::endl;
        return -1;
    }
}