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

class REQREPWorker {
private:
	zmq::context_t context; // Контекст ZeroMQ для управления сокетами
	zmq::socket_t req_socket; // Сокет для запросов к Capturer (REQ)
	zmq::socket_t push_socket; // Сокет для отправки результатов к Composer (PUSH)
	std::string worker_id; // Уникальный идентификатор воркера
	std::string capturer_address; // Адрес Capturer'а
	std::string composer_address; // Адрес Composer'а
	ScannerDarklyEffect effect; // Объект для применения визуального эффекта
	std::atomic<uint64_t> processed_count; // Счетчик успешно обработанных кадров
	std::atomic<uint64_t> failed_count; // Счетчик неудачных обработок
	std::chrono::steady_clock::time_point start_time; // Время начала работы для статистики
	std::atomic<bool> stop_requested; // Флаг запроса остановки

public:
	REQREPWorker() : context(1), // Инициализация контекста с 1 IO thread
		req_socket(context, ZMQ_REQ), // Инициализация REQ сокета
		push_socket(context, ZMQ_PUSH), // Инициализация PUSH сокета
		processed_count(0), failed_count(0), stop_requested(false) { // Инициализация счетчиков и флагов

		std::cout << "=== Worker Initialization ===" << std::endl;
		std::cout << "1. Available capturer network interfaces:" << std::endl;

		// Генерация ID воркера на основе PID процесса
		worker_id = "worker_" + std::to_string(_getpid());

		// Настройка времени ожидания при закрытии сокетов
		int linger = 100;
		req_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
		push_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

		// Подключение к Composer (PUSH) - всегда сначала
		for (const auto& address : worker_to_composer_connect_addresses) {
			push_socket.connect(address);  // Подключаем PUSH сокет
			composer_address = address;    // Сохраняем успешный адрес
			std::cout << "- [ OK ] PUSH connected to Composer: " << address << std::endl;
			break;  // Выходим при успехе
		}

		// Настройка эффекта Scanner Darkly
		effect.setCannyThresholds(effect_canny_low_threshold, effect_canny_high_threshold); // Установка порогов для детектора границ Кэнни
		effect.setGaussianKernelSize(effect_gaussian_kernel_size); // Размер ядра размытия Гаусса
		effect.setDilationKernelSize(effect_dilation_kernel_size); // Отключение расширения контуров
		effect.setColorQuantizationLevels(effect_color_quantization_levels); // Уровни квантования цвета
		effect.setBlackContours(effect_black_contours); // Включение черных контуров

		// Запись времени начала работы для расчета FPS
		start_time = std::chrono::steady_clock::now();

		std::cout << "2. Worker initialized with Scanner Darkly effect" << std::endl;
		std::cout << "3. Worker ID: " << worker_id << std::endl;
		std::cout << "======================================================" << std::endl;
	}

	~REQREPWorker() {
		stop_requested = true; // Установка флага остановки при разрушении объекта
	}

private:
	// Извлечение изображения из protobuf сообщения
	cv::Mat extract_image(const video_processing::ImageData& image_data) {
		const std::string& data = image_data.image_data(); // Получение бинарных данных изображения
		std::vector<uchar> buffer(data.begin(), data.end()); // Конвертация в вектор байтов

		if (image_data.encoding() == proto_image_encoding) {
			// Декодирование JPEG изображения
			cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
			if (decoded.empty()) {
				throw std::runtime_error("- [FAIL] Failed to decode JPEG image");
			}
			return decoded;
		}
		else {
			// Создание матрицы из raw данных
			return cv::Mat(
				image_data.height(), // Высота изображения
				image_data.width(), // Ширина изображения
				CV_8UC3, // 3-канальное изображение (BGR)
				(void*)data.data() // Указатель на данные
			).clone(); // Клонирование для безопасности памяти
		}
	}

	// Создание protobuf сообщения с изображением
	video_processing::ImageData create_image_data(const cv::Mat& image) {
		video_processing::ImageData image_data; // Создание объекта для хранения изображения

		std::vector<uchar> buffer; // Буфер для сжатого изображения
		std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality }; // Параметры сжатия JPEG
		cv::imencode(".jpg", image, buffer, compression_params); // Кодирование в JPEG

		// Заполнение метаданных изображения
		image_data.set_width(image.cols); // Ширина
		image_data.set_height(image.rows); // Высота
		image_data.set_pixel_format(proto_pixel_format); // Формат пикселей
		image_data.set_encoding(proto_image_encoding); // Формат кодирования
		image_data.set_image_data(buffer.data(), buffer.size()); // Бинарные данные

		return image_data;
	}

	// Ожидание запуска Capturer и подключение к нему
	bool wait_for_capturer() {
		int attempts = 0;
		while (!stop_requested && attempts < 1000) { // 100 секунд ожидания (1000 * 100ms)
			try {

				for (const auto& address : worker_to_capturer_connect_addresses) {
					req_socket.connect(address);  // Пытаемся подключиться
					capturer_address = address;      // Сохраняем успешный адрес
					std::cout << "- [ -- ] REQ connected to Capturer: " << address << std::endl;
					break;  // Выходим из цикла при успешном подключении
				}
				// Пытаемся подключиться к Capturer
				return true;
			}
			catch (const zmq::error_t& e) {
				attempts++;
				if (attempts % 10 == 0) {
					std::cout << "- [ -- ] Still waiting for Capturer... (" << attempts << " attempts)" << std::endl;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Пауза между попытками

				// Закрываем и пересоздаем сокет если нужно
				req_socket.close();
				req_socket = zmq::socket_t(context, ZMQ_REQ); // Пересоздание сокета
				int linger = 100;
				req_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // Повторная настройка
			}
		}

		std::cout << "- [FAIL] Capturer not found after 100 seconds" << std::endl;
		return false;
	}

	// Запрос кадра у Capturer и его обработка
	bool request_and_process_frame() {
		try {
			// Отправляем запрос на получение кадра
			zmq::message_t request(9); // Создание сообщения размером 9 байт
			memcpy(request.data(), "GET_FRAME", 9); // Копирование команды в сообщение

			if (!req_socket.send(request, ZMQ_DONTWAIT)) { // Неблокирующая отправка
				std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Пауза при неудаче
				return false;
			}

			// Ждем ответ от Capturer
			zmq::message_t response;
			zmq::pollitem_t items[] = { { static_cast<void*>(req_socket), 0, ZMQ_POLLIN, 0 } }; // Настройка poll
			zmq::poll(items, 1, 100); // Ожидание ответа с таймаутом 100ms

			if (items[0].revents & ZMQ_POLLIN) { // Проверка поступления данных
				if (req_socket.recv(&response)) { // Получение ответа
					if (response.size() > 0) { // Проверка что ответ не пустой
						video_processing::VideoFrame input_frame;
						if (input_frame.ParseFromArray(response.data(), response.size())) { // Парсинг protobuf
							return process_frame(input_frame); // Обработка кадра
						}
					}
					// Пустой ответ - нет кадров, но Capturer работает
					std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Короткая пауза
				}
			}
		}
		catch (const zmq::error_t& e) {
			// При ошибке - переподключаемся
			std::cout << "- [FAIL] Connection error, reconnecting..." << std::endl;
			req_socket.close();
			req_socket = zmq::socket_t(context, ZMQ_REQ); // Пересоздание сокета
			int linger = 100;
			req_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // Настройка
			std::this_thread::sleep_for(std::chrono::milliseconds(5000)); // Пауза перед переподключением
			wait_for_capturer(); // Повторное подключение
		}

		return false;
	}

	// Обработка одного кадра
	bool process_frame(const video_processing::VideoFrame& input_frame) {
		if (processed_count) { // Логирование каждые 10 кадров ...% 10 == 0
			std::cout << "- [ OK ] Processing frame: " << input_frame.frame_id() << std::endl;
		}

		if (!input_frame.has_single_image()) { // Проверка наличия изображения
			failed_count++;
			return false;
		}

		cv::Mat original_image;
		try {
			original_image = extract_image(input_frame.single_image()); // Декодирование изображения
		}
		catch (const std::exception& e) {
			failed_count++;
			return false;
		}

		if (original_image.empty()) { // Проверка валидности изображения
			failed_count++;
			return false;
		}

		// Применяем эффект ScannerDarkly
		cv::Mat processed_image;
		try {
			processed_image = effect.applyEffect(original_image); // Применение визуального эффекта
		}
		catch (const std::exception& e) {
			failed_count++;
			return false;
		}

		// Создание выходного сообщения с результатом
		video_processing::VideoFrame output_frame;
		output_frame.set_frame_id(input_frame.frame_id()); // Сохранение ID кадра
		output_frame.set_timestamp(input_frame.timestamp()); // Сохранение временной метки
		output_frame.set_sender_id(worker_id); // ID воркера
		output_frame.set_frame_type(video_processing::PROCESSED_FRAME); // Тип сообщения

		auto* image_pair = output_frame.mutable_image_pair(); // Создание пары изображений
		*image_pair->mutable_original() = create_image_data(original_image); // Исходное изображение
		*image_pair->mutable_processed() = create_image_data(processed_image); // Обработанное изображение

		if (send_to_composer(output_frame)) { // Отправка результата
			processed_count++; // Увеличение счетчика успешных обработок
			return true;
		}
		else {
			failed_count++; // Увеличение счетчика ошибок
			return false;
		}
	}

	// Отправка результата в Composer
	bool send_to_composer(const video_processing::VideoFrame& output_frame) {
		try {
			std::string serialized = output_frame.SerializeAsString(); // Сериализация protobuf
			zmq::message_t output_message(serialized.size()); // Создание ZeroMQ сообщения
			memcpy(output_message.data(), serialized.data(), serialized.size()); // Копирование данных
			return push_socket.send(output_message, ZMQ_DONTWAIT); // Неблокирующая отправка
		}
		catch (const std::exception& e) {
			return false;
		}
	}

	// Вывод статистики работы
	void show_statistics() {
		auto now = std::chrono::steady_clock::now(); // Текущее время

		std::cout << "=== Worker " << worker_id << ": "
			<< processed_count << " processed, " // Количество обработанных
			<< failed_count << " failed, " // Количество ошибок
			<< std::fixed << std::setprecision(1) << "" // FPS с одним знаком после запятой
			<< std::endl;
	}

public:
	// Основной цикл работы воркера
	void run() {
		std::cout << "=== Worker Started ===" << std::endl;
		std::cout << std::endl << "=== 4-REQ-REP with frame skips ===" << std::endl << std::endl;
		//std::cout << "4. Listening from: " << capturer_address << std::endl;  // Адрес источника
		//std::cout << "5. Sending to: " << composer_address << std::endl;      // Адрес назначения
		// Ждем capturer
		if (!wait_for_capturer()) {
			std::cout << "- [FAIL] Cannot start without Capturer" << std::endl;
			return;
		}

		while (!stop_requested) { // Основной цикл обработки
			if (request_and_process_frame()) { // Попытка обработки кадра
				// Успешно обработали кадр - короткая пауза
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			else {
				// Не удалось - ждем немного дольше
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}

			if (processed_count > 0 && processed_count % 20 == 0) { // Вывод статистики каждые 20 кадров
				show_statistics();
			}
		}
	}
};

int main() {
	try {
		REQREPWorker worker; // Создание экземпляра воркера
		worker.run(); // Запуск основного цикла
		return 0;
	}
	catch (const std::exception& e) {
		std::cout << "- [FAIL] Worker error: " << e.what() << std::endl; // Обработка исключений
		return -1;
	}
}