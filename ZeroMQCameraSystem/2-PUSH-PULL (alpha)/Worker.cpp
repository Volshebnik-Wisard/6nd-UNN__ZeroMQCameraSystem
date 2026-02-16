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

class Worker {
private:
	zmq::context_t context;        // Контекст ZeroMQ (версия 1 потока)
	zmq::socket_t pull_socket;     // Сокет для получения заданий от Capturer
	zmq::socket_t push_socket;     // Сокет для отправки результатов в Composer
	std::string worker_id;         // Уникальный идентификатор воркера
	std::string temp_dir;          // Временная директория для сохранения
	ScannerDarklyEffect effect;    // Обработчик эффекта "Scanner Darkly"
	std::string capturer_address;  // Адрес Capturer для подключения
	std::string composer_address;  // Адрес Composer для подключения
	uint64_t processed_count;      // Счетчик успешно обработанных кадров
	uint64_t failed_count;         // Счетчик неудачных обработок
	std::atomic<bool> stop_requested; // Флаг для graceful shutdown

public:
	Worker() : context(1), pull_socket(context, ZMQ_PULL), push_socket(context, ZMQ_PUSH),
		processed_count(0), failed_count(0), stop_requested(false) {

		std::cout << "=== Worker Initialization ===" << std::endl;
		std::cout << "1. Available capturer network interfaces:" << std::endl;
		// Настраиваем High Water Mark для ограничения буфера приема
		int rcvhwm = 10; // Максимум 10 сообщений в буфере
		pull_socket.setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));

		// Попытка подключения к одному из адресов Capturer (PULL сокет)
		for (const auto& address : worker_to_capturer_connect_addresses) {
			try {
				pull_socket.connect(address); // Подключение к PULL сокету
				capturer_address = address;   // Сохранение успешного адреса
				std::cout << "- [ OK ] Worker connected to Capturer: " << address << std::endl;
				break; // Выход при успешном подключении
			}
			catch (const zmq::error_t& e) {
				std::cout << "- [FAIL] Failed to connect to Capturer " << address << ": " << e.what() << std::endl;
			}
		}

		// Попытка подключения к одному из адресов Composer (PUSH сокет)
		for (const auto& address : worker_to_composer_connect_addresses) {
			try {
				push_socket.connect(address); // Подключение к PUSH сокету
				composer_address = address;   // Сохранение успешного адреса
				std::cout << "- [ OK ] Worker connected to Composer: " << address << std::endl;
				break; // Выход при успешном подключении
			}
			catch (const zmq::error_t& e) {
				std::cout << "- [FAIL] Failed to connect to Composer " << address << ": " << e.what() << std::endl;
			}
		}

		//temp_dir = "Worker_Temp"; // Создание временной директории для Worker
		//_mkdir(temp_dir.c_str());

		// Генерация уникального ID воркера на основе времени
		worker_id = "worker_" + std::to_string(time(nullptr));

		// Настройка параметров эффекта Scanner Darkly
		effect.setCannyThresholds(effect_canny_low_threshold, effect_canny_high_threshold);      // Пороги для детектора границ Кэнни
		effect.setGaussianKernelSize(effect_gaussian_kernel_size);         // Размер ядра размытия Гаусса
		effect.setDilationKernelSize(effect_dilation_kernel_size);         // Отключение утолщения контуров
		effect.setColorQuantizationLevels(effect_color_quantization_levels);    // Уровни квантования цвета
		effect.setBlackContours(effect_black_contours);           // Черные контуры вместо белых

		// Вывод информации о конфигурации
		std::cout << "2. Worker input buffer: " << rcvhwm << " frames" << std::endl;
		std::cout << "3. Worker initialized with Scanner Darkly effect" << std::endl;
		std::cout << "4. Worker ID: " << worker_id << std::endl;
		std::cout << "======================================================" << std::endl;
	}

	~Worker() {
		stop_requested = true;
	}

private:
	// Извлечение изображения из protobuf сообщения
	cv::Mat extract_image(const video_processing::ImageData& image_data) {
		const std::string& data = image_data.image_data(); // Получение бинарных данных
		std::vector<uchar> buffer(data.begin(), data.end()); // Конвертация в вектор байт

		// Декодирование JPEG изображения
		if (image_data.encoding() == proto_image_encoding) {
			cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR); // Декодирование JPEG
			if (decoded.empty()) {
				throw std::runtime_error("- [FAIL] Failed to decode JPEG image"); // Ошибка декодирования
			}
			return decoded; // Возврат декодированного изображения
		}
		else {
			// Создание матрицы из сырых данных (RAW формат)
			return cv::Mat(
				image_data.height(),     // Высота изображения
				image_data.width(),      // Ширина изображения  
				CV_8UC3,                 // Формат: 3 канала по 8 бит
				(void*)data.data()       // Указатель на данные
			).clone(); // Клонирование для безопасности памяти
		}
	}

	// Создание protobuf сообщения с изображением
	video_processing::ImageData create_image_data(const cv::Mat& image) {
		video_processing::ImageData image_data; // Создание объекта protobuf

		// Кодирование изображения в JPEG с параметрами сжатия
		std::vector<uchar> buffer; // Буфер для сжатых данных
		std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality }; // Качество 80%
		cv::imencode(".jpg", image, buffer, compression_params); // Кодирование в JPEG

		// Заполнение полей protobuf сообщения
		image_data.set_width(image.cols);           // Ширина изображения
		image_data.set_height(image.rows);          // Высота изображения
		image_data.set_pixel_format(proto_pixel_format); // Формат BGR (OpenCV)
		image_data.set_encoding(proto_image_encoding);    // Кодирование JPEG
		image_data.set_image_data(buffer.data(), buffer.size()); // Данные изображения

		return image_data; // Возврат заполненного объекта
	}

	// Вывод статистики производительности
	void show_statistics() {
		auto now = std::chrono::steady_clock::now(); // Текущее время         

		// Форматированный вывод статистики
		std::cout << "=== Worker " << worker_id << " stats: "
			<< processed_count << " processed, "  // Обработано кадров
			<< failed_count << " failed"        // Неудачных обработок
			<< std::fixed << std::setprecision(1) << "" << std::endl; // FPS с точностью до 0.1
	}

	// Отправка результата в Composer
	bool send_to_composer(const video_processing::VideoFrame& output_frame) {
		try {
			std::string serialized = output_frame.SerializeAsString(); // Сериализация protobuf
			zmq::message_t output_message(serialized.size()); // Создание ZMQ сообщения
			memcpy(output_message.data(), serialized.data(), serialized.size()); // Копирование данных

			bool sent = push_socket.send(output_message, ZMQ_DONTWAIT); // Неблокирующая отправка
			return sent; // Возврат статуса отправки
		}
		catch (const std::exception& e) {
			std::cout << "- [FAIL]  Error sending to Composer: " << e.what() << std::endl; // Ошибка отправки
			return false; // Неудачная отправка
		}
	}

public:
	// Основной цикл работы Worker
	void run() {
		std::cout << "=== Worker Started ===" << std::endl;
		std::cout << std::endl << "=== 2-PULL-PUSH with frame skipping ===" << std::endl << std::endl;
		std::cout << "5. Listening from: " << capturer_address << std::endl;  // Адрес источника
		std::cout << "6. Sending to: " << composer_address << std::endl;      // Адрес назначения

		// Основной цикл обработки
		while (!stop_requested) {
			try {
				zmq::message_t message; // Сообщение ZeroMQ

				// Неблокирующий прием сообщения receive
				bool received = pull_socket.recv(&message, ZMQ_DONTWAIT);

				if (received) {
					// Десериализация protobuf сообщения
					video_processing::VideoFrame input_frame;
					if (!input_frame.ParseFromArray(message.data(), message.size())) {
						std::cout << "- [FAIL] Failed to parse message from Capturer" << std::endl;
						failed_count++; // Увеличение счетчика ошибок
						continue; // Пропуск невалидного сообщения
					}

					// Вывод информации о полученном кадре
					std::cout << "- [ OK ] Processing frame: " << input_frame.frame_id()
						<< " from: " << input_frame.sender_id() << std::endl;

					// Извлекаем исходное изображение
					if (input_frame.has_single_image()) {
						cv::Mat original_image; // Матрица для исходного изображения
						try {
							original_image = extract_image(input_frame.single_image()); // Извлечение изображения
						}
						catch (const std::exception& e) {
							std::cout << "- [FAIL] Failed to extract image: " << e.what() << std::endl;
							failed_count++; // Ошибка извлечения
							continue; // Пропуск кадра
						}

						// Проверка что изображение не пустое
						if (!original_image.empty()) {
							cv::Mat processed_image; // Матрица для обработанного изображения
							try {
								processed_image = effect.applyEffect(original_image); // Применение эффекта
							}
							catch (const std::exception& e) {
								std::cout << "- [FAIL] Failed to apply effect: " << e.what() << std::endl;
								failed_count++; // Ошибка обработки
								continue; // Пропуск кадра
							}

							// Создание выходного сообщения для Composer
							video_processing::VideoFrame output_frame;

							// Копирование метаданных из входного сообщения
							output_frame.set_frame_id(input_frame.frame_id());     // ID кадра
							output_frame.set_timestamp(input_frame.timestamp());   // Метка времени
							output_frame.set_sender_id(worker_id);                 // ID воркера
							output_frame.set_frame_type(video_processing::PROCESSED_FRAME); // Тип: обработанный

							// Создание пары изображений (оригинал + обработанное)
							auto* image_pair = output_frame.mutable_image_pair();
							*image_pair->mutable_original() = create_image_data(original_image);  // Оригинал
							*image_pair->mutable_processed() = create_image_data(processed_image); // Обработанное

							// Отправка результата в Composer
							if (send_to_composer(output_frame)) {
								processed_count++; // Успешная обработка
								std::cout << "- [ OK ] Sent to Composer: " << output_frame.frame_id() << std::endl;
							}
							else {
								failed_count++; // Ошибка отправки
								std::cout << "- [FAIL] Failed to send to Composer: " << output_frame.frame_id() << std::endl;
							}

							// Периодический вывод статистики
							if (processed_count % 20 == 0) {
								show_statistics(); // Статистика каждые 20 кадров
							}
						}
						else {
							failed_count++; // Пустое изображение
							std::cout << "- [FAIL] Empty image extracted from frame: " << input_frame.frame_id() << std::endl;
						}
					}
					else {
						failed_count++; // Отсутствует изображение в сообщении
						std::cout << "- [FAIL] Frame has no image data: " << input_frame.frame_id() << std::endl;
					}
				}
				else {
					// Нет сообщений - небольшая пауза для снижения нагрузки на CPU
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
			catch (const zmq::error_t& e) {
				// Обработка ошибок ZeroMQ (игнорирование временных ошибок)
				if (e.num() != EAGAIN && e.num() != EINTR) {
					std::cout << "- [FAIL] ZMQ error: " << e.what() << std::endl;
					failed_count++; // Учет ошибки
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Пауза при ошибке
			}
			catch (const std::exception& e) {
				std::cout << "- [FAIL] Processing error: " << e.what() << std::endl; // Общая ошибка обработки
				failed_count++; // Учет ошибки
				std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Пауза при ошибке
			}
		}
	}
};

// Точка входа в программу
int main() {
	try {
		Worker worker; // Создание экземпляра Worker
		worker.run();  // Запуск основного цикла
		return 0;      // Успешное завершение
	}
	catch (const std::exception& e) {
		std::cout << "- [FAIL] Worker error: " << e.what() << std::endl; // Ошибка инициализации
		return -1;     // Аварийное завершение
	}
}