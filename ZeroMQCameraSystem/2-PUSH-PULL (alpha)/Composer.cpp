#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include "video_processing.pb.h"
#include "..\video_addresses.h"
#include <direct.h>
#include <chrono>
#include <map>
#include <cstdio>
#include <iomanip>
#include <atomic>

class Composer {
private:
	zmq::context_t context;  // Контекст ZeroMQ
	zmq::socket_t pull_socket;  // Сокет для получения сообщений от Workers
	std::string composer_id;  // Уникальный идентификатор компоновщика
	std::string temp_original_dir;  // Директория для временного хранения исходных кадров
	std::string temp_processed_dir;  // Директория для временного хранения обработанных кадров
	cv::VideoWriter video_writer_original;  // Видео-райтер для исходного видео
	cv::VideoWriter video_writer_processed;  // Видео-райтер для обработанного видео
	uint64_t expected_frame_id;  // Ожидаемый порядковый номер следующего кадра
	std::map<uint64_t, std::pair<cv::Mat, cv::Mat>> frame_buffer;  // Буфер для хранения кадров (по ID)
	bool recording;  // Флаг активности записи видео
	cv::Size last_frame_size;  // Размер последнего обработанного кадра
	std::chrono::steady_clock::time_point last_frame_time;  // Время получения последнего кадра
	bool first_frame_received;  // Флаг получения первого кадра
	uint64_t max_frame_gap;  // Максимально допустимый разрыв между кадрами перед вставкой черных
	std::chrono::steady_clock::time_point last_frame_received_time;  // Время последнего полученного кадра
	bool insertion_active;  // Флаг активности вставки черных кадров
	std::atomic<uint64_t> total_frames_received;  // Счетчик всех полученных кадров
	std::atomic<uint64_t> black_frames_inserted;  // Счетчик вставленных черных кадров
	std::atomic<uint64_t> total_frames_written;  // Счетчик записанных в видео кадров

public:
	// Конструктор класса Composer
	Composer() : context(1), pull_socket(context, ZMQ_PULL),  // Инициализация контекста и PULL-сокета
		expected_frame_id(0), recording(false), first_frame_received(false),  // Инициализация флагов
		max_frame_gap(frame_gap), insertion_active(false),  // Установка максимального разрыва и флага вставки
		total_frames_received(0), black_frames_inserted(0), total_frames_written(0) {  // Инициализация счетчиков

		// Очистка старых файлов перед началом работы
		cleanup_old_video_files();
		std::cout << "=== Composer Initialization ===" << std::endl;
		// Привязка к сетевым интерфейсам
		std::cout << "1. Available network interfaces:" << std::endl;

		// Попытка привязки к каждому адресу из списка
		for (const auto& address : composer_bind_addresses) {
			try {
				pull_socket.bind(address);  // Привязка сокета к адресу
				std::cout << "- [ OK ] Composer bound to: " << address << std::endl;  // Сообщение об успешной привязке
				break;  // Выход из цикла после успешной привязки
			}
			catch (const zmq::error_t& e) {  // Обработка ошибок привязки
				std::cout << "- [FAIL] Failed to bind to " << address << ": " << e.what() << std::endl;  // Сообщение об ошибке
			}
		}

		// Создаем папки для сохранения временных файлов
		//temp_original_dir = "Temp_Original";
		//temp_processed_dir = "Temp_Processed";
		//_mkdir(temp_original_dir.c_str());
		//_mkdir(temp_processed_dir.c_str());

		composer_id = "composer_" + std::to_string(time(nullptr));  // Генерация ID
		last_frame_time = std::chrono::steady_clock::now();  // Инициализация времени последнего кадра
		last_frame_received_time = std::chrono::steady_clock::now();  // Инициализация времени получения кадра
		std::cout << "2. Composer ID: " << composer_id << std::endl;
		std::cout << "======================================================" << std::endl;
	}

private:
	// Проверка существования файла (безопасная версия)
	bool file_exists(const std::string& filename) {
		FILE* file = nullptr;  // Указатель на файл
		if (fopen_s(&file, filename.c_str(), "r") == 0 && file != nullptr) {  // Попытка открыть файл для чтения
			fclose(file);  // Закрытие файла
			return true;  // Файл существует
		}
		return false;  // Файл не существует
	}

	// Очистка старых видеофайлов перед началом работы
	void cleanup_old_video_files() {
		std::vector<std::string> video_files = {  // Список видеофайлов для очистки
			"output_original.avi",  // Исходное видео
			"output_processed.avi"  // Обработанное видео
		};

		std::cout << "=== Cleaning up old video files ===" << std::endl;  // Заголовок процесса очистки

		int deleted_count = 0;  // Счетчик удаленных файлов
		for (const auto& filename : video_files) {  // Перебор всех файлов в списке
			if (file_exists(filename)) {  // Проверка существования файла
				if (std::remove(filename.c_str()) == 0) {  // Попытка удаления файла
					std::cout << "- [ OK ] Deleted: " << filename << std::endl;  // Сообщение об успешном удалении
					deleted_count++;  // Увеличение счетчика удаленных файлов
				}
				else {
					std::cout << "- [FAIL] Failed to delete: " << filename << std::endl;  // Сообщение об ошибке удаления
				}
			}
			else {
				std::cout << "- [ OK ] Not found: " << filename << std::endl;  // Сообщение об отсутствии файла
			}
		}
		std::cout << "- [ OK ] Deleted " << deleted_count << " old video files" << std::endl;  // Итог очистки
		std::cout << "======================================================" << std::endl;
	}

	// Извлечение изображения из protobuf сообщения
	cv::Mat extract_image(const video_processing::ImageData& image_data) {
		const std::string& data = image_data.image_data();  // Получение данных изображения
		std::vector<uchar> buffer(data.begin(), data.end());  // Конвертация в вектор байтов

		if (image_data.encoding() == proto_image_encoding) {  // Если изображение в формате JPEG
			return cv::imdecode(buffer, cv::IMREAD_COLOR);  // Декодирование JPEG
		}
		else {  // Для RAW формата
			return cv::Mat(  // Создание матрицы OpenCV из raw данных
				image_data.height(),  // Высота изображения
				image_data.width(),   // Ширина изображения
				CV_8UC3,              // 3-канальное изображение (BGR)
				(void*)data.data()    // Указатель на данные
			).clone();  // Создание копии данных
		}
	}

	// Инициализация видео-райтеров
	void initialize_video_writers(const cv::Mat& first_frame) {
		std::string video_original_path = "output_original.avi";  // Путь для исходного видео
		std::string video_processed_path = "output_processed.avi";  // Путь для обработанного видео

		// Кодек и параметры
		int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');  // Кодек MJPEG
		double fps = cap_fps;  // Частота кадров
		cv::Size frame_size(first_frame.cols, first_frame.rows);  // Размер кадра
		last_frame_size = frame_size;  // Сохранение размера для создания черных кадров

		video_writer_original.open(video_original_path, fourcc, fps, frame_size);  // Открытие райтера для исходного видео
		video_writer_processed.open(video_processed_path, fourcc, fps, frame_size);  // Открытие райтера для обработанного видео

		if (video_writer_original.isOpened() && video_writer_processed.isOpened()) {  // Проверка успешного открытия
			recording = true;  // Установка флага записи
			std::cout << "- [ OK ] Video recording started: " << fps << " FPS, "  // Сообщение о начале записи
				<< frame_size.width << "x" << frame_size.height << std::endl;
			std::cout << "- [ OK ] Output files: " << video_original_path << ", " << video_processed_path << std::endl;  // Пути к файлам
		}
		else {
			std::cout << "- [FAIL] Failed to initialize video writers!" << std::endl;  // Сообщение об ошибке
			std::cout << "Original writer: " << (video_writer_original.isOpened() ? "OK" : "FAILED") << std::endl;  // Статус исходного райтера
			std::cout << "Processed writer: " << (video_writer_processed.isOpened() ? "OK" : "FAILED") << std::endl;  // Статус обработанного райтера
		}
	}

	// Вставка черных кадров для пропусков
	void insert_missing_frames() {
		if (frame_buffer.empty() || !recording) return;  // Выход если буфер пуст или запись не активна

		// Находим минимальный frame_id в буфере
		uint64_t min_buffered_id = frame_buffer.begin()->first;  // Первый (минимальный) ID в буфере

		// Проверяем чтобы не было переполнения (min_buffered_id должен быть >= expected_frame_id)
		if (min_buffered_id < expected_frame_id) {  // Если получен кадр с ID меньше ожидаемого
			// Это ошибка состояния - очищаем буфер от старых кадров
			std::cout << "- [WARN] Cleaning old frames from buffer. Expected: " << expected_frame_id
				<< ", but got: " << min_buffered_id << std::endl;
			frame_buffer.erase(min_buffered_id);  // Удаление устаревшего кадра
			return;
		}

		// Если разрыв между ожидаемым и минимальным в буфере слишком большой
		if (min_buffered_id - expected_frame_id > max_frame_gap) {  // Проверка на превышение максимального разрыва
			if (!insertion_active) {  // Если вставка еще не активна
				std::cout << "- [ -- ] Missing frames detected. Inserting black frames from "  // Сообщение о начале вставки
					<< expected_frame_id << " to " << (min_buffered_id - 1) << std::endl;
				insertion_active = true;  // Активация флага вставки
			}

			uint64_t frames_to_insert = min_buffered_id - expected_frame_id;  // Количество кадров для вставки
			uint64_t inserted_count = 0;  // Счетчик вставленных кадров

			// Вставляем черные кадры до следующего доступного кадра
			while (expected_frame_id < min_buffered_id) {  // Пока есть пропуски
				insert_black_frame(expected_frame_id);  // Вставка черного кадра
				expected_frame_id++;  // Увеличение ожидаемого ID
				inserted_count++;  // Увеличение счетчика вставленных
				black_frames_inserted++;  // Увеличение общего счетчика черных кадров
			}

			insertion_active = false;  // Деактивация флага вставки
			std::cout << "- [ -- ] Inserted " << inserted_count << " black frames" << std::endl;  // Сообщение о завершении вставки
		}
	}

	// Вставка одного черного кадра
	void insert_black_frame(uint64_t frame_id) {
		if (!recording || last_frame_size.width == 0) return;  // Выход если запись не активна или размер неизвестен

		cv::Mat black_frame = cv::Mat::zeros(last_frame_size, CV_8UC3);  // Создание черного кадра

		// Добавляем текст с номером пропущенного кадра
		std::string text = "MISSING FRAME " + std::to_string(frame_id);  // Текст для отображения
		cv::putText(black_frame, text,  // Добавление текста на кадр
			cv::Point(50, last_frame_size.height / 2),  // Позиция текста
			cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);  // Параметры шрифта и цвета

		// Записываем в оба видео
		video_writer_original.write(black_frame);  // Запись в исходное видео
		video_writer_processed.write(black_frame);  // Запись в обработанное видео
		total_frames_written++;  // Увеличение счетчика записанных кадров

		// Сохраняем для отладки
		//save_black_frame(frame_id, black_frame);  // Сохранение черного кадра как отдельного файла

		std::cout << "- [ -- ] Inserted black frame for: " << frame_id << std::endl;  // Сообщение о вставке
	}

	// Сохранение черного кадра для отладки
	void save_black_frame(uint64_t frame_id, const cv::Mat& black_frame) {
		std::string original_filename = temp_original_dir + "\\black_" +  // Имя файла для исходного черного кадра
			std::to_string(frame_id) + ".jpg";
		std::string processed_filename = temp_processed_dir + "\\black_" +  // Имя файла для обработанного черного кадра
			std::to_string(frame_id) + ".jpg";

		cv::imwrite(original_filename, black_frame);  // Сохранение исходного черного кадра
		cv::imwrite(processed_filename, black_frame);  // Сохранение обработанного черного кадра
	}

	// Обработка полученного кадра
	void process_frame_for_video(const video_processing::VideoFrame& frame) {
		// Обновляем время получения кадра
		last_frame_received_time = std::chrono::steady_clock::now();  // Обновление времени последнего получения

		// Обрабатываем только кадры с изображениями
		if (frame.has_image_pair()) {  // Проверка наличия пары изображений
			const auto& pair = frame.image_pair();  // Получение пары изображений
			cv::Mat original_image = extract_image(pair.original());  // Извлечение исходного изображения
			cv::Mat processed_image = extract_image(pair.processed());  // Извлечение обработанного изображения

			if (!original_image.empty() && !processed_image.empty()) {  // Проверка что изображения не пустые
				// Обновляем время последнего кадра
				last_frame_time = std::chrono::steady_clock::now();  // Обновление времени последнего кадра
				first_frame_received = true;  // Установка флага получения первого кадра

				// Инициализируем VideoWriter при первом кадре
				if (!recording) {  // Если запись еще не начата
					initialize_video_writers(original_image);  // Инициализация видео-райтеров
				}

				uint64_t received_frame_id = frame.frame_id();  // Получение ID кадра

				// Проверяем не устарел ли кадр
				if (received_frame_id < expected_frame_id) {  // Если получен кадр с меньшим ID чем ожидаемый
					std::cout << "- [ -- ] Skipping old frame: " << received_frame_id
						<< " (expected: " << expected_frame_id << ")" << std::endl;  // Сообщение о пропуске устаревшего кадра
					return;  // Выход из функции
				}

				// Сохраняем в буфер
				frame_buffer[received_frame_id] = { original_image, processed_image };  // Сохранение пары изображений в буфер

				// Проверяем пропуски
				insert_missing_frames();  // Проверка и вставка пропущенных кадров

				// Пытаемся записать упорядоченные кадры
				write_ordered_frames();  // Запись кадров в правильном порядке
			}
		}
	}

	// Запись упорядоченных кадров из буфера
	void write_ordered_frames() {
		// Ищем следующий ожидаемый кадр в буфере
		while (frame_buffer.find(expected_frame_id) != frame_buffer.end()) {  // Пока следующий ожидаемый кадр есть в буфере
			auto& frames = frame_buffer[expected_frame_id];  // Получение пары изображений по ID

			// Записываем в видео
			if (recording) {  // Если запись активна
				video_writer_original.write(frames.first);  // Запись исходного изображения
				video_writer_processed.write(frames.second);  // Запись обработанного изображения
				total_frames_written++;  // Увеличение счетчика записанных кадров
			}

			// Сохраняем отдельные файлы для отладки
			//save_frame_pair(expected_frame_id, frames.first, frames.second);  // Сохранение пары как отдельных файлов

			// Удаляем из буфера и увеличиваем счетчик
			frame_buffer.erase(expected_frame_id);  // Удаление кадра из буфера
			expected_frame_id++;  // Увеличение ожидаемого ID

			// Выводим прогресс каждые 10 кадров ...% 10 == 0
			if (expected_frame_id) {  // Каждый 10-й кадр
				std::cout << "- [ OK ] Written to video: frame " << (expected_frame_id - 1)
					<< " (buffer: " << frame_buffer.size() << ")" << std::endl;  // Сообщение о прогрессе
			}
		}
	}

	// Сохранение пары кадров как отдельных файлов
	void save_frame_pair(uint64_t frame_id, const cv::Mat& original, const cv::Mat& processed) {
		// Сохраняем как отдельные файлы (опционально)
		std::string original_filename = temp_original_dir + "\\frame_" +  // Имя файла для исходного кадра
			std::to_string(frame_id) + ".jpg";
		std::string processed_filename = temp_processed_dir + "\\frame_" +  // Имя файла для обработанного кадра
			std::to_string(frame_id) + ".jpg";

		cv::imwrite(original_filename, original);  // Сохранение исходного кадра
		cv::imwrite(processed_filename, processed);  // Сохранение обработанного кадра
	}

	// Проверка условий для остановки компоновщика
	bool should_stop() {
		if (!first_frame_received) {  // Если первый кадр еще не получен
			return false; // Ждем первый кадр
		}

		auto now = std::chrono::steady_clock::now();  // Текущее время
		auto time_since_last_frame = std::chrono::duration_cast<std::chrono::seconds>(now - last_frame_received_time);  // Время с последнего кадра

		// Проверяем пропуски при длительной паузе
		if (time_since_last_frame > std::chrono::seconds(2) && !frame_buffer.empty()) {  // Если пауза больше 2 секунд и буфер не пуст
			insert_missing_frames();  // Вставка пропущенных кадров
			write_ordered_frames(); // Пытаемся записать что есть  // Запись оставшихся кадров
		}

		// Останавливаемся если нет кадров 10 секунд И буфер пуст
		if (time_since_last_frame > std::chrono::seconds(10) && frame_buffer.empty()) {  // Если пауза больше 10 секунд и буфер пуст
			std::cout << "- [ OK ] No frames for 10 seconds. Finishing..." << std::endl;  // Сообщение о завершении
			return true;  // Возврат true для остановки
		}

		return false;  // Продолжение работы
	}

	// Отображение статистики работы
	void show_statistics() {
		std::cout << "=== Composer stats: "  // Заголовок статистики
			<< total_frames_received << " received, "  // Полученные кадры
			<< total_frames_written << " written, "  // Записанные кадры
			<< black_frames_inserted << " black inserted, "  // Вставленные черные кадры
			<< frame_buffer.size() << " buffered" << std::endl;  // Кадры в буфере
	}

public:
	// Основной метод запуска компоновщика
	void run() {
		std::cout << "=== Composer Started ===" << std::endl;  // Сообщение о запуске
		std::cout << std::endl << "=== 2-PULL-PUSH with frame skipping ===" << std::endl << std::endl;
		std::cout << "3. Will auto-stop after 10 seconds of inactivity." << std::endl;

		auto start_time = std::chrono::steady_clock::now();  // Запись времени начала работы

		while (true) {  // Основной цикл работы
			try {
				zmq::message_t message;  // Сообщение ZeroMQ

				// Неблокирующий receive с таймаутом 1 секунда
				zmq::pollitem_t items[] = { { static_cast<void*>(pull_socket), 0, ZMQ_POLLIN, 0 } };  // Элемент для опроса сокета
				zmq::poll(items, 1, 1000); // Таймаут 1 секунда  // Ожидание сообщения с таймаутом

				if (items[0].revents & ZMQ_POLLIN) {  // Если есть входящее сообщение
					// Есть сообщение
					if (pull_socket.recv(&message)) {  // Получение сообщения
						video_processing::VideoFrame frame;  // Создание объекта для кадра
						if (frame.ParseFromArray(message.data(), message.size())) {  // Парсинг protobuf сообщения
							process_frame_for_video(frame);  // Обработка кадра
							total_frames_received++;  // Увеличение счетчика полученных кадров
						}
					}
				}

				// Периодически проверяем пропуски (только если есть кадры в буфере)
				if (!frame_buffer.empty() && total_frames_received % 10 == 0) {  // Каждый 10-й полученный кадр
					insert_missing_frames();  // Проверка и вставка пропущенных кадров
				}

				// Показываем статистику каждые 100 кадров
				if (total_frames_received % 100 == 0) {  // Каждый 60-й полученный кадр
					show_statistics();  // Отображение статистики
				}

				// Проверяем условие остановки
				if (should_stop()) {  // Проверка условий для остановки
					break;  // Выход из цикла
				}

			}
			catch (const zmq::error_t& e) {  // Обработка ошибок ZeroMQ
				if (e.num() != EINTR) {  // Если ошибка не "прервано системным вызовом"
					std::cout << "- [FAIL] ZMQ error: " << e.what() << std::endl;  // Сообщение об ошибке
				}
				break;  // Выход из цикла
			}
			catch (const std::exception& e) {  // Обработка стандартных исключений
				std::cout << "- [FAIL] Processing error: " << e.what() << std::endl;  // Сообщение об ошибке
			}
		}

		// Финальная обработка: вставляем черные кадры для оставшихся пропусков
		std::cout << "- [ -- ] Final processing: inserting remaining black frames..." << std::endl;  // Сообщение о финальной обработке
		while (!frame_buffer.empty()) {  // Пока буфер не пуст
			insert_missing_frames();  // Вставка пропущенных кадров
			write_ordered_frames();  // Запись упорядоченных кадров
		}

		// Финальная статистика
		auto end_time = std::chrono::steady_clock::now();  // Время окончания работы
		auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);  // Общее время работы

		std::cout << "\n=== COMPOSER FINISHED ===" << std::endl;  // Заголовок завершения
		std::cout << "Total frames received: " << total_frames_received << std::endl;  // Итоговые полученные кадры
		std::cout << "Total frames written: " << total_frames_written << std::endl;  // Итоговые записанные кадры
		std::cout << "Black frames inserted: " << black_frames_inserted << std::endl;  // Итоговые вставленные черные кадры
		std::cout << "Frames remaining in buffer: " << frame_buffer.size() << std::endl;  // Оставшиеся кадры в буфере
		if (total_frames_written > 0) {  // Если были записаны кадры
			std::cout << "Black frame rate: " << (double)black_frames_inserted / total_frames_written * 100 << "%" << std::endl;  // Процент черных кадров
		}

		// При завершении закрываем видеофайлы
		if (recording) {  // Если запись была активна
			video_writer_original.release();  // Закрытие райтера исходного видео
			video_writer_processed.release();  // Закрытие райтера обработанного видео
			std::cout << "Video files finalized: output_original.avi, output_processed.avi" << std::endl;  // Сообщение о закрытии файлов
		}

		std::cout << "=== Thank you for using ZeroMQ Camera System ===" << std::endl;  // Финальное сообщение
	}
};

// Главная функция программы
int main() {
	try {
		Composer composer;  // Создание экземпляра компоновщика
		composer.run();  // Запуск компоновщика
		return 0;  // Успешное завершение
	}
	catch (const std::exception& e) {  // Обработка исключений
		std::cout << "- [FAIL] Composer error: " << e.what() << std::endl;  // Сообщение об ошибке
		return -1;  // Завершение с ошибкой
	}
}