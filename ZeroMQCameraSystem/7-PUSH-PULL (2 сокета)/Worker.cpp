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

class Worker {
private:
	zmq::context_t context;

	// Основные сокеты PUSH-PULL
	zmq::socket_t data_pull_socket;    // Для получения кадров от Capturer
	zmq::socket_t data_push_socket;    // Для отправки результатов в Composer

	// Сокет для обратной связи (сообщает о своей готовности)
	zmq::socket_t feedback_push_socket; // PUSH для отправки статуса Capturer'у

	std::string worker_id;
	ScannerDarklyEffect effect;

	// Статистика
	std::atomic<uint64_t> processed_count;
	std::atomic<uint64_t> failed_count;
	std::atomic<uint64_t> ready_signals_sent;
	std::atomic<bool> stop_requested;
	std::atomic<bool> is_ready;

	// Время обработки
	std::chrono::steady_clock::time_point start_time;
	double avg_processing_time_ms;

public:
	Worker() : context(1),
		data_pull_socket(context, ZMQ_PULL),
		data_push_socket(context, ZMQ_PUSH),
		feedback_push_socket(context, ZMQ_PUSH),
		processed_count(0), failed_count(0), ready_signals_sent(0),
		stop_requested(false), is_ready(true), avg_processing_time_ms(0) {

		std::cout << "=== Worker Initialization (PUSH-PULL with Runtime) ===" << std::endl;

		// Генерация ID Worker'а
		worker_id = "worker_pp_" + std::to_string(_getpid()) + "_" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

		// КРИТИЧЕСКИ ВАЖНО: Минимальные буферы для PUSH-PULL
		int hwm = 1;  // БУФЕР РАЗМЕРОМ ВСЕГО 1 СООБЩЕНИЕ!
		data_pull_socket.setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));
		data_push_socket.setsockopt(ZMQ_SNDHWM, &hwm, sizeof(hwm));

		// Немедленная отправка/прием
		int immediate = 1;
		data_push_socket.setsockopt(ZMQ_IMMEDIATE, &immediate, sizeof(immediate));

		int linger = 0;
		data_pull_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
		data_push_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
		feedback_push_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

		// Подключение к Capturer для получения кадров
		bool data_connected = false;
		for (const auto& address : worker_to_capturer_connect_addresses) {
			try {
				data_pull_socket.connect(address);
				std::cout << "- [ OK ] PULL connected to Capturer: " << address << std::endl;
				data_connected = true;
				break;
			}
			catch (const zmq::error_t& e) {
				std::cout << "- [FAIL] Failed to connect PULL: " << e.what() << std::endl;
			}
		}

		if (!data_connected) {
			throw std::runtime_error("Failed to connect to Capturer data channel");
		}

		// Подключение к Composer для отправки результатов
		bool composer_connected = false;
		for (const auto& address : worker_to_composer_connect_addresses) {
			try {
				data_push_socket.connect(address);
				std::cout << "- [ OK ] PUSH connected to Composer: " << address << std::endl;
				composer_connected = true;
				break;
			}
			catch (const zmq::error_t& e) {
				std::cout << "- [FAIL] Failed to connect PUSH: " << e.what() << std::endl;
			}
		}

		if (!composer_connected) {
			throw std::runtime_error("Failed to connect to Composer");
		}

		// Подключение для обратной связи (сообщаем о готовности)
		const std::vector<std::string> feedback_address = {
		"tcp://192.168.9.50:5560"  // 528 - стол - лево
	    //"tcp://*:5560"            // Все интерфейсы
		};  // Порт для статусов готовности
		for (const auto& address : feedback_address) {
			try {
				feedback_push_socket.connect(address);
				std::cout << "- [ OK ] PUSH connected to Composer: " << address << std::endl;
				break;
			}
			catch (const zmq::error_t& e) {
			}
		}

		// Настройка эффекта
		effect.setCannyThresholds(effect_canny_low_threshold, effect_canny_high_threshold);
		effect.setGaussianKernelSize(effect_gaussian_kernel_size);
		effect.setDilationKernelSize(effect_dilation_kernel_size);
		effect.setColorQuantizationLevels(effect_color_quantization_levels);
		effect.setBlackContours(effect_black_contours);

		start_time = std::chrono::steady_clock::now();

		std::cout << "Worker ID: " << worker_id << std::endl;
		std::cout << "PUSH-PULL with runtime feedback" << std::endl;
		std::cout << "========================================\n" << std::endl;
	}

	~Worker() {
		stop_requested = true;
	}

private:
	cv::Mat extract_image(const video_processing::ImageData& image_data) {
		const std::string& data = image_data.image_data();
		std::vector<uchar> buffer(data.begin(), data.end());

		if (image_data.encoding() == proto_image_encoding) {
			cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
			if (decoded.empty()) {
				throw std::runtime_error("- [FAIL] Failed to decode JPEG");
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

	// Отправка сигнала о готовности Capturer'у
	void send_ready_signal() {
		if (!is_ready.load()) return;

		try {
			std::string ready_msg = "READY " + worker_id;
			zmq::message_t msg(ready_msg.size());
			memcpy(msg.data(), ready_msg.data(), ready_msg.size());

			bool sent = feedback_push_socket.send(msg, ZMQ_DONTWAIT);
			if (sent) {
				ready_signals_sent++;
				is_ready = false;  // Теперь мы заняты обработкой
			}
		}
		catch (...) {
			// Игнорируем ошибки отправки статуса
		}
	}

	// Отправка результата в Composer
	bool send_to_composer(const video_processing::VideoFrame& frame) {
		try {
			std::string serialized = frame.SerializeAsString();
			zmq::message_t msg(serialized.size());
			memcpy(msg.data(), serialized.data(), serialized.size());

			return data_push_socket.send(msg, ZMQ_DONTWAIT);
		}
		catch (...) {
			return false;
		}
	}

	void show_statistics() {
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

		if (elapsed >= 3) {
			double fps = (elapsed > 0) ? (double)processed_count / elapsed : 0;

			std::cout << "\n[STATS] Worker " << worker_id
				<< " | Processed: " << processed_count
				<< " | Failed: " << failed_count
				<< " | Ready signals: " << ready_signals_sent
				<< " | FPS: " << std::fixed << std::setprecision(1) << fps
				<< " | Avg time: " << std::fixed << std::setprecision(1) << avg_processing_time_ms << "ms"
				<< std::endl;

			start_time = now;
			processed_count = 0;
			failed_count = 0;
		}
	}

public:
	void run() {
		std::cout << "=== Worker Started ===" << std::endl;
		std::cout << "\n=== 7-PUSH-PULL with runtime worker tracking ===" << std::endl;
		std::cout << "Protocol: Send READY signal → Receive frame → Process → Send result\n" << std::endl;

		// Отправляем первый сигнал о готовности
		send_ready_signal();

		while (!stop_requested) {
			try {
				// 1. Проверяем наличие кадра с очень коротким таймаутом
				zmq::pollitem_t items[] = {
					{ static_cast<void*>(data_pull_socket), 0, ZMQ_POLLIN, 0 }
				};

				// Используем poll с коротким таймаутом для быстрого реагирования
				int rc = zmq::poll(items, 1, 10);  // 10ms таймаут

				if (items[0].revents & ZMQ_POLLIN) {
					// Есть кадр для обработки
					auto process_start = std::chrono::steady_clock::now();

					zmq::message_t message;
					if (data_pull_socket.recv(&message, ZMQ_DONTWAIT)) {
						video_processing::VideoFrame input_frame;
						if (input_frame.ParseFromArray(message.data(), message.size())) {
							uint64_t frame_id = input_frame.frame_id();

							std::cout << "-[PROCESS] Frame " << frame_id << " received" << std::endl;

							// Обрабатываем кадр
							if (input_frame.has_single_image()) {
								try {
									cv::Mat original_image = extract_image(input_frame.single_image());

									if (!original_image.empty()) {
										cv::Mat processed_image = effect.applyEffect(original_image);

										// Создаем выходной кадр
										video_processing::VideoFrame output_frame;
										output_frame.set_frame_id(frame_id);
										output_frame.set_timestamp(input_frame.timestamp());
										output_frame.set_sender_id(worker_id);
										output_frame.set_frame_type(video_processing::PROCESSED_FRAME);

										auto* image_pair = output_frame.mutable_image_pair();
										*image_pair->mutable_original() = create_image_data(original_image);
										*image_pair->mutable_processed() = create_image_data(processed_image);

										// Отправляем результат
										if (send_to_composer(output_frame)) {
											processed_count++;
											std::cout << "-[SENT] Frame " << frame_id << " to Composer" << std::endl;

											// Вычисляем время обработки
											auto process_end = std::chrono::steady_clock::now();
											double process_time = std::chrono::duration_cast<std::chrono::milliseconds>(
												process_end - process_start).count();

											// Обновляем среднее время
											if (avg_processing_time_ms == 0) {
												avg_processing_time_ms = process_time;
											}
											else {
												avg_processing_time_ms = avg_processing_time_ms * 0.9 + process_time * 0.1;
											}

											// Снова готовы к работе
											is_ready = true;

											// Отправляем сигнал о готовности для следующего кадра
											std::this_thread::sleep_for(std::chrono::milliseconds(1));
											send_ready_signal();
										}
										else {
											failed_count++;
											std::cout << "-[FAIL] Failed to send frame " << frame_id << std::endl;
											is_ready = true;  // Все равно готовы
											send_ready_signal();
										}
									}
									else {
										failed_count++;
										std::cout << "-[FAIL] Empty image in frame " << frame_id << std::endl;
										is_ready = true;
										send_ready_signal();
									}
								}
								catch (const std::exception& e) {
									failed_count++;
									std::cout << "-[FAIL] Processing error: " << e.what() << std::endl;
									is_ready = true;
									send_ready_signal();
								}
							}
							else {
								failed_count++;
								std::cout << "-[FAIL] No image in frame " << frame_id << std::endl;
								is_ready = true;
								send_ready_signal();
							}
						}
						else {
							failed_count++;
							std::cout << "- [FAIL]  Failed to parse frame" << std::endl;
							is_ready = true;
							send_ready_signal();
						}
					}
				}

				// Если мы готовы, но еще не отправили сигнал
				if (is_ready.load()) {
					send_ready_signal();
				}

				// Показываем статистику
				//show_statistics();

				// Короткая пауза
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			catch (const std::exception& e) {
				std::cout << "- [FAIL] " << e.what() << std::endl;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}

		std::cout << "\n=== Worker Finished ===" << std::endl;
	}
};

int main() {
	try {
		Worker worker;
		worker.run();
		return 0;
	}
	catch (const std::exception& e) {
		std::cout << "[FAIL] Worker error: " << e.what() << std::endl;
		return -1;
	}
}