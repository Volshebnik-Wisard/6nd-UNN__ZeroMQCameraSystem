#include <iostream>
#include <vector>
#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <thread>

using namespace cv;
using namespace std;

//string connect_address = "tcp://192.168.9.61:9999";
//cout << "Client IP: 192.168.9.62" << endl;
//imshow("Subscriber - 192.168.9.62", image);

int sub_test() {
	try {
		// Создание контекста ZeroMQ
		zmq::context_t context(1); // 1 - количество потоков ввода/вывода
		// Создание SUB-сокета (подписчика)
		zmq::socket_t subscriber(context, ZMQ_SUB);
		// Пробуем разные адреса для подключения - список возможных адресов для подключения
		std::vector<string> connect_addresses = {
			"tcp://localhost:5555",      // Локальная машина
			"tcp://127.0.0.1:5555",      // Loopback
			"tcp://192.168.9.61:5555",   // Конкретный IP
			"tcp://192.168.1.61:5555"    // Альтернативный IP
		};
		bool connected = false; // Флаг успешного подключения
		string used_address; // Для хранения адреса, к которому удалось подключиться
		// Перебор всех возможных адресов для подключения
		for (const auto& address : connect_addresses) {
			try {
				subscriber.connect(address); // Попытка подключения к адресу
				subscriber.setsockopt(ZMQ_SUBSCRIBE, "", 0); // Подписка на все сообщения (пустой фильтр)
				connected = true; // Установка флага успешного подключения
				used_address = address; // Сохранение используемого адреса
				std::cout << "Successfully connected to: " << address << std::endl;
				break;
			}
			catch (const zmq::error_t& e) {
				std::cout << "Failed to connect to " << address << ": " << e.what() << std::endl;
				// Закрываем и пересоздаем сокет для следующей попытки
				subscriber.close();
				subscriber = zmq::socket_t(context, ZMQ_SUB);
			}
		}
		// Проверка, удалось ли подключиться к какому-либо адресу
		if (!connected) {
			std::cerr << "Failed to connect to any address!" << std::endl;
			return -1;
		}
		std::cout << "ZeroMQ Test Subscriber successfully connected to: " << used_address << std::endl;
		std::cout << "Waiting for messages... Press ESC to exit." << std::endl;
		int message_count = 0; // Счетчик полученных сообщений
		auto start_time = std::chrono::steady_clock::now(); // Время начала работы
		// Основной цикл приема сообщений
		while (true) {
			zmq::message_t message; // Создание объекта для хранения сообщения
			// Неблокирующий receive - попытка получить сообщение без блокировки
			if (subscriber.recv(&message, ZMQ_NOBLOCK)) {
				// Сообщение получено успешно
				std::string message_text(static_cast<char*>(message.data()), message.size());
				message_count++; // Увеличение счетчика сообщений
				// Расчет времени с начала работы
				auto current_time = std::chrono::steady_clock::now();
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
				// Вывод информации о полученном сообщении
				std::cout << "Received [" << message_count << " | " << elapsed.count() << "ms]: "
					<< message_text << std::endl;
			}
			else {
				// Если сообщений нет, небольшая пауза
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			// Проверяем нажатие ESC для выхода
			if (waitKey(1) == 27) {
				std::cout << "Exit by ESC key. Total messages received: " << message_count << std::endl;
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

int sub_camera() {
	// Настройка подписчика для видеопотока
	zmq::context_t context(1); // Создание контекста ZeroMQ
	zmq::socket_t subscriber(context, ZMQ_SUB); // Создание SUB-сокета
	subscriber.connect("tcp://localhost:9999"); // Подключение к локальному издателю
	subscriber.setsockopt(ZMQ_SUBSCRIBE, "", 0); // Подписка на все сообщения
	cout << "C++ subscriber started. Waiting for frames..." << endl;
	// Основной цикл приема видеокадров
	while (true) {
		zmq::message_t message; // Создание объекта для хранения сообщения
		// Прием данных кадра
		try {
			// Блокирующий receive - ожидание сообщения. Без флагов
			if (subscriber.recv(&message)) {
				// Преобразование данных в OpenCV матрицу
				vector<uchar> buffer(static_cast<char*>(message.data()),
					static_cast<char*>(message.data()) + message.size());
				// Декодирование JPEG изображения из буфера
				Mat image = imdecode(buffer, IMREAD_COLOR);
				// Проверка успешности декодирования
				if (image.empty()) {
					cout << "Failed to decode image" << endl;
					continue;
				}
				// Отображение кадра в окне
				imshow("C++ Subscriber Frame", image);
			}
		}
		catch (const zmq::error_t& e) {
			cout << "ZMQ receive error: " << e.what() << endl;
			break;
		}
		// Проверка нажатия ESC для выхода
		if (waitKey(1) == 27) {
			cout << "Exit by ESC key" << endl;
			break;
		}
	}

	return 0;
}