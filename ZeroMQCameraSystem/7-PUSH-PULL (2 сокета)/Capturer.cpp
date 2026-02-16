#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include "video_processing.pb.h"
#include "..\video_addresses.h"
#include <chrono>
#include <thread>
#include <atomic>
#include <iomanip>
#include <mutex>
#include <deque>
#include <condition_variable>

#pragma warning(disable : 4996)

class Capturer {
private:
    zmq::context_t context;

    // Основные сокеты PUSH-PULL
    zmq::socket_t data_push_socket;    // Для отправки кадров Worker'ам
    zmq::socket_t feedback_pull_socket; // Для получения статусов готовности

    cv::VideoCapture cap;
    uint64_t frame_counter;
    std::string sender_id;

    // Управление Worker'ами
    std::mutex workers_mutex;
    std::deque<std::string> ready_workers;  // Очередь готовых Worker'ов

    // Очередь кадров для буферизации
    std::mutex queue_mutex;
    std::deque<video_processing::VideoFrame> frame_queue;
    const size_t MAX_QUEUE_SIZE;

    // Статистика
    std::atomic<uint64_t> captured_frames;
    std::atomic<uint64_t> sent_frames;
    std::atomic<uint64_t> dropped_frames;
    std::atomic<uint64_t> queued_frames;
    std::atomic<bool> stop_requested;

    // Время
    std::chrono::steady_clock::time_point start_time;

public:
    Capturer() : context(1),
        data_push_socket(context, ZMQ_PUSH),
        feedback_pull_socket(context, ZMQ_PULL),
        frame_counter(0),
        captured_frames(0),
        sent_frames(0),
        dropped_frames(0),
        queued_frames(0),
        stop_requested(false),
        MAX_QUEUE_SIZE(queue_size) {  // Используем queue_size из video_addresses.h

        std::cout << "=== Capturer Initialization (PUSH-PULL with Queue) ===" << std::endl;
        std::cout << "Queue size: " << MAX_QUEUE_SIZE << " frames" << std::endl;

        // КРИТИЧЕСКИ ВАЖНО: Минимальные буферы
        int hwm = queue_size;  // Используем тот же размер
        data_push_socket.setsockopt(ZMQ_SNDHWM, &hwm, sizeof(hwm));
        feedback_pull_socket.setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));

        // Немедленная отправка
        int immediate = 1;
        data_push_socket.setsockopt(ZMQ_IMMEDIATE, &immediate, sizeof(immediate));

        int linger = 0;
        data_push_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        feedback_pull_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

        // Привязка основного сокета для отправки кадров
        for (const auto& address : capturer_bind_addresses) {
            try {
                data_push_socket.bind(address);
                std::cout << "- [ OK ] PUSH bound to: " << address << " (for frames)" << std::endl;
                break;
            }
            catch (const zmq::error_t& e) {
                std::cout << "- [FAIL] Failed to bind PUSH: " << e.what() << std::endl;
            }
        }

        // Привязка сокета для обратной связи
        const std::vector<std::string> feedback_address = { 
        //"tcp://192.168.9.50:5560",  // 528 - стол - лево
        "tcp://*:5560"            // Все интерфейсы
        };  // Порт для статусов готовности
        for (const auto& address : feedback_address) {
            try {
                feedback_pull_socket.bind(address);
                std::cout << "- [ OK ] PUSH bound to: " << address << " (for frames)" << std::endl;
                break;
            }
            catch (const zmq::error_t& e) {
            }
        }

        init_camera();
        sender_id = "capturer_pp_" + std::to_string(time(nullptr));
        start_time = std::chrono::steady_clock::now();

        std::cout << "Capturer ID: " << sender_id << std::endl;
        std::cout << "PUSH-PULL with frame queue and runtime worker tracking" << std::endl;
        std::cout << "========================================\n" << std::endl;
    }

    ~Capturer() {
        stop_requested = true;
    }

private:
    void init_camera() {
        std::cout << "Searching for camera..." << std::endl;

        cap.open(camera_id);
        if (cap.isOpened()) {
            cap.set(cv::CAP_PROP_FRAME_WIDTH, cap_frame_width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, cap_frame_height);
            cap.set(cv::CAP_PROP_FPS, cap_fps);
            std::cout << "- [ OK ] Camera found at ID: " << camera_id << std::endl;

            double actual_fps = cap.get(cv::CAP_PROP_FPS);
            std::cout << "- [ OK ] Camera FPS: " << actual_fps << std::endl;
            return;
        }

        throw std::runtime_error("- [FAIL] No camera found!");
    }

    video_processing::VideoFrame create_video_frame(const cv::Mat& frame) {
        video_processing::VideoFrame message;
        message.set_frame_id(frame_counter++);
        message.set_timestamp(get_current_time());
        message.set_sender_id(sender_id);
        message.set_frame_type(video_processing::CAPTURED_FRAME);

        std::vector<uchar> buffer;
        std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality };
        cv::imencode(".jpg", frame, buffer, compression_params);

        auto* image_data = message.mutable_single_image();
        image_data->set_width(frame.cols);
        image_data->set_height(frame.rows);
        image_data->set_pixel_format(proto_pixel_format);
        image_data->set_encoding(proto_image_encoding);
        image_data->set_image_data(buffer.data(), buffer.size());

        return message;
    }

    double get_current_time() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration<double>(now.time_since_epoch()).count();
    }

    // Добавление кадра в очередь
    bool add_frame_to_queue(const video_processing::VideoFrame& frame) {
        std::lock_guard<std::mutex> lock(queue_mutex);

        if (frame_queue.size() >= MAX_QUEUE_SIZE) {
            // Очередь полна - удаляем самый старый кадр
            frame_queue.pop_front();
            queued_frames--;
            std::cout << "[QUEUE FULL] Dropped oldest frame from queue" << std::endl;
        }

        frame_queue.push_back(frame);
        queued_frames++;
        return true;
    }

    // Проверка готовых Worker'ов
    void check_worker_status() {
        try {
            zmq::message_t status_msg;
            while (feedback_pull_socket.recv(&status_msg, ZMQ_DONTWAIT)) {
                std::string status_str(static_cast<char*>(status_msg.data()), status_msg.size());

                if (status_str.substr(0, 5) == "READY") {
                    std::string worker_id = status_str.substr(6);  // Пропускаем "READY "

                    std::lock_guard<std::mutex> lock(workers_mutex);
                    ready_workers.push_back(worker_id);

                    std::cout << "[READY] Worker " << worker_id << " is ready" << std::endl;
                }
            }
        }
        catch (const zmq::error_t& e) {
            // Игнорируем временные ошибки
        }
    }

    // Попытка отправить кадр из очереди готовому Worker'у
    void try_send_queued_frames() {
        std::lock_guard<std::mutex> lock1(queue_mutex);
        std::lock_guard<std::mutex> lock2(workers_mutex);

        while (!ready_workers.empty() && !frame_queue.empty()) {
            // Берем первый кадр из очереди
            video_processing::VideoFrame frame = frame_queue.front();

            // Берем первого готового Worker'а
            std::string worker_id = ready_workers.front();
            ready_workers.pop_front();

            try {
                std::string serialized = frame.SerializeAsString();
                zmq::message_t msg(serialized.size());
                memcpy(msg.data(), serialized.data(), serialized.size());

                bool sent = data_push_socket.send(msg, ZMQ_DONTWAIT);

                if (sent) {
                    // Успешно отправили - удаляем кадр из очереди
                    frame_queue.pop_front();
                    queued_frames--;
                    sent_frames++;

                    std::cout << "[SENT] Frame " << frame.frame_id()
                        << " to " << worker_id
                        << " (Queue: " << frame_queue.size() << ")" << std::endl;
                }
                else {
                    // Не удалось отправить - возвращаем Worker в очередь
                    ready_workers.push_front(worker_id);
                    std::cout << "[QUEUE SEND FAIL] Could not send frame " << frame.frame_id()
                        << " (buffer full?)" << std::endl;
                    break;  // Прерываем попытки отправки
                }
            }
            catch (const zmq::error_t& e) {
                if (e.num() == EAGAIN) {
                    // Буфер полон, возвращаем Worker в очередь
                    ready_workers.push_front(worker_id);
                    std::cout << "[QUEUE BUFFER FULL] Buffer full for frame " << frame.frame_id() << std::endl;
                }
                else {
                    // Другая ошибка - возвращаем Worker
                    ready_workers.push_front(worker_id);
                    std::cout << "[QUEUE ERROR] " << e.what() << std::endl;
                }
                break;  // Прерываем попытки отправки
            }
        }
    }

    void show_statistics() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        if (elapsed >= 3) {
            double capture_fps = (elapsed > 0) ? (double)captured_frames / elapsed : 0;
            double send_fps = (elapsed > 0) ? (double)sent_frames / elapsed : 0;

            std::lock_guard<std::mutex> lock1(queue_mutex);
            std::lock_guard<std::mutex> lock2(workers_mutex);

            int ready_count = ready_workers.size();
            int queue_size_val = frame_queue.size();

            std::cout << "\n[STATS] Captured: " << captured_frames
                << " | Sent: " << sent_frames
                << " | Dropped: " << dropped_frames
                << " | Queued: " << queue_size_val
                << " | Ready workers: " << ready_count
                << " | Capture FPS: " << std::fixed << std::setprecision(1) << capture_fps
                << " | Send FPS: " << std::fixed << std::setprecision(1) << send_fps
                << std::endl;

            start_time = now;
            captured_frames = 0;
            sent_frames = 0;
        }
    }

public:
    void run() {
        std::cout << "=== Capturer Started ===" << std::endl;
        std::cout << "\n=== 7-PUSH-PULL with runtime worker tracking ===" << std::endl;
        std::cout << "PUSH-PULL with frame queue (size: " << MAX_QUEUE_SIZE << ")" << std::endl;
        std::cout << "Protocol: Capture → Queue → Send when worker ready\n" << std::endl;

        cv::Mat frame;
        uint64_t last_frame_id = 0;

        while (!stop_requested) {
            // 1. Захватываем кадр
            if (cap.read(frame) && !frame.empty()) {
                captured_frames++;

                auto video_frame = create_video_frame(frame);
                uint64_t current_frame_id = video_frame.frame_id();

                // Проверяем, не пропустили ли мы кадры
                if (last_frame_id > 0 && current_frame_id > last_frame_id + 1) {
                    std::cout << "[WARNING] Skipped " << (current_frame_id - last_frame_id - 1)
                        << " frames during capture" << std::endl;
                }
                last_frame_id = current_frame_id;

                // 2. Добавляем кадр в очередь
                if (!add_frame_to_queue(video_frame)) {
                    dropped_frames++;
                    std::cout << "[DROP] Frame " << video_frame.frame_id()
                        << " (queue full)" << std::endl;
                }
                else {
                    std::cout << "[QUEUE] Frame " << video_frame.frame_id()
                        << " added to queue (size: " << frame_queue.size() << ")" << std::endl;
                }
            }

            // 3. Проверяем статусы Worker'ов
            check_worker_status();

            // 4. Пытаемся отправить кадры из очереди готовым Worker'ам
            try_send_queued_frames();

            // 5. Контроль скорости захвата
            std::this_thread::sleep_for(std::chrono::milliseconds(33));  // ~30 FPS

            // 6. Показываем статистику
            show_statistics();

            // 7. Проверяем выход по ESC
            if (cv::waitKey(1) == 27) {
                stop_requested = true;
            }
        }

        cap.release();
        cv::destroyAllWindows();

        std::cout << "\n=== Capturer Finished ===" << std::endl;
        std::cout << "Final stats - Captured: " << captured_frames
            << " | Sent: " << sent_frames
            << " | Dropped: " << dropped_frames
            << " | Remaining in queue: " << frame_queue.size() << std::endl;
    }
};

int main() {
    try {
        Capturer capturer;
        capturer.run();
        return 0;
    }
    catch (const std::exception& e) {
        std::cout << "[FAIL] Capturer error: " << e.what() << std::endl;
        return -1;
    }
}