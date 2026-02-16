#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include "video_processing.pb.h"
#include "scanner_darkly_effect.hpp"
#include "..\video_addresses.h"
#include <direct.h>
#include <chrono>

class Worker {
private:
	zmq::context_t context;      // Контекст ZeroMQ
	zmq::socket_t pull_socket;   // Сокет для получения заданий от Capturer
	zmq::socket_t push_socket;   // Сокет для отправки результатов в Composer
	std::string worker_id;       // Уникальный идентификатор worker'а
	std::string temp_dir;        // Временная директория для сохранения изображений
	ScannerDarklyEffect effect;  // Объект для применения визуального эффекта
	std::string capturer_address; // Адрес Capturer'а для подключения
	std::string composer_address; // Адрес Composer'а для подключения

public:
	Worker() : context(1), pull_socket(context, ZMQ_PULL), push_socket(context, ZMQ_PUSH) {
		std::cout << "=== Worker Initialization ===" << std::endl;
		std::cout << "1. Available capturer network interfaces:" << std::endl;
		// Подключение к Capturer (PULL) - получение заданий
		for (const auto& address : worker_to_capturer_connect_addresses) {
			try {
				pull_socket.connect(address); // Подключение к адресу
				capturer_address = address;   // Сохранение успешного адреса
				std::cout << "- [ OK ] Worker connected to Capturer: " << address << std::endl;
				break; //Убрать
			}
			catch (const zmq::error_t& e) {
				std::cout << "- [FAIL] Failed to connect to Capturer " << address << ": " << e.what() << std::endl;
			}
		}
		std::cout << "1. Available composer network interfaces:" << std::endl;
		// Подключение к Composer (PUSH) - отправка результатов
		for (const auto& address : worker_to_composer_connect_addresses) {
			try {
				push_socket.connect(address); // Подключение к адресу
				composer_address = address;   // Сохранение успешного адреса
				std::cout << "- [ OK ] Worker connected to Composer: " << address << std::endl;
				break; // Выход из цикла при успешном подключении
			}
			catch (const zmq::error_t& e) {
				std::cout << "- [FAIL] Failed to connect to Composer " << address << ": " << e.what() << std::endl;
			}
		}

		//temp_dir = "Worker_Temp"; // Создаем папку для временного сохранения изображений
		//_mkdir(temp_dir.c_str());
		// Генерация уникального ID worker'а на основе текущего времени
		worker_id = "worker_" + std::to_string(time(nullptr));

        // Настройка параметров эффекта Scanner Darkly
		effect.setCannyThresholds(effect_canny_low_threshold, effect_canny_high_threshold); // Пороги для детектора границ Кэнни
		effect.setGaussianKernelSize(effect_gaussian_kernel_size); // Размер ядра размытия Гаусса
		effect.setDilationKernelSize(effect_dilation_kernel_size); // Отключение утолщения контуров
		effect.setColorQuantizationLevels(effect_color_quantization_levels); // Уровни квантования цвета
		effect.setBlackContours(effect_black_contours); // Использование черных контуров
		std::cout << "2. Worker initialized with Scanner Darkly effect" << std::endl;
		std::cout << "3. Worker ID: " << worker_id << std::endl;
		std::cout << "======================================================" << std::endl;
	}

private:
	cv::Mat extract_image(const video_processing::ImageData& image_data) {     // Извлечение изображения из protobuf сообщения
		const std::string& data = image_data.image_data(); // Получение бинарных данных изображения
		std::vector<uchar> buffer(data.begin(), data.end()); // Конвертация в вектор байтов
		// Декодирование изображения в зависимости от формата кодирования
		if (image_data.encoding() == proto_image_encoding) {
			return cv::imdecode(buffer, cv::IMREAD_COLOR);
		}
		else {
			return cv::Mat( // Создание матрицы из raw данных
				image_data.height(), // Высота изображения
				image_data.width(),  // Ширина изображения
				CV_8UC3,             // Формат: 3 канала по 8 бит
				(void*)data.data()   // Указатель на данные
			).clone();               // Создание копии данных
		}
	}
	// Создание protobuf сообщения с изображением из cv::Mat
    video_processing::ImageData create_image_data(const cv::Mat& image) {
        video_processing::ImageData image_data;  // Создание объекта для хранения изображения

        // Кодируем изображение в JPEG
        std::vector<uchar> buffer;  // Буфер для сжатых данных
        std::vector<int> compression_params = { cv::IMWRITE_JPEG_QUALITY, cap_quality };  // Параметры сжатия (качество 80%)
        cv::imencode(".jpg", image, buffer, compression_params);  // Кодирование в JPEG

        // Заполнение полей protobuf сообщения
        image_data.set_width(image.cols);                         // Ширина изображения
        image_data.set_height(image.rows);                        // Высота изображения
        image_data.set_pixel_format(proto_pixel_format);       // Формат пикселей (BGR)
        image_data.set_encoding(proto_image_encoding);          // Формат кодирования (JPEG)
        image_data.set_image_data(buffer.data(), buffer.size());  // Бинарные данные изображения

        return image_data;  // Возврат заполненного объекта
    }

    // Сохранение изображений для отладки
    void save_debug_images(const video_processing::VideoFrame& frame,
        const cv::Mat& original, const cv::Mat& processed) {
        // Формирование имен файлов с ID кадра
        std::string original_filename = temp_dir + "\\original_" +
            std::to_string(frame.frame_id()) + ".jpg";
        std::string processed_filename = temp_dir + "\\processed_" +
            std::to_string(frame.frame_id()) + ".jpg";

        // Сохранение изображений в файлы
        cv::imwrite(original_filename, original);
        cv::imwrite(processed_filename, processed);

        // Вывод информации о сохранении
        std::cout << "Debug saved: " << frame.frame_id() << std::endl;
    }

public:
    // Основной цикл работы worker'а
    void run() {
        std::cout << "=== Worker Started ===" << std::endl;
        std::cout << std::endl << "=== 1-No buffer limits ===" << std::endl << std::endl;
        std::cout << "4. Listening from: " << capturer_address << std::endl;
        std::cout << "5. Sending to: " << composer_address << std::endl;

        // Бесконечный цикл обработки сообщений
        while (true) {
            try {
                zmq::message_t message;  // Сообщение ZeroMQ

                // Получение сообщения от Capturer'а
                if (pull_socket.recv(&message)) {
                    // Десериализуем сообщение от Capturer в protobuf формат
                    video_processing::VideoFrame input_frame;
                    if (!input_frame.ParseFromArray(message.data(), message.size())) {
                        std::cout << "- [FAIL] Failed to parse message from Capturer" << std::endl;
                        continue;  // Пропуск невалидного сообщения
                    }

                    // Вывод информации о полученном кадре
                    std::cout << "- [ OK ] Processing frame: " << input_frame.frame_id()
                        << " from: " << input_frame.sender_id() << std::endl;

                    // Обработка сообщения с одним изображением
                    if (input_frame.has_single_image()) {
                        // Извлечение изображения из сообщения
                        cv::Mat original_image = extract_image(input_frame.single_image());

                        // Проверка что изображение не пустое
                        if (!original_image.empty()) {
                            // Применяем эффект Scanner Darkly к изображению
                            cv::Mat processed_image = effect.applyEffect(original_image);

                            // Сохраняем изображения для отладки
                            //save_debug_images(input_frame, original_image, processed_image);

                            // Показываем превью (закомментировано)
                            //cv::imshow("Worker - Original", original_image);
                            //cv::imshow("Worker - Processed", processed_image);
                            cv::waitKey(1);  // Обработка событий OpenCV

                            // Создаем сообщение для Composer с результатами
                            video_processing::VideoFrame output_frame;

                            // Копируем метаданные из входного сообщения
                            output_frame.set_frame_id(input_frame.frame_id());
                            output_frame.set_timestamp(input_frame.timestamp());
                            output_frame.set_sender_id(worker_id);
                            output_frame.set_frame_type(video_processing::PROCESSED_FRAME);

                            // Добавляем оба изображения (оригинал и обработанное)
                            auto* image_pair = output_frame.mutable_image_pair();
                            *image_pair->mutable_original() = create_image_data(original_image);
                            *image_pair->mutable_processed() = create_image_data(processed_image);

                            // Сериализуем и отправляем сообщение в Composer
                            std::string serialized = output_frame.SerializeAsString();
                            zmq::message_t output_message(serialized.size());
                            memcpy(output_message.data(), serialized.data(), serialized.size());
                            push_socket.send(output_message);

                            // Вывод информации об отправке
                            std::cout << "- [ OK ] Sent processed frame to Composer: " << output_frame.frame_id() << std::endl;
                        }
                    }
                }
            }
            catch (const zmq::error_t& e) {
                if (e.num() != EINTR) {
                    std::cout << "- [FAIL] ZMQ error: " << e.what() << std::endl;
                }
                break;
            }
            catch (const std::exception& e) {
                std::cout << "- [FAIL] Processing error: " << e.what() << std::endl;
            }
        }
    }
};

// Главная функция программы
int main() {
    try {
        Worker worker;  // Создание экземпляра worker'а
        worker.run();   // Запуск основного цикла
        return 0;       // Успешное завершение
    }
    catch (const std::exception& e) {
        // Обработка исключений в main
        std::cout << "- [FAIL] Worker error: " << e.what() << std::endl;
        return -1;  // Завершение с ошибкой
    }
}