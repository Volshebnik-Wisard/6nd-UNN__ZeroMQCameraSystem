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

#pragma warning(disable : 4996)

class Capturer {
private:
    zmq::context_t context;

    // Основной PUSH сокет для отправки кадров Worker'ам
    zmq::socket_t push_socket;

    // PULL сокет для получения подтверждений от Worker'ов
    zmq::socket_t ack_pull_socket;

    cv::VideoCapture cap;
    uint64_t frame_counter;
    std::string sender_id;

    // Статистика
    std::atomic<uint64_t> dropped_frames;
    std::atomic<uint64_t> sent_frames;
    std::atomic<uint64_t> ack_received;
    std::atomic<bool> stop_requested;

    // Контроль потока
    std::mutex flow_mutex;
    std::atomic<bool> waiting_for_ack;
    std::atomic<uint64_t> last_sent_frame_id;

    // Статистика времени
    std::chrono::steady_clock::time_point start_time;
    uint64_t captured_frames;

    // Очередь отправленных кадров, ожидающих подтверждения
    struct PendingFrame {
        uint64_t frame_id;
        std::chrono::steady_clock::time_point sent_time;
    };
    std::vector<PendingFrame> pending_frames;
    std::mutex pending_mutex;

public:
    Capturer() : context(1),
        push_socket(context, ZMQ_PUSH),
        ack_pull_socket(context, ZMQ_PULL),
        frame_counter(0), dropped_frames(0), sent_frames(0),
        ack_received(0), stop_requested(false),
        waiting_for_ack(false), last_sent_frame_id(0),
        captured_frames(0) {

        std::cout << "=== Capturer Initialization (Feedback PUSH-PULL) ===" << std::endl;
        std::cout << "1. Available network interfaces:" << std::endl;

        // Настройка основного PUSH сокета
        int push_hwm = queue_size;  // Минимальный буфер
        push_socket.setsockopt(ZMQ_SNDHWM, &push_hwm, sizeof(push_hwm));

        int immediate = 1;  // Отправлять только если получатель готов
        push_socket.setsockopt(ZMQ_IMMEDIATE, &immediate, sizeof(immediate));

        int linger = 0;
        push_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

        // Настройка ACK PULL сокета
        int ack_hwm = 10;  // Может быть несколько подтверждений в очереди
        ack_pull_socket.setsockopt(ZMQ_RCVHWM, &ack_hwm, sizeof(ack_hwm));

        // Привязка основного сокета
        for (const auto& address : capturer_bind_addresses) {
            try {
                push_socket.bind(address);
                std::cout << "- [ OK ] PUSH bound to: " << address << " (for frames)" << std::endl;
                break;
            }
            catch (const zmq::error_t& e) {
                std::cout << "- [FAIL] Failed to bind PUSH to " << address << ": " << e.what() << std::endl;
            }
        }

        // Привязка ACK сокета (на другом порту)
        std::string ack_address = "tcp://*:5558";  // Порт для подтверждений
        try {
            ack_pull_socket.bind(ack_address);
            std::cout << "- [ OK ] PULL bound to: " << ack_address << " (for ACKs)" << std::endl;
        }
        catch (const zmq::error_t& e) {
            std::cout << "- [FAIL] Failed to bind PULL to " << ack_address << ": " << e.what() << std::endl;
        }

        init_camera();
        sender_id = "capturer_feedback_" + std::to_string(time(nullptr));
        start_time = std::chrono::steady_clock::now();

        std::cout << "2. Camera initialized" << std::endl;
        std::cout << "3. Capturer ID: " << sender_id << std::endl;
        std::cout << "4. Feedback channel: PUSH(frames) → Workers, PULL(ACKs) ← Workers" << std::endl;
        std::cout << "======================================================" << std::endl;
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

    // Попытка отправки кадра с проверкой обратной связи
    bool send_frame_with_feedback(const video_processing::VideoFrame& frame) {
        std::lock_guard<std::mutex> lock(flow_mutex);

        // 1. Проверяем, получили ли мы ACK для предыдущего кадра
        if (waiting_for_ack) {
            // Ждем ACK или таймаут
            if (!wait_for_ack(50)) {  // Таймаут 50ms
                dropped_frames++;
                std::cout << "- [WARN] Waiting for ACK, dropping frame " << frame.frame_id() << std::endl;
                return false;
            }
        }

        // 2. Пытаемся отправить кадр
        try {
            std::string serialized = frame.SerializeAsString();
            zmq::message_t msg(serialized.size());
            memcpy(msg.data(), serialized.data(), serialized.size());

            bool sent = push_socket.send(msg, ZMQ_DONTWAIT);

            if (sent) {
                sent_frames++;
                last_sent_frame_id = frame.frame_id();
                waiting_for_ack = true;

                // Добавляем в очередь ожидания подтверждения
                {
                    std::lock_guard<std::mutex> pending_lock(pending_mutex);
                    pending_frames.push_back({
                        frame.frame_id(),
                        std::chrono::steady_clock::now()
                        });
                }

                std::cout << "- [ OK ] Frame " << frame.frame_id() << " sent, waiting for ACK" << std::endl;
                return true;
            }
        }
        catch (const zmq::error_t& e) {
            if (e.num() == EAGAIN) {
                dropped_frames++;
                std::cout << "- [FAIL] Frame " << frame.frame_id() << " dropped (send buffer full)" << std::endl;
            }
        }
        catch (...) {
            // Игнорируем другие ошибки
        }

        return false;
    }

    // Ожидание подтверждения с таймаутом
    bool wait_for_ack(int timeout_ms) {
        zmq::pollitem_t items[] = {
            { static_cast<void*>(ack_pull_socket), 0, ZMQ_POLLIN, 0 }
        };

        int rc = zmq::poll(items, 1, timeout_ms);

        if (items[0].revents & ZMQ_POLLIN) {
            // Есть подтверждение
            process_acknowledgments();
            return true;
        }

        // Таймаут - проверяем старые ожидающие кадры
        check_pending_timeouts();
        return false;
    }

    // Обработка подтверждений от Worker'ов
    void process_acknowledgments() {
        try {
            while (true) {
                zmq::message_t ack_msg;
                if (!ack_pull_socket.recv(&ack_msg, ZMQ_DONTWAIT)) {
                    break;  // Нет больше сообщений
                }

                std::string ack_str(static_cast<char*>(ack_msg.data()), ack_msg.size());

                if (ack_str.substr(0, 3) == "ACK") {
                    // Парсим ID кадра из ACK сообщения
                    uint64_t acked_frame_id = 0;
                    try {
                        acked_frame_id = std::stoull(ack_str.substr(4));
                    }
                    catch (...) {
                        // Если не можем распарсить, считаем что подтвержден последний кадр
                        acked_frame_id = last_sent_frame_id;
                    }

                    ack_received++;
                    waiting_for_ack = false;

                    // Удаляем из очереди ожидания
                    {
                        std::lock_guard<std::mutex> lock(pending_mutex);
                        pending_frames.erase(
                            std::remove_if(pending_frames.begin(), pending_frames.end(),
                                [acked_frame_id](const PendingFrame& pf) {
                                    return pf.frame_id == acked_frame_id;
                                }),
                            pending_frames.end()
                        );
                    }

                    std::cout << "- [ OK ] ACK received for frame " << acked_frame_id << std::endl;
                }
            }
        }
        catch (...) {
            // Игнорируем ошибки
        }
    }

    // Проверка таймаутов для ожидающих кадров
    void check_pending_timeouts() {
        std::lock_guard<std::mutex> lock(pending_mutex);
        auto now = std::chrono::steady_clock::now();

        for (auto it = pending_frames.begin(); it != pending_frames.end(); ) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->sent_time).count();

            if (elapsed > 1000) {  // Таймаут 1 секунда
                std::cout << "- [WARN] Timeout for frame " << it->frame_id
                    << " (" << elapsed << "ms old)" << std::endl;
                it = pending_frames.erase(it);
                waiting_for_ack = false;  // Сбрасываем ожидание
            }
            else {
                ++it;
            }
        }
    }

    void show_statistics() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

        if (elapsed > 2000) {
            double elapsed_seconds = elapsed / 1000.0;

            double capture_fps = captured_frames / elapsed_seconds;
            double sent_fps = sent_frames / elapsed_seconds;
            double ack_fps = ack_received / elapsed_seconds;
            double drop_rate = (captured_frames > 0) ?
                (double)dropped_frames / captured_frames * 100.0 : 0;

            std::cout << "\n=== Feedback PUSH-PULL Statistics ===" << std::endl;
            std::cout << "Time window: " << std::fixed << std::setprecision(1) << elapsed_seconds << "s" << std::endl;
            std::cout << "Captured frames: " << captured_frames << std::endl;
            std::cout << "Sent frames: " << sent_frames << std::endl;
            std::cout << "ACK received: " << ack_received << std::endl;
            std::cout << "Dropped frames: " << dropped_frames << std::endl;
            std::cout << "Capture rate: " << std::fixed << std::setprecision(1) << capture_fps << " FPS" << std::endl;
            std::cout << "Send rate: " << std::fixed << std::setprecision(1) << sent_fps << " FPS" << std::endl;
            std::cout << "ACK rate: " << std::fixed << std::setprecision(1) << ack_fps << " FPS" << std::endl;
            std::cout << "Drop rate: " << std::fixed << std::setprecision(1) << drop_rate << "%" << std::endl;
            std::cout << "Pending frames: " << pending_frames.size() << std::endl;
            std::cout << "Waiting for ACK: " << (waiting_for_ack ? "YES" : "NO") << std::endl;
            std::cout << "=======================================\n" << std::endl;

            // Сбрасываем статистику
            start_time = now;
            captured_frames = 0;
            sent_frames = 0;
            ack_received = 0;
            dropped_frames = 0;
        }
    }

public:
    void run() {
        std::cout << "=== Capturer Started ===" << std::endl;
        std::cout << "\n=== 6-Feedback PUSH-PULL ===" << std::endl;
        //std::cout << "How it works:" << std::endl;
        //std::cout << "1. Capturer sends frame to Worker (PUSH)" << std::endl;
        //std::cout << "2. Capturer waits for ACK from Worker" << std::endl;
        //std::cout << "3. Worker processes frame and sends ACK back" << std::endl;
        //std::cout << "4. Capturer sends next frame only after receiving ACK" << std::endl;
        //std::cout << "5. If no ACK within timeout → frame considered dropped" << std::endl;
        //std::cout << "\nThis provides true runtime behavior like ROUTER-DEALER!" << std::endl;
        std::cout << "===========================================================\n" << std::endl;

        cv::Mat frame;
        uint64_t total_captured = 0;
        uint64_t total_sent = 0;
        uint64_t total_ack = 0;
        uint64_t total_dropped = 0;
        auto global_start_time = std::chrono::steady_clock::now();

        while (!stop_requested) {
            // 1. Захватываем кадр
            if (cap.read(frame) && !frame.empty()) {
                captured_frames++;
                total_captured++;

                auto video_frame = create_video_frame(frame);

                // 2. Обрабатываем любые подтверждения
                process_acknowledgments();

                // 3. Пытаемся отправить с обратной связью
                if (send_frame_with_feedback(video_frame)) {
                    // Успешно отправлено
                }
                else {
                    // Не отправлено (ждем ACK или буфер полон)
                    total_dropped++;
                }

                // 4. Показываем статистику
                //show_statistics();

                // 5. Контроль скорости (примерно 30 FPS)
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }

            // 6. Проверяем выход
            if (cv::waitKey(1) == 27) {
                stop_requested = true;
            }

            // 7. Короткая пауза
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        cap.release();
        cv::destroyAllWindows();       
    }
};

int main() {
    try {
        Capturer capturer;
        capturer.run();
        return 0;
    }
    catch (const std::exception& e) {
        std::cout << "- [FAIL] Capturer error: " << e.what() << std::endl;
        return -1;
    }
}