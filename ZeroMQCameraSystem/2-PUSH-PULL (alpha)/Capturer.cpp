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

#pragma warning(disable : 4996)  // Отключение предупреждений для устаревших функций

class Capturer {
private:
	zmq::context_t context;      // Контекст ZeroMQ
	zmq::socket_t push_socket;   // PUSH сокет для отправки кадров Workers
	cv::VideoCapture cap;        // Объект захвата видео с камеры
	uint64_t frame_counter;      // Счетчик кадров
	std::string sender_id;       // Идентификатор отправителя
	size_t max_queue_size;       // Максимальный размер очереди
	std::atomic<uint64_t> dropped_frames;  // Счетчик потерянных кадров (атомарный)
	std::atomic<bool> stop_requested;      // Флаг остановки (атомарный)

public:
	Capturer() : context(1), push_socket(context, ZMQ_PUSH), frame_counter(0),
		max_queue_size(queue_size), dropped_frames(0), stop_requested(false) {

		std::cout << "=== Capturer Initialization ===" << std::endl;
		std::cout << "1. Available network interfaces:" << std::endl;
		// Настраиваем буфер чтобы не терять кадры
		int hwm = max_queue_size;  // High Water Mark - максимальный размер очереди
		push_socket.setsockopt(ZMQ_SNDHWM, &hwm, sizeof(hwm));  // Установка размера буфера отправки

		// Попытка привязки к каждому адресу из списка
		for (const auto& address : capturer_bind_addresses) {
			try {
				push_socket.bind(address);  // Привязка сокета к адресу
				std::cout << "- [ OK ] Capturer bound to: " << address << std::endl;
				break;  // Выход при успешной привязке
			}
			catch (const zmq::error_t& e) {
				std::cout << "- [FAIL] Failed to bind to " << address << ": " << e.what() << std::endl;
			}
		}

		// Инициализация камеры
		init_camera();
		// Генерация уникального ID отправителя на основе времени
		sender_id = "capturer_" + std::to_string(time(nullptr));

		std::cout << "- [ OK ] PUSH socket with HWM: " << max_queue_size << " frames" << std::endl;
		std::cout << "3. Capturer ID: " << sender_id << std::endl;
		std::cout << "======================================================" << std::endl;
	}

	// Деструктор - устанавливает флаг остановки
	~Capturer() {
		stop_requested = true;
	}

private:
	// Инициализация камеры
	void init_camera() {
		std::cout << "2. Searching for camera..." << std::endl;
		// Перебор возможных ID камер (0-9)
		cap.open(camera_id);  // Попытка открыть камеру с текущим ID
		if (cap.isOpened()) {
			// Установка параметров камеры
			cap.set(cv::CAP_PROP_FRAME_WIDTH, cap_frame_width);   // Ширина кадра
			cap.set(cv::CAP_PROP_FRAME_HEIGHT, cap_frame_height);  // Высота кадра
			cap.set(cv::CAP_PROP_FPS, cap_fps);            // Желаемая частота кадров
			std::cout << "- [ OK ] Camera found at ID: " << camera_id << std::endl;

			// Проверяем реальные параметры
			double actual_fps = cap.get(cv::CAP_PROP_FPS);  // Получение реального FPS
			std::cout << "- [ OK ] Camera FPS: " << actual_fps << std::endl;
			return;  // Выход при успешном открытии камеры
		}

		throw std::runtime_error("- [FAIL] No camera found!");  // Исключение если камера не найдена
	}

	// Создание protobuf сообщения из кадра
	video_processing::VideoFrame create_video_frame(const cv::Mat& frame) {
		video_processing::VideoFrame message;  // Создание объекта сообщения

		// Заполнение метаданных
		message.set_frame_id(frame_counter++);  // Установка ID кадра и инкремент счетчика
		message.set_timestamp(get_current_time());  // Установка временной метки
		message.set_sender_id(sender_id);  // Установка ID отправителя
		message.set_frame_type(video_processing::CAPTURED_FRAME);  // Тип - захваченный кадр

		// Кодируем изображение в JPEG
		std::vector<uchar> buffer;  // Буфер для сжатого изображения
		// Параметры сжатия JPEG (качество 80%)
		std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality };
		cv::imencode(".jpg", frame, buffer, compression_params);  // Кодирование в JPEG

		// Заполняем данные изображения
		auto* image_data = message.mutable_single_image();  // Получение указателя на поле изображения
		image_data->set_width(frame.cols);      // Ширина изображения
		image_data->set_height(frame.rows);     // Высота изображения
		image_data->set_pixel_format(proto_pixel_format);  // Формат пикселей BGR
		image_data->set_encoding(proto_image_encoding);     // Кодирование JPEG
		image_data->set_image_data(buffer.data(), buffer.size());  // Данные изображения

		return message;  // Возврат готового сообщения
	}

	// Получение текущего времени в секундах
	double get_current_time() {
		auto now = std::chrono::system_clock::now();  // Текущее время
		return std::chrono::duration<double>(now.time_since_epoch()).count();  // Преобразование в секунды
	}

	// Ожидание подключения Workers
	bool wait_for_workers_connection(int timeout_seconds = 30) {
		std::cout << "- [ -- ] Waiting for Workers to connect..." << std::endl;

		auto start_time = std::chrono::steady_clock::now();  // Время начала ожидания
		int check_count = 0;  // Счетчик проверок

		// Цикл ожидания пока не истечет время или не запрошена остановка
		while (!stop_requested) {
			// В PUSH/PULL нет прямого способа проверить подключения
			// Ждем некоторое время чтобы Workers успели подключиться
			auto current_time = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time);

			// Проверка таймаута
			if (elapsed.count() >= timeout_seconds) {
				std::cout << "- [WARN] Starting without confirmed Worker connections..." << std::endl;
				return false;  // Возврат false при таймауте
			}

			// Показываем прогресс каждые 3 секунды ...% 3 == 0
			if (check_count) {
				std::cout << "- [ -- ] Waiting... (" << elapsed.count() << "/" << timeout_seconds << "s)" << std::endl;
			}

			std::this_thread::sleep_for(std::chrono::seconds(1));  // Пауза 1 секунда
			check_count++;  // Инкремент счетчика проверок
		}

		return false;  // Возврат false если запрошена остановка
	}

public:
	// Основной метод работы захватчика
	void run() {
		std::cout << "=== Capturer Started ===" << std::endl;
		std::cout << std::endl << "=== 2-PULL-PUSH with frame skipping ===" << std::endl << std::endl;
		std::cout << "4. PUSH socket HWM: " << max_queue_size << " frames" << std::endl;
		std::cout << "   Load balancing: AUTO (round-robin between Workers)" << std::endl;

		// Ждем подключения Workers (5 секунд)
		wait_for_workers_connection(5);

		cv::Mat frame;  // Переменная для хранения кадра
		auto start_time = std::chrono::steady_clock::now();  // Время начала работы
		uint64_t sent_frames = 0;        // Счетчик отправленных кадров
		uint64_t consecutive_drops = 0;  // Счетчик последовательных потерь
		const uint64_t MAX_CONSECUTIVE_DROPS = 10;  // Максимум последовательных потерь

		// Основной цикл обработки
		while (!stop_requested) {
			// Чтение кадра с камеры
			if (!cap.read(frame) || frame.empty()) {
				std::cout << "- [FAIL] Failed to grab frame" << std::endl;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Пауза при ошибке
				continue;  // Продолжение цикла
			}

			try {
				// Создаем protobuf сообщение из кадра
				auto video_frame = create_video_frame(frame);
				std::string serialized = video_frame.SerializeAsString();  // Сериализация сообщения

				// Создание ZeroMQ сообщения
				zmq::message_t message(serialized.size());
				memcpy(message.data(), serialized.data(), serialized.size());  // Копирование данных

				// Пытаемся отправить без блокировки
				bool sent = push_socket.send(message, ZMQ_DONTWAIT);  // Неблокирующая отправка

				if (sent) {
					sent_frames++;           // Инкремент отправленных кадров
					consecutive_drops = 0;   // Сброс счетчика последовательных потерь
					std::cout << "- [ OK ] Sent frame: " << video_frame.frame_id() << std::endl;

					// Показываем статистику каждые 50 кадров 
					if (sent_frames % 50 == 0) {
						auto now = std::chrono::steady_clock::now();

						std::cout << "=== Capturer stats: " << sent_frames << " sent, "
							<< dropped_frames << " dropped"
							<< std::fixed << std::setprecision(1) << "" << std::endl;
					}
				}
				else {
					// Не удалось отправить - буфер полон
					dropped_frames++;      // Инкремент потерянных кадров
					consecutive_drops++;   // Инкремент последовательных потерь

					// Вывод сообщения о потерях каждые 5 кадров ...
					if (consecutive_drops % 5 == 0) {
						std::cout << "- [ -- ] Consecutive drops: " << consecutive_drops
							<< " (buffer full, Workers busy)" << std::endl;
					}

					// Если много последовательных пропусков, небольшая пауза
					if (consecutive_drops > MAX_CONSECUTIVE_DROPS) {
						std::cout << "- [ -- ] Workers overloaded, pausing briefly..." << std::endl;
						std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Пауза 50мс
					}
				}
			}
			catch (const std::exception& e) {
				std::cout << "- [FAIL] Error sending frame: " << e.what() << std::endl;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Короткая пауза при ошибке
			}

			// Проверяем выход по клавише ESC
			if (cv::waitKey(1) == 27) {
				stop_requested = true;  // Установка флага остановки
			}
		}

		cap.release();           // Освобождение камеры
		cv::destroyAllWindows(); // Закрытие окон OpenCV
	}
};

// Главная функция
int main() {
	try {
		Capturer capturer;  // Создание объекта захватчика
		capturer.run();     // Запуск основного цикла
		return 0;           // Успешное завершение
	}
	catch (const std::exception& e) {
		std::cout << "- [FAIL] Capturer error: " << e.what() << std::endl;  // Обработка исключений
		return -1;          // Завершение с ошибкой
	}
}