#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <stdio.h>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <thread>

using namespace cv;
using namespace std;

//cout << "Server IP: 192.168.9.61" << endl;
//imshow("Publisher - 192.168.9.61", frame);

#pragma warning(disable : 4996) // Отключение предупреждений для VS (localtime)
int pub_test() {
	try {
		// ZMQ контекст - управляет всеми сокетами в программе
		zmq::context_t context(1); // 1 = количество потоков ввода-вывода
		// PUB (publisher) сокет - рассылает сообщения всем подписчикам
		zmq::socket_t publisher(context, ZMQ_PUB);
		// Привязка ко всем сетевым интерфейсам, 
		string bind_address = "tcp://*:5555"; // * = все сетевые интерфейсы, 5555 = порт
		publisher.bind(bind_address); // Привязка сокета к адресу
		// Вывод информации о запуске издателя
		std::cout << "ZeroMQ Test Publisher started on: " << bind_address << std::endl;
		std::cout << "Listening on all network interfaces" << std::endl;
		std::cout << "Possible connection addresses:" << std::endl;
		std::cout << "  - tcp://localhost:5555" << std::endl;
		std::cout << "  - tcp://127.0.0.1:5555" << std::endl;
		std::cout << "  - tcp://192.168.9.61:5555" << std::endl;
		std::cout << "  - tcp://[your-local-ip]:5555" << std::endl;
		std::cout << "Press ESC to stop..." << std::endl;
		int message_count = 0; // Счетчик отправленных сообщений
		// Пауза для подключения подписчиков
		std::this_thread::sleep_for(std::chrono::seconds(2));
		// Основной цикл отправки сообщений
		while (true) {
			// Получение текущего времени
			auto now = std::chrono::system_clock::now();
			std::time_t time = std::chrono::system_clock::to_time_t(now);
			std::tm* tm = std::localtime(&time); // Преобразование в локальное время
			// Создание сообщения с временем и счетчиком
			std::stringstream ss;
			ss << "Message #" << message_count
				<< " | Time: " << std::put_time(tm, "%H:%M:%S")
				<< " | Test message from publisher";
			// Получение строки из stringstream
			std::string message_text = ss.str();
			// Создание и отправка сообщения
			zmq::message_t message(message_text.size()); // Создание сообщения нужного размера
			memcpy(message.data(), message_text.c_str(), message_text.size()); // Копирование данных
			publisher.send(message); // Отправка сообщения
			// Вывод каждого сообщения
			if (message_count) {
				std::cout << "Sent: " << message_text << std::endl;
			}
			message_count++;// Увеличение счетчика сообщений
			// Проверка ESC с задержкой 100мс
			if (waitKey(100) == 27) {
				std::cout << "Exit by ESC key. Total messages sent: " << message_count << std::endl;
				break;
			}
		}
	}
	catch (const zmq::error_t& e) {
		std::cerr << "ZeroMQ error: " << e.what() << std::endl;
		return -1;
	}
	catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}

	return 0;
}

//----------------------------------------------------------------------------------

int pub_camera() {
	// Настройка издателя для видеопотока
	zmq::context_t context(1); // Создание контекста ZeroMQ
	zmq::socket_t publisher(context, ZMQ_PUB); // Создание PUB-сокета
	try {
		publisher.bind("tcp://*:9999"); // Привязка ко всем интерфейсам на порту 9999
		cout << "ZMQ publisher bound to tcp://*:9999" << endl;
	}
	catch (const zmq::error_t& e) {
		cout << "ZMQ bind error: " << e.what() << endl;
		return -1;
	}
	// Настройка захвата с USB-камеры
	VideoCapture cap; // Объект для захвата видео
	int camId = 0; // ID камеры по умолчанию
	// Поиск работающей камеры
	for (int i = 0; i < 10; i++) {
		cap.open(i); // Попытка открыть камеру с ID i
		if (cap.isOpened()) {
			camId = i; // Сохранение ID работающей камеры
			cout << "Camera found at ID: " << camId << endl;
			break;
		}
		if (i == 9) {
			cout << "No camera found!" << endl;
			return -1;
		}
	}
	// Установка разрешения камеры
	cap.set(CAP_PROP_FRAME_WIDTH, 640);
	cap.set(CAP_PROP_FRAME_HEIGHT, 480);
	Mat frame; // Матрица для хранения кадра
	vector<uchar> buffer; // Буфер для сжатого изображения
	// Параметры сжатия JPEG
	vector<int> compression_params;
	compression_params.push_back(IMWRITE_JPEG_QUALITY); // Параметр качества
	compression_params.push_back(80); // Качество 80%
	cout << "Starting video stream. Press ESC to exit." << endl;
	// Основной цикл захвата и отправки видеопотока
	while (true) {
		// Захват кадра с камеры
		bool grabSuccess = cap.read(frame);
		if (!grabSuccess || frame.empty()) {
			cout << "Failed to grab a frame" << endl;
			break;
		}
		// Локальный просмотр кадра
		imshow("C++ Publisher - Local View", frame);
		// ZMQ publisher - кодирование и отправка кадра
		try {
			// Кодирование кадра в JPEG
			bool encodeSuccess = imencode(".jpg", frame, buffer, compression_params);
			if (!encodeSuccess) {
				cout << "Failed to encode frame" << endl;
				continue; // Пропуск неудачного кадра
			}
			// Отправка кадра через ZeroMQ
			zmq::message_t message(buffer.size()); // Создание сообщения
			memcpy(message.data(), buffer.data(), buffer.size()); // Копирование данных
			// Блокирующая отправка. Без флагов (самый совместимый)
			publisher.send(message);
		}
		catch (const zmq::error_t& e) {
			cout << "ZMQ send error: " << e.what() << endl;
		}
		// Проверка ESC с задержкой 10мс
		if (waitKey(10) == 27) {
			cout << "Exit by ESC key" << endl;
			break;
		}
	}
	// Освобождение ресурсов
	cap.release(); // Освобождение камеры
	destroyAllWindows(); // Закрытие всех окон OpenCV

	return 0;
}