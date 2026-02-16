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
#include <unordered_set>
#include <mutex>

#pragma warning(disable : 4996)  // Отключаем предупреждения для устаревших функций

class Capturer {
private:
	zmq::context_t context;  // Контекст ZeroMQ (обязательный для всех сокетов)
	zmq::socket_t router_socket;  // ROUTER сокет для общения с worker'ами
	cv::VideoCapture cap;  // Объект захвата видео из OpenCV
	uint64_t frame_counter;  // Счетчик кадров (уникальный идентификатор)
	std::string sender_id;  // Идентификатор этого захватчика
	size_t max_queue_size;  // Максимальный размер очереди кадров
	std::atomic<uint64_t> dropped_frames;  // Счетчик потерянных кадров (потокобезопасный)
	std::atomic<bool> stop_requested;  // Флаг остановки приложения

	// Интеллектуальная очередь и управление Worker'ами
	std::queue<video_processing::VideoFrame> frame_queue;  // Очередь кадров для обработки
	std::queue<std::string> available_workers;  // Очередь доступных worker'ов
	std::mutex queue_mutex;  // Мьютекс для защиты очередей от гонки данных
	std::unordered_set<std::string> connected_workers;  // Множество подключенных worker'ов

public:

	Capturer() : context(1), router_socket(context, ZMQ_ROUTER),  // Создаем контекст и ROUTER сокет
		frame_counter(0), max_queue_size(queue_size), dropped_frames(0), stop_requested(false) {  // Инициализация переменных

		std::cout << "=== Capturer Initialization ===" << std::endl;
		std::cout << "1. Available network interfaces:" << std::endl;
		// Настраиваем ROUTER сокет
		int hwm = max_queue_size;  // High Water Mark - максимальный размер очереди ZeroMQ
		router_socket.setsockopt(ZMQ_SNDHWM, &hwm, sizeof(hwm));  // Устанавливаем лимит отправки
		router_socket.setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));  // Устанавливаем лимит приема

		// Попытка привязаться к каждому адресу из списка
		for (const auto& address : capturer_bind_addresses) {
			try {
				router_socket.bind(address);  // Привязка сокета к адресу
				std::cout << "- [ OK ] Capturer bound to: " << address << std::endl;  // Успешная привязка
				break;  // Выход из цикла после успешной привязки
			}
			catch (const zmq::error_t& e) {  // Обработка ошибок привязки
				std::cout << "- [FAIL] Failed to bind to " << address << ": " << e.what() << std::endl;  // Сообщение об ошибке
			}
		}

		// Инициализация камеры
		init_camera();  // Вызов метода инициализации камеры
		sender_id = "capturer_router_" + std::to_string(time(nullptr));  // Генерация уникального ID

		std::cout << "- [ OK ] ROUTER socket with HWM: " << max_queue_size << " frames" << std::endl;
		std::cout << "3. Capturer ID: " << sender_id << std::endl;
		std::cout << "======================================================" << std::endl;
	}

	// Деструктор - установка флага остановки
	~Capturer() {
		stop_requested = true;  // Запрос на остановку работы
	}

private:
	// Метод инициализации камеры
	void init_camera() {
		std::cout << "2. Searching for camera..." << std::endl;  // Сообщение о поиске камеры

		// Перебор возможных ID камер (0-9)
		cap.open(camera_id);  // Попытка открыть камеру с текущим ID
		if (cap.isOpened()) {  // Если камера успешно открыта
			// Настройка параметров камеры
			cap.set(cv::CAP_PROP_FRAME_WIDTH, cap_frame_width);  // Установка ширины кадра
			cap.set(cv::CAP_PROP_FRAME_HEIGHT, cap_frame_height);  // Установка высоты кадра
			cap.set(cv::CAP_PROP_FPS, cap_fps);  // Установка FPS
			std::cout << "- [ OK ] Camera found at ID: " << camera_id << std::endl;  // Сообщение об успехе

			// Получение реального FPS камеры
			double actual_fps = cap.get(cv::CAP_PROP_FPS);  // Чтение фактического FPS
			std::cout << "- [ OK ] Camera FPS: " << actual_fps << std::endl;  // Вывод FPS
			return;  // Выход из метода после успешной инициализации
		}

		throw std::runtime_error("- [FAIL] No camera found!");  // Исключение если камера не найдена
	}

	// Создание protobuf сообщения из кадра OpenCV
	video_processing::VideoFrame create_video_frame(const cv::Mat& frame) {
		video_processing::VideoFrame message;  // Создание объекта сообщения

		// Заполнение метаданных сообщения
		message.set_frame_id(frame_counter++);  // Установка ID кадра и инкремент счетчика
		message.set_timestamp(get_current_time());  // Установка временной метки
		message.set_sender_id(sender_id);  // Установка идентификатора отправителя
		message.set_frame_type(video_processing::CAPTURED_FRAME);  // Установка типа кадра

		// Кодируем изображение в JPEG для уменьшения размера
		std::vector<uchar> buffer;  // Буфер для сжатых данных
		std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality };  // Параметры сжатия (качество 80%)
		cv::imencode(".jpg", frame, buffer, compression_params);  // Сжатие изображения в JPEG

		// Заполняем данные изображения в сообщении
		auto* image_data = message.mutable_single_image();  // Получаем указатель на поле изображения
		image_data->set_width(frame.cols);  // Ширина изображения
		image_data->set_height(frame.rows);  // Высота изображения
		image_data->set_pixel_format(proto_pixel_format);  // Формат пикселей (BGR для OpenCV)
		image_data->set_encoding(proto_image_encoding);  // Тип кодирования (JPEG)
		image_data->set_image_data(buffer.data(), buffer.size());  // Данные изображения

		return message;  // Возврат готового сообщения
	}

	// Получение текущего времени в секундах
	double get_current_time() {
		auto now = std::chrono::system_clock::now();  // Текущее время
		return std::chrono::duration<double>(now.time_since_epoch()).count();  // Преобразование в секунды
	}

	// Обработка запросов от worker'ов
	void process_worker_requests() {
		// Обрабатываем все входящие запросы от Worker'ов
		while (true) {
			zmq::message_t identity;  // Сообщение с идентификатором worker'а
			zmq::message_t request;  // Сообщение с запросом

			// Неблокирующее получение идентификатора с флагом ZMQ_DONTWAIT
			if (!router_socket.recv(&identity, ZMQ_DONTWAIT)) {
				break;  // Выход если нет сообщений
			}

			// Получение запроса от worker'а
			if (router_socket.recv(&request, ZMQ_DONTWAIT)) {
				// Преобразование идентификатора в строку
				std::string worker_id(static_cast<char*>(identity.data()), identity.size());

				// Проверка что worker запрашивает задание (пустое сообщение или "GET")
				if (request.size() == 0 ||
					std::string(static_cast<char*>(request.data()), request.size()) == "GET") {

					std::lock_guard<std::mutex> lock(queue_mutex);  // Блокировка мьютекса
					connected_workers.insert(worker_id);  // Добавление worker'а в множество подключенных
					available_workers.push(worker_id);  // Добавление worker'а в очередь доступных

					std::cout << "- [ OK ] Worker " << worker_id << " is ready for work" << std::endl;  // Логирование
				}
			}
		}
	}

	// Распределение кадров доступным worker'ам
	void distribute_frames() {
		std::lock_guard<std::mutex> lock(queue_mutex);  // Блокировка мьютекса для безопасного доступа

		// Пока есть доступные worker'ы и кадры в очереди
		while (!available_workers.empty() && !frame_queue.empty()) {
			std::string worker_id = available_workers.front();  // Берем первого доступного worker'а
			available_workers.pop();  // Удаляем его из очереди доступных

			video_processing::VideoFrame frame = frame_queue.front();  // Берем первый кадр из очереди
			frame_queue.pop();  // Удаляем его из очереди

			try {
				std::string serialized = frame.SerializeAsString();  // Сериализация сообщения в строку

				// Отправляем кадр конкретному Worker'у (используем ZMQ_SNDMORE для multipart сообщения)
				zmq::message_t identity_msg(worker_id.data(), worker_id.size());  // Первая часть: идентификатор
				router_socket.send(identity_msg, ZMQ_SNDMORE);  // Отправка с флагом "есть еще данные"

				zmq::message_t frame_msg(serialized.data(), serialized.size());  // Вторая часть: данные кадра
				router_socket.send(frame_msg, 0);  // Отправка без флагов

				std::cout << "- [ OK ] Sent frame " << frame.frame_id() << " to " << worker_id << std::endl;  // Логирование успеха
			}
			catch (const std::exception& e) {  // Обработка ошибок отправки
				std::cout << "- [FAIL] Failed to send to " << worker_id << ": " << e.what() << std::endl;  // Логирование ошибки
				// Возвращаем Worker в доступные при ошибке отправки
				available_workers.push(worker_id);  // Возврат worker'а в очередь
			}
		}
	}

	// Управление размером очереди (удаление старых кадров при переполнении)
	void manage_queue_size() {
		std::lock_guard<std::mutex> lock(queue_mutex);  // Блокировка мьютекса

		// Удаляем старые кадры если очередь переполнена
		while (frame_queue.size() > max_queue_size) {
			frame_queue.pop();  // Удаление самого старого кадра
			dropped_frames++;  // Увеличение счетчика потерянных кадров
		}
	}

public:
	// Основной метод работы захватчика
	void run() {
		std::cout << "=== Capturer Started ===" << std::endl;
		std::cout << std::endl << "=== 3-ROUTER-DEALER with frame skips ===" << std::endl << std::endl;
		std::cout << "4. ROUTER socket HWM: " << max_queue_size << " frames" << std::endl;  // Информация о размере очереди
		std::cout << "ROUTER pattern: Workers request frames when ready" << std::endl;  // Описание паттерна

		cv::Mat frame;  // Переменная для хранения кадра
		auto start_time = std::chrono::steady_clock::now();  // Время начала работы для расчета FPS
		uint64_t sent_frames = 0;  // Счетчик отправленных кадров

		// Основной цикл работы
		while (!stop_requested) {
			// 1. Захватываем кадры с камеры
			if (cap.read(frame) && !frame.empty()) {  // Чтение кадра и проверка что он не пустой
				auto video_frame = create_video_frame(frame);  // Создание сообщения из кадра

				{
					std::lock_guard<std::mutex> lock(queue_mutex);  // Блокировка для добавления в очередь
					frame_queue.push(video_frame);  // Добавление кадра в очередь
					sent_frames++;  // Увеличение счетчика отправленных кадров
				}

				manage_queue_size();  // Проверка и управление размером очереди
			}

			// 2. Обрабатываем запросы от Worker'ов
			process_worker_requests();  // Обработка входящих запросов на получение кадров

			// 3. Распределяем кадры доступным Worker'ам
			distribute_frames();  // Отправка кадров готовым к работе worker'ам

			// 4. Показываем статистику каждые 5 кадров ...
			if (sent_frames % 30 == 0) {  // Условие вывода статистики
				auto now = std::chrono::steady_clock::now();  // Текущее время


				std::lock_guard<std::mutex> lock(queue_mutex);  // Блокировка для чтения статистики
				std::cout << "=== Capturer stats: " << sent_frames << " captured, "  // Вывод статистики
					<< dropped_frames << " dropped, "  // Потерянные кадры
					<< frame_queue.size() << " queued, "  // Размер очереди
					<< available_workers.size() << " workers ready, "  // Доступные worker'ы
					<< std::fixed << std::setprecision(1) << "" << std::endl;  // FPS с форматированием
			}

			// 5. Проверяем выход по клавише ESC
			if (cv::waitKey(1) == 27) {  // Ожидание нажатия клавиши (1 мс) и проверка на ESC
				stop_requested = true;  // Установка флага остановки
			}

			// Небольшая пауза для снижения нагрузки на CPU
			std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Сон на 1 мс
		}

		cap.release();  // Освобождение камеры
		cv::destroyAllWindows();  // Закрытие всех окон OpenCV
	}
};

// Точка входа в программу
int main() {
	try {
		Capturer capturer;  // Создание объекта захватчика
		capturer.run();  // Запуск основного цикла
		return 0;  // Успешное завершение
	}
	catch (const std::exception& e) {  // Обработка исключений
		std::cout << "- [FAIL] Capturer error: " << e.what() << std::endl;  // Вывод сообщения об ошибке
		return -1;  // Завершение с ошибкой
	}
}