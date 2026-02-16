#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include "video_processing.pb.h"
#include "..\video_addresses.h"
#include <chrono>
#include <thread>
#include <fstream>

#pragma warning(disable : 4996) // Отключение предупреждений безопасности

class Capturer {
private:
	zmq::context_t context;  // Контекст ZeroMQ
	zmq::socket_t push_socket;  // Сокет для отправки Workers
	cv::VideoCapture cap;  // Захват видео с камеры
	uint64_t frame_counter;  // Счетчик кадров
	std::string sender_id;  // Идентификатор отправителя

public:
	Capturer() : context(1), push_socket(context, ZMQ_PUSH), frame_counter(0) {
		std::cout << "=== Capturer Initialization ===" << std::endl;
		// Привязка к сетевым интерфейсам
		std::cout << "1. Available network interfaces:" << std::endl;
		// Перебор всех доступных адресов для привязки
		for (const auto& address : capturer_bind_addresses) {
			try {
				// Попытка привязаться к адресу
				push_socket.bind(address);
				std::cout << "- [ OK ] Capturer bound to: " << address << std::endl;
				break;
			}
			catch (const zmq::error_t& e) {
				std::cout << "- [FAIL] Failed to bind to: " << address << ": " << e.what() << std::endl;
			}
		}

		init_camera(); // Инициализация камеры
		sender_id = "capturer_" + std::to_string(time(nullptr)); // Генерация ID
		std::cout << "3. Capturer ID: " << sender_id << std::endl;
		std::cout << "======================================================" << std::endl;
	}

private:
	void init_camera() {
		std::cout << "2. Searching for camera..." << std::endl;
		cap.open(camera_id);  // Попытка открыть камеру с текущим ID
		if (cap.isOpened()) {
			// Установка параметров камеры
			cap.set(cv::CAP_PROP_FRAME_WIDTH, cap_frame_width);  // Ширина кадра
			cap.set(cv::CAP_PROP_FRAME_HEIGHT, cap_frame_height); // Высота кадра
			cap.set(cv::CAP_PROP_FPS, cap_fps); // Частота кадров
			std::cout << "- [ OK ] Camera found at ID: " << camera_id << std::endl;
			return; // Выход при успешном открытии камеры
		}

		throw std::runtime_error("- [FAIL] No camera found!");
	}

	// Создание protobuf сообщения с кадром
	video_processing::VideoFrame create_video_frame(const cv::Mat& frame) {
		video_processing::VideoFrame message; // Создание объекта сообщения

		// Заполнение метаданных
		message.set_frame_id(frame_counter++);  // Установка ID кадра и инкремент счетчика
		message.set_timestamp(get_current_time());  // Установка временной метки (Текущее время)
		message.set_sender_id(sender_id); // Установка ID отправителя
		message.set_frame_type(video_processing::CAPTURED_FRAME); // Тип кадра

		// Кодирование изображения в JPEG
		std::vector<uchar> buffer; // Буфер для JPEG данных
		std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality }; // Параметры сжатия JPEG (качество 80%)
		cv::imencode(".jpg", frame, buffer, compression_params); // Кодирование кадра в JPEG формат

		// Заполнение данных изображения в сообщении
		auto* image_data = message.mutable_single_image(); // Получение указателя на поле изображения
		image_data->set_width(frame.cols); // Ширина изображения
		image_data->set_height(frame.rows); // Высота изображения
		image_data->set_pixel_format(proto_pixel_format); // Формат пикселей (BGR)
		image_data->set_encoding(proto_image_encoding); // Кодирование (JPEG)
		image_data->set_image_data(buffer.data(), buffer.size()); // Копирование JPEG данных

		return message; // Возврат готового сообщения
	}

	double get_current_time() {
		// Получение текущего времени в секундах
		auto now = std::chrono::system_clock::now(); // Получение текущего времени
		return std::chrono::duration<double>(now.time_since_epoch()).count(); // Преобразование в секунды с дробной частью
	}

public:
	void run() { // Основной метод работы захватчика
		std::cout << "=== Capturer Started ===" << std::endl;
		std::cout << std::endl << "=== 1-No buffer limits ===" << std::endl << std::endl;
		std::cout << "4. Sending frames to Workers on port 5555..." << std::endl;

		cv::Mat frame; // Переменная для хранения кадра
		while (true) { // Чтение кадра с камеры
			if (!cap.read(frame) || frame.empty()) {
				std::cout << "- [FAIL] Failed to grab frame" << std::endl;
				std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Пауза при ошибке чтения кадра
				continue; // Переход к следующей итерации
			}

			//cv::imshow("Capturer - Local View", frame); // Локальный просмотр

			try {
				// Создаем и сериализуем сообщение
				auto video_frame = create_video_frame(frame); // Создание protobuf сообщения
				std::string serialized = video_frame.SerializeAsString(); // Сериализация в строку

				// Создание ZeroMQ сообщения
				zmq::message_t message(serialized.size()); // Выделение памяти под сообщение
				memcpy(message.data(), serialized.data(), serialized.size()); // Копирование данных
				push_socket.send(message); // Отправка сообщения (без флагов - старый стиль ZeroMQ)
				std::cout << "- [ OK ] Sent frame: " << video_frame.frame_id() << std::endl;
			}
			catch (const std::exception& e) {
				std::cout << "- [FAIL] Error sending frame: " << e.what() << std::endl;
			}

			if (cv::waitKey(1) == 27) break;  // Проверка нажатия ESC для выхода
		}

		cap.release();  // Освобождение ресурсов камеры
		cv::destroyAllWindows();  // Закрытие всех окон OpenCV
	}
};

int main() {
	try {
		Capturer capturer; // Создание объекта захватчика
		capturer.run(); // Запуск основного цикла
		return 0; // Успешное завершение
	}
	catch (const std::exception& e) {
		std::cout << "- [FAIL] Capturer error: " << e.what() << std::endl;
		return -1;
	}
}