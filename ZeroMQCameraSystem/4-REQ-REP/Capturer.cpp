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
#include <condition_variable> // Условные переменные для синхронизации


class Capturer {
private:
	zmq::context_t context; // Контекст ZeroMQ
	zmq::socket_t rep_socket; // REP-сокет для ответов worker'ам
	cv::VideoCapture cap; // Объект для захвата видео с камеры
	uint64_t frame_counter; // Счетчик кадров
	std::string sender_id; // Идентификатор отправителя
	size_t max_queue_size; // Максимальный размер очереди
	std::atomic<uint64_t> dropped_frames; // Счетчик потерянных кадров
	std::atomic<bool> stop_requested; // Флаг остановки

	// Очередь для хранения кадров
	std::queue<video_processing::VideoFrame> frame_queue;
	std::mutex queue_mutex; // Мьютекс для синхронизации доступа к очереди
	std::condition_variable queue_cv; // Условная переменная для уведомлений

	// Статистика
	std::atomic<uint64_t> total_captured; // Всего захвачено кадров
	std::atomic<uint64_t> total_sent; // Всего отправлено кадров
	std::chrono::steady_clock::time_point start_time; // Время начала работы
	std::atomic<int> active_workers; // Количество активных worker'ов

public:
	Capturer() : context(1), rep_socket(context, ZMQ_REP), // Инициализация контекста и REP-сокета
		frame_counter(0), max_queue_size(queue_size), dropped_frames(0), // Начальные значения
		stop_requested(false), total_captured(0), total_sent(0), active_workers(0) { // Инициализация флагов и счетчиков

		std::cout << "=== Capturer Initialization ===" << std::endl;
		std::cout << "1. Available network interfaces:" << std::endl;
		// Настройка REP сокета
		int linger = 0; // Время ожидания при закрытии сокета
		rep_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // Установка параметра

		// Привязка
		for (const auto& address : capturer_bind_addresses) {
			try {
				rep_socket.bind(address); // Привязка сокета к порту
				std::cout << "- [ OK ] Capturer REP socket bound to: " << address << std::endl;  // Успешная привязка
				break;  // Выход из цикла после успешной привязки
			}
			catch (const zmq::error_t& e) {  // Обработка ошибок привязки
				std::cout << "- [FAIL] Failed to bind to " << address << ": " << e.what() << std::endl;  // Сообщение об ошибке
			}
		}

		// Инициализация камеры
		init_camera(); // Вызов метода инициализации камеры
		sender_id = "capturer_rep_" + std::to_string(time(nullptr)); // Генерация ID отправителя
		start_time = std::chrono::steady_clock::now(); // Запись времени начала
		std::cout << "- [ OK ] Max queue size: " << max_queue_size << std::endl;
		std::cout << "3. Capturer ID: " << sender_id << std::endl;
		std::cout << "======================================================" << std::endl;
	}

	~Capturer() {
		stop_requested = true; // Установка флага остановки
		queue_cv.notify_all(); // Уведомление всех ожидающих потоков
	}

private:
	// Метод инициализации камеры
	void init_camera() {
		std::cout << "2. Searching for camera..." << std::endl;

		cap.open(camera_id);  // Попытка открыть камеру с текущим ID

		if (cap.isOpened()) { // Если камера открыта успешно
			// Установка параметров камеры
			cap.set(cv::CAP_PROP_FRAME_WIDTH, cap_frame_width); // Ширина кадра
			cap.set(cv::CAP_PROP_FRAME_HEIGHT, cap_frame_height); // Высота кадра
			cap.set(cv::CAP_PROP_FPS, cap_fps); // Частота кадров
			cap.set(cv::CAP_PROP_BUFFERSIZE, 1); // Размер буфера

			// Получение фактических параметров
			double actual_width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
			double actual_height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
			double actual_fps = cap.get(cv::CAP_PROP_FPS);

			std::cout << "- [ OK ] Camera found at ID: " << camera_id << std::endl;
			std::cout << "- [ OK ] Resolution: " << actual_width << "x" << actual_height << std::endl;
			std::cout << "- [ OK ] FPS: " << actual_fps << std::endl;
			return; // Выход при успешном открытии
		}

		throw std::runtime_error("- [FAIL] No camera found!"); // Исключение если камера не найдена
	}

	// Создание protobuf сообщения из кадра
	video_processing::VideoFrame create_video_frame(const cv::Mat& frame) {
		video_processing::VideoFrame message; // Создание сообщения

		// Заполнение метаданных
		message.set_frame_id(frame_counter++); // Установка ID кадра и инкремент счетчика
		message.set_timestamp(get_current_time()); // Установка временной метки
		message.set_sender_id(sender_id); // Установка ID отправителя
		message.set_frame_type(video_processing::CAPTURED_FRAME); // Установка типа кадра

		// Кодирование изображения в JPEG
		std::vector<uchar> buffer; // Буфер для сжатого изображения
		std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality }; // Параметры сжатия
		cv::imencode(".jpg", frame, buffer, compression_params); // Кодирование в JPEG

		// Заполнение данных изображения
		auto* image_data = message.mutable_single_image(); // Получение указателя на поле изображения
		image_data->set_width(frame.cols); // Ширина
		image_data->set_height(frame.rows); // Высота
		image_data->set_pixel_format(proto_pixel_format); // Формат пикселей
		image_data->set_encoding(proto_image_encoding); // Кодировка
		image_data->set_image_data(buffer.data(), buffer.size()); // Данные изображения

		total_captured++; // Увеличение счетчика захваченных кадров
		return message; // Возврат сообщения
	}

	// Получение текущего времени в секундах
	double get_current_time() {
		auto now = std::chrono::system_clock::now(); // Текущее время
		return std::chrono::duration<double>(now.time_since_epoch()).count(); // Конвертация в секунды
	}

	// Поток захвата кадров с камеры
	void capture_frames() {
		cv::Mat frame; // Переменная для хранения кадра
		int empty_frame_count = 0; // Счетчик пустых кадров

		// Основной цикл захвата
		while (!stop_requested && empty_frame_count < 50) { // Пока не запрошена остановка и нет много пустых кадров
			if (cap.read(frame) && !frame.empty()) { // Если кадр успешно прочитан и не пустой
				empty_frame_count = 0; // Сброс счетчика пустых кадров

				auto video_frame = create_video_frame(frame); // Создание сообщения

				{
					std::lock_guard<std::mutex> lock(queue_mutex); // Блокировка мьютекса

					// Проверка переполнения очереди
					if (frame_queue.size() >= max_queue_size) {
						frame_queue.pop(); // Удаление самого старого кадра
						dropped_frames++; // Увеличение счетчика потерянных кадров
					}

					frame_queue.push(video_frame); // Добавление кадра в очередь
					queue_cv.notify_one(); // Уведомление одного ожидающего потока
				}
			}
			else {
				empty_frame_count++; // Увеличение счетчика пустых кадров
				std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Пауза при ошибке
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(2)); // Небольшая пауза
		}

		// Обработка ситуации когда камера перестала работать
		if (empty_frame_count >= 50) {
			std::cout << "- [FAIL] Camera stopped producing frames." << std::endl;
			stop_requested = true; // Установка флага остановки
		}
	}

	// Обслуживание worker'ов
	void serve_workers() {
		std::cout << "- [ OK ] Starting to serve workers..." << std::endl;

		// Настройка poll для асинхронной работы
		zmq::pollitem_t items[] = {
			{ static_cast<void*>(rep_socket), 0, ZMQ_POLLIN, 0 } // Ожидание входящих сообщений
		};

		// Основной цикл обслуживания
		while (!stop_requested) {
			try {
				zmq::poll(items, 1, 100); // Ожидание событий 100ms

				// Если есть входящее сообщение
				if (items[0].revents & ZMQ_POLLIN) {
					zmq::message_t request; // Сообщение-запрос
					if (rep_socket.recv(&request, ZMQ_DONTWAIT)) { // Получение запроса без блокировки
						std::string request_str(static_cast<char*>(request.data()), request.size()); // Конвертация в строку

						// Обработка запроса кадра
						if (request_str == "GET_FRAME") {
							video_processing::VideoFrame frame_to_send; // Сообщение для отправки
							bool has_frame = false; // Флаг наличия кадра

							{
								std::unique_lock<std::mutex> lock(queue_mutex); // Блокировка мьютекса

								// Проверка наличия кадров в очереди
								if (!frame_queue.empty()) {
									frame_to_send = frame_queue.front(); // Взятие первого кадра
									frame_queue.pop(); // Удаление из очереди
									has_frame = true; // Установка флага
									total_sent++; // Увеличение счетчика отправленных кадров
								}
							}

							// Отправка кадра worker'у
							if (has_frame) {
								std::string serialized = frame_to_send.SerializeAsString(); // Сериализация
								zmq::message_t reply(serialized.size()); // Создание сообщения-ответа
								memcpy(reply.data(), serialized.data(), serialized.size()); // Копирование данных

								// Отправка ответа
								if (rep_socket.send(reply, ZMQ_DONTWAIT)) {
									// Периодический вывод информации  ...% 50 == 0
									if (total_sent) {
										std::cout << "- [ OK ] Sent frame " << frame_to_send.frame_id() << " to worker" << std::endl;
									}
								}
							}
							else {
								// Нет кадров - отправка пустого сообщения
								zmq::message_t reply(0);
								rep_socket.send(reply, ZMQ_DONTWAIT);
							}
						}
					}
				}

				// Периодический вывод статистики
				if (total_sent % 30 == 0 && total_sent > 0) {
					show_statistics(); // Вызов метода показа статистики
				}
			}
			catch (const zmq::error_t& e) {
				// Обработка ошибок ZeroMQ (кроме EAGAIN)
				if (e.num() != EAGAIN) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Пауза при ошибке
				}
			}
		}
	}

	// Вывод статистики работы
	void show_statistics() {
		auto now = std::chrono::steady_clock::now(); // Текущее время
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time); // Прошедшее время

		std::lock_guard<std::mutex> lock(queue_mutex); // Блокировка для доступа к очереди
		uint64_t queue_size = frame_queue.size(); // Текущий размер очереди

		// Расчет FPS
		double capture_fps = (elapsed.count() > 0) ? total_captured / elapsed.count() : 0;
		double send_fps = (elapsed.count() > 0) ? total_sent / elapsed.count() : 0;

		// Вывод статистики
		std::cout << "=== Capturer stats: "
			<< total_captured << " captured, " // Всего захвачено
			<< total_sent << " sent, " // Всего отправлено
			<< dropped_frames << " dropped, " // Потеряно
			<< queue_size << " queued, " // В очереди
			<< std::fixed << std::setprecision(1) << capture_fps << "/" << send_fps << " fps" // FPS
			<< std::endl;
	}

public:
	// Основной метод запуска
	void run() {
		std::cout << "=== Capturer Started ===" << std::endl;
		std::cout << std::endl << "=== 4-REQ-REP with frame skips ===" << std::endl << std::endl;
		std::cout << "4. Now serving waiting workers..." << std::endl;

		// Запуск потока захвата кадров
		std::thread capture_thread(&Capturer::capture_frames, this);
		// Запуск обслуживания worker'ов в основном потоке
		serve_workers();

		// Завершение работы
		stop_requested = true; // Установка флага остановки
		queue_cv.notify_all(); // Уведомление всех потоков

		// Ожидание завершения потока захвата
		if (capture_thread.joinable()) {
			capture_thread.join();
		}

		cap.release();  // Освобождение камеры
		cv::destroyAllWindows();  // Закрытие всех окон OpenCV
	}
};

// Точка входа в программу
int main() {
	try {
		Capturer capturer; // Создание объекта захватчика
		capturer.run(); // Запуск основного цикла
		return 0; // Успешное завершение
	}
	catch (const std::exception& e) {
		// Обработка исключений
		std::cout << "- [FAIL] Capturer error: " << e.what() << std::endl;
		return -1; // Завершение с ошибкой
	}
}