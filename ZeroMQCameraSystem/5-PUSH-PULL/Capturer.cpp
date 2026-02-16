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
#include <queue>
#include <mutex>


class Capturer {
private:
	zmq::context_t context;      // Контекст ZeroMQ
	zmq::socket_t push_socket;   // PUSH-сокет для отправки кадров
	cv::VideoCapture cap;        // Объект для захвата видео
	uint64_t frame_counter;      // Счетчик кадров
	std::string sender_id;       // Идентификатор отправителя
	size_t max_queue_size;       // Максимальный размер очереди
	std::atomic<uint64_t> dropped_frames;  // Счетчик потерянных кадров
	std::atomic<bool> stop_requested;      // Флаг остановки

	// Статистические счетчики
	std::atomic<uint64_t> total_captured;  // Всего захвачено кадров
	std::atomic<uint64_t> total_sent;      // Всего отправлено кадров
	std::chrono::steady_clock::time_point start_time;  // Время начала работы

public:
	// Конструктор класса
	Capturer() : context(1), push_socket(context, ZMQ_PUSH),  // Инициализация контекста и PUSH-сокета
		frame_counter(0), max_queue_size(queue_size), dropped_frames(0), // Инициализация счетчиков
		stop_requested(false), total_captured(0), total_sent(0) { // Инициализация флагов и счетчиков

		std::cout << "=== Capturer Initialization ===" << std::endl;
		std::cout << "1. Available network interfaces:" << std::endl;

		int sndhwm = max_queue_size;    // Максимальный размер очереди отправки
		int linger = 0;                 // Не задерживать сообщения при закрытии
		push_socket.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm)); // Установка размера очереди
		push_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // Установка политики linger

		// Попытка привязаться к каждому адресу из списка
		for (const auto& address : capturer_bind_addresses) {
			try {
				push_socket.bind(address);  // Привязка сокета к адресу
				std::cout << "- [ OK ] Capturer bound to: " << address << std::endl;  // Успешная привязка
				break;  // Выход из цикла после успешной привязки
			}
			catch (const zmq::error_t& e) {  // Обработка ошибок привязки
				std::cout << "- [FAIL] Failed to bind to " << address << ": " << e.what() << std::endl;  // Сообщение об ошибке
			}
		}

		init_camera();  // Инициализация камеры
		sender_id = "capturer_push_" + std::to_string(time(nullptr));  // Генерация ID отправителя

		start_time = std::chrono::steady_clock::now();  // Запись времени начала
		std::cout << "3. Capturer ID: " << sender_id << std::endl;
		std::cout << "======================================================" << std::endl;
	}

	// Деструктор
	~Capturer() {
		stop_requested = true;  // Установка флага остановки
	}

private:
	// Метод инициализации камеры
	void init_camera() {
		std::cout << "2. Searching for camera..." << std::endl;  // Сообщение о поиске камеры

		cap.open(camera_id);  // Попытка открыть камеру с текущим ID
		if (cap.isOpened()) {  // Если камера открыта успешно
			// Баланс между качеством и производительностью
			cap.set(cv::CAP_PROP_FRAME_WIDTH, cap_frame_width);   // Установка ширины кадра
			cap.set(cv::CAP_PROP_FRAME_HEIGHT, cap_frame_height);  // Установка высоты кадра
			cap.set(cv::CAP_PROP_FPS, cap_fps);            // Установка FPS
			cap.set(cv::CAP_PROP_BUFFERSIZE, 1);      // Установка размера буфера

			// Получение фактических параметров камеры
			double actual_width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
			double actual_height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
			double actual_fps = cap.get(cv::CAP_PROP_FPS);

			std::cout << "- [ OK ] Camera: " << actual_width << "x" << actual_height
				<< " at " << actual_fps << " FPS" << std::endl;
			return;  // Выход после успешной инициализации
		}

		throw std::runtime_error("- [FAIL] No camera found!");  // Исключение если камера не найдена
	}

	// Создание protobuf-сообщения с видеокадром
	video_processing::VideoFrame create_video_frame(const cv::Mat& frame) {
		video_processing::VideoFrame message;  // Создание объекта сообщения

		message.set_frame_id(frame_counter++);  // Установка ID кадра и инкремент счетчика
		message.set_timestamp(get_current_time());  // Установка временной метки
		message.set_sender_id(sender_id);           // Установка ID отправителя
		message.set_frame_type(video_processing::CAPTURED_FRAME);  // Установка типа кадра


		std::vector<uchar> buffer;  // Буфер для JPEG данных
		std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality };  // Параметры сжатия
		cv::imencode(".jpg", frame, buffer, compression_params);  // Кодирование кадра в JPEG

		auto* image_data = message.mutable_single_image();  // Получение указателя на данные изображения
		image_data->set_width(frame.cols);        // Установка ширины
		image_data->set_height(frame.rows);       // Установка высоты
		image_data->set_pixel_format(proto_pixel_format);  // Установка формата пикселей
		image_data->set_encoding(proto_image_encoding);     // Установка кодирования
		image_data->set_image_data(buffer.data(), buffer.size());  // Установка данных изображения

		total_captured++;  // Увеличение счетчика захваченных кадров
		return message;    // Возврат сформированного сообщения
	}

	// Получение текущего времени в секундах
	double get_current_time() {
		auto now = std::chrono::system_clock::now();  // Текущее время
		return std::chrono::duration<double>(now.time_since_epoch()).count();  // Конвертация в секунды
	}

	// Отправка кадра через ZeroMQ
	bool send_frame(const video_processing::VideoFrame& frame) {
		try {
			std::string serialized = frame.SerializeAsString();  // Сериализация protobuf-сообщения
			zmq::message_t message(serialized.size());           // Создание ZeroMQ сообщения
			memcpy(message.data(), serialized.data(), serialized.size());  // Копирование данных

			// Неблокирующая отправка - важно!
			return push_socket.send(message, ZMQ_DONTWAIT);  // Отправка без блокировки
		}
		catch (const std::exception& e) {
			return false;  // Возврат false при ошибке
		}
	}

	// Отображение статистики
	void show_statistics() {
		auto now = std::chrono::steady_clock::now();  // Текущее время
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);  // Прошедшее время

		// Расчет FPS
		double capture_fps = (elapsed.count() > 0) ? total_captured / elapsed.count() : 0;
		double send_fps = (elapsed.count() > 0) ? total_sent / elapsed.count() : 0;

		// Вывод статистики
		std::cout << "=== Capturer stats: "
			<< total_captured << " captured, "    // Всего захвачено
			<< total_sent << " sent, "            // Всего отправлено
			<< dropped_frames << " dropped, "     // Всего потеряно
			<< std::fixed << std::setprecision(1) << capture_fps << "/" << send_fps << " fps"  // FPS
			<< std::endl;
	}

public:
	// Основной метод работы захватчика
	void run() {
		std::cout << "=== Capturer Started ===" << std::endl;
		std::cout << std::endl << "=== 5-PULL-PUSH with dynamic balancing ===" << std::endl << std::endl;
		std::cout << "4. Max queue size HWM: " << max_queue_size << std::endl;

		// Даем время worker'am подключиться
		std::cout << "- [ -- ] Waiting 5 seconds for workers to connect..." << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(5));  // Пауза для подключения воркеров
		std::cout << "- [ OK ] Starting frame capture..." << std::endl;

		cv::Mat frame;              // Переменная для хранения кадра
		int empty_frame_count = 0;  // Счетчик пустых кадров
		uint64_t consecutive_drops = 0;  // Счетчик последовательных потерь

		// Основной цикл обработки
		while (!stop_requested && empty_frame_count < 50) {  // Пока не запрошена остановка и нет 50 пустых кадров подряд
			if (cap.read(frame) && !frame.empty()) {  // Если кадр успешно прочитан и не пустой
				empty_frame_count = 0;  // Сброс счетчика пустых кадров

				auto video_frame = create_video_frame(frame);  // Создание сообщения с кадром

				if (send_frame(video_frame)) {  // Если отправка успешна
					total_sent++;               // Увеличение счетчика отправленных
					consecutive_drops = 0;      // Сброс счетчика потерь

					if (total_sent) {  // Каждые 50 отправленных кадров ... % 50 == 0
						std::cout << "- [ OK ] Sent frame " << video_frame.frame_id() << std::endl;
					}
				}
				else {
					// не удалось отправить - worker'ы заняты
					dropped_frames++;       // Увеличение счетчика потерь
					consecutive_drops++;    // Увеличение счетчика последовательных потерь

					if (consecutive_drops % 10 == 0) {  // Каждые 10 последовательных потерь
						std::cout << "- [FAIL] Workers busy, dropped " << consecutive_drops << " frames" << std::endl;
					}
				}
			}
			else {
				empty_frame_count++;  // Увеличение счетчика пустых кадров
			}

			// статистика каждые 30 кадров
			if (total_sent % 30 == 0 && total_sent > 0) {
				show_statistics();  // Показать статистику
			}

			// минимальная пауза для сбалансированной нагрузки
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}

		if (empty_frame_count >= 50) {  // Если слишком много пустых кадров
			std::cout << "- [FAIL] Camera stopped producing frames." << std::endl;
		}


		cap.release();  // Освобождение камеры
	}
};

// Главная функция
int main() {
	try {
		Capturer capturer;  // Создание объекта захватчика
		capturer.run();         // Запуск захватчика
		return 0;               // Успешное завершение
	}
	catch (const std::exception& e) {  // Обработка исключений
		std::cout << "- [FAIL] Capturer error: " << e.what() << std::endl;
		return -1;  // Завершение с ошибкой
	}
}
