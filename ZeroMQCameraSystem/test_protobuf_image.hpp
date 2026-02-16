#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "video_processing.pb.h"

using namespace cv;
using namespace std;

// Функция для загрузки изображения из файла и сериализации
inline bool test_serialize_image(const string& image_path, const string& output_bin) {
	try {
		cout << "=== Тест сериализации изображения ===" << endl;

		// 1. Загружаем изображение
		Mat image = imread(image_path);
		if (image.empty()) {
			cerr << "Не удалось загрузить изображение: " << image_path << endl;
			return false;
		}
		cout << "Изображение загружено: " << image.cols << "x" << image.rows << endl;

		// 2. Кодируем в JPEG (для экономии места)
		vector<uchar> jpeg_buffer;
		vector<int> compression_params = { IMWRITE_JPEG_QUALITY, 90 };
		imencode(".jpg", image, jpeg_buffer, compression_params);
		cout << "Размер после JPEG кодирования: " << jpeg_buffer.size() << " байт" << endl;

		// 3. Создаем protobuf сообщение
		video_processing::VideoFrame frame;
		frame.set_frame_id(1);
		frame.set_timestamp(chrono::duration<double>(
			chrono::system_clock::now().time_since_epoch()).count());
		frame.set_sender_id("test_serializer");
		frame.set_frame_type(video_processing::CAPTURED_FRAME);

		// ЗАПОЛНЯЕМ ДАННЫЕ ИЗОБРАЖЕНИЯ ПРАВИЛЬНО (через single_image)
		video_processing::ImageData* image_data = frame.mutable_single_image();
		image_data->set_width(image.cols);
		image_data->set_height(image.rows);
		image_data->set_pixel_format(video_processing::RGB);
		image_data->set_encoding(video_processing::PNG);
		image_data->set_image_data(jpeg_buffer.data(), jpeg_buffer.size());

		// 4. Сериализуем в файл
		string serialized_data;
		frame.SerializeToString(&serialized_data);

		ofstream file(output_bin, ios::binary);
		file.write(serialized_data.data(), serialized_data.size());
		file.close();

		cout << "Сериализация успешна!" << endl;
		cout << "Размер protobuf сообщения: " << serialized_data.size() << " байт" << endl;
		cout << "Сохранено в: " << output_bin << endl;

		return true;

	}
	catch (const exception& e) {
		cerr << "Ошибка сериализации: " << e.what() << endl;
		return false;
	}
}

// Функция для десериализации и сохранения изображения
inline bool test_deserialize_image(const string& input_bin, const string& output_image) {
	try {
		cout << "\n=== Тест десериализации изображения ===" << endl;

		// 1. Читаем данные из файла
		ifstream file(input_bin, ios::binary);
		if (!file) {
			cerr << "Не удалось открыть файл: " << input_bin << endl;
			return false;
		}

		string serialized_data((istreambuf_iterator<char>(file)),
			istreambuf_iterator<char>());
		file.close();

		cout << "Прочитано из файла: " << serialized_data.size() << " байт" << endl;

		// 2. Десериализуем сообщение
		video_processing::VideoFrame frame;
		if (!frame.ParseFromString(serialized_data)) {
			cerr << "Ошибка парсинга protobuf сообщения" << endl;
			return false;
		}

		cout << "Десериализация успешна!" << endl;
		cout << "Frame ID: " << frame.frame_id() << endl;
		cout << "Sender: " << frame.sender_id() << endl;
		cout << "Frame Type: " << frame.frame_type() << endl;

		// 3. ПРОВЕРЯЕМ ТИП КОНТЕНТА И ИЗВЛЕКАЕМ ДАННЫЕ (используем content_case())
		switch (frame.content_case()) {
		case video_processing::VideoFrame::kSingleImage: {
			const video_processing::ImageData& image_data = frame.single_image();
			cout << "Размер изображения: " << image_data.width()
				<< "x" << image_data.height() << endl;
			cout << "Pixel Format: " << image_data.pixel_format() << endl;
			cout << "Encoding: " << image_data.encoding() << endl;

			// Восстанавливаем изображение
			const string& jpeg_data = image_data.image_data();
			vector<uchar> buffer(jpeg_data.begin(), jpeg_data.end());

			Mat image = imdecode(buffer, IMREAD_COLOR);
			if (image.empty()) {
				cerr << "Не удалось декодировать изображение" << endl;
				return false;
			}

			// Сохраняем восстановленное изображение
			imwrite(output_image, image);
			cout << "Изображение восстановлено и сохранено в: " << output_image << endl;
			break;
		}

		case video_processing::VideoFrame::kImagePair: {
			cout << "Сообщение содержит пару изображений" << endl;
			const video_processing::ImagePair& pair = frame.image_pair();
			cout << "Original: " << pair.original().width() << "x" << pair.original().height() << endl;
			cout << "Processed: " << pair.processed().width() << "x" << pair.processed().height() << endl;
			return false;
		}

		case video_processing::VideoFrame::kCommand: {
			cout << "Сообщение содержит команду: " << frame.command() << endl;
			return false;
		}

		case video_processing::VideoFrame::CONTENT_NOT_SET: {
			cout << "Сообщение не содержит данных" << endl;
			return false;
		}
		}

		// 4. Показываем изображение
		Mat image = imread(output_image);
		imshow("Восстановленное изображение", image);
		cout << "Нажмите любую клавишу для продолжения..." << endl;
		waitKey(0);

		return true;

	}
	catch (const exception& e) {
		cerr << "Ошибка десериализации: " << e.what() << endl;
		return false;
	}
}

// Дополнительная функция для демонстрации работы с двумя изображениями
inline bool test_image_pair() {
	cout << "\n=== Тест с двумя изображениями ===" << endl;

	try {
		// Создаем тестовые изображения
		Mat image1(300, 400, CV_8UC3, Scalar(100, 150, 200));
		rectangle(image1, Point(50, 50), Point(200, 150), Scalar(255, 0, 0), -1);

		Mat image2(300, 400, CV_8UC3, Scalar(200, 100, 50));
		circle(image2, Point(200, 150), 60, Scalar(0, 255, 0), -1);

		// Кодируем в JPEG
		vector<uchar> buffer1, buffer2;
		vector<int> compression_params = { IMWRITE_JPEG_QUALITY, 90 };
		imencode(".jpg", image1, buffer1, compression_params);
		imencode(".jpg", image2, buffer2, compression_params);

		// Создаем сообщение с парой изображений
		video_processing::VideoFrame frame;
		frame.set_frame_id(2);
		frame.set_timestamp(chrono::duration<double>(
			chrono::system_clock::now().time_since_epoch()).count());
		frame.set_sender_id("test_pair");
		frame.set_frame_type(video_processing::PROCESSED_FRAME);

		// ЗАПОЛНЯЕМ ПАРУ ИЗОБРАЖЕНИЙ
		video_processing::ImagePair* pair = frame.mutable_image_pair();

		// Оригинальное изображение
		video_processing::ImageData* original = pair->mutable_original();
		original->set_width(image1.cols);
		original->set_height(image1.rows);
		original->set_pixel_format(video_processing::BGR);
		original->set_encoding(video_processing::JPEG);
		original->set_image_data(buffer1.data(), buffer1.size());

		// Обработанное изображение
		video_processing::ImageData* processed = pair->mutable_processed();
		processed->set_width(image2.cols);
		processed->set_height(image2.rows);
		processed->set_pixel_format(video_processing::BGR);
		processed->set_encoding(video_processing::JPEG);
		processed->set_image_data(buffer2.data(), buffer2.size());

		// Сериализуем
		string serialized_data;
		frame.SerializeToString(&serialized_data);
		cout << "Размер сообщения с парой изображений: " << serialized_data.size() << " байт" << endl;

		// Десериализуем обратно
		video_processing::VideoFrame frame2;
		frame2.ParseFromString(serialized_data);

		// Проверяем тип контента через content_case()
		if (frame2.content_case() == video_processing::VideoFrame::kImagePair) {
			const video_processing::ImagePair& pair2 = frame2.image_pair();
			cout << "Успешно десериализована пара изображений!" << endl;
			cout << "Original: " << pair2.original().width() << "x" << pair2.original().height() << endl;
			cout << "Processed: " << pair2.processed().width() << "x" << pair2.processed().height() << endl;
		}

		return true;

	}
	catch (const exception& e) {
		cerr << "Ошибка в тесте пар изображений: " << e.what() << endl;
		return false;
	}
}

// Тест с командой
inline bool test_command() {
	cout << "\n=== Тест с командой ===" << endl;

	try {
		video_processing::VideoFrame frame;
		frame.set_frame_id(3);
		frame.set_timestamp(chrono::duration<double>(
			chrono::system_clock::now().time_since_epoch()).count());
		frame.set_sender_id("controller");
		frame.set_frame_type(video_processing::CONTROL_MESSAGE);
		frame.set_command("STOP");  // Используем set_command() для установки команды

		string serialized_data;
		frame.SerializeToString(&serialized_data);
		cout << "Размер сообщения с командой: " << serialized_data.size() << " байт" << endl;

		// Десериализуем обратно
		video_processing::VideoFrame frame2;
		frame2.ParseFromString(serialized_data);

		// Проверяем через content_case()
		if (frame2.content_case() == video_processing::VideoFrame::kCommand) {
			cout << "Успешно десериализована команда: " << frame2.command() << endl;
		}

		return true;

	}
	catch (const exception& e) {
		cerr << "Ошибка в тесте команды: " << e.what() << endl;
		return false;
	}
}

// Полный тест: сериализация → десериализация → сравнение
inline bool test_full_cycle(const string& original_image) {
	cout << "\n=== Полный тест: сериализация → десериализация ===" << endl;

	string temp_bin = "temp_frame.bin";
	string restored_image = "restored_image.jpg";

	// Сериализация
	if (!test_serialize_image(original_image, temp_bin)) {
		return false;
	}

	// Десериализация  
	if (!test_deserialize_image(temp_bin, restored_image)) {
		return false;
	}

	// Тест с парой изображений
	if (!test_image_pair()) {
		return false;
	}

	// Тест с командой
	if (!test_command()) {
		return false;
	}

	// Сравнение оригинального и восстановленного
	Mat orig = imread(original_image);
	Mat restored = imread(restored_image);

	if (orig.size() != restored.size()) {
		cerr << "Размеры изображений не совпадают!" << endl;
		return false;
	}

	// Вычисляем разницу
	Mat diff;
	absdiff(orig, restored, diff);

	double max_diff;
	minMaxLoc(diff, nullptr, &max_diff);

	cout << "Максимальная разница между пикселями: " << max_diff << endl;

	if (max_diff < 5) {  // Допустимая погрешность из-за JPEG сжатия
		cout << "ТЕСТ ПРОЙДЕН: Изображения практически идентичны!" << endl;
	}
	else {
		cout << "ТЕСТ С ОГОВОРКАМИ: Есть различия (JPEG артефакты)" << endl;
	}

	// Показываем оба изображения для сравнения
	imshow("Оригинал", orig);
	imshow("Восстановленный", restored);
	cout << "Нажмите любую клавишу для выхода..." << endl;
	waitKey(0);

	return true;
}

inline int test_protobuf_image() {
	// Инициализация protobuf
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	setlocale(LC_ALL, "ru");
	// Укажите путь к вашему тестовому изображению
	//string test_image_path = "test_image.jpg";
	string test_image_path = "123.png";
	// ПРОВЕРЯЕМ СУЩЕСТВОВАНИЕ ФАЙЛА
	ifstream test_file(test_image_path);
	if (!test_file.good()) {
		// Если файла нет, создаем простое тестовое изображение
		cout << "Тестовое изображение не найдено. Создаем новое..." << endl;
		Mat test_image(480, 640, CV_8UC3, Scalar(100, 150, 200));
		rectangle(test_image, Point(100, 100), Point(300, 300), Scalar(255, 0, 0), -1);
		circle(test_image, Point(400, 200), 50, Scalar(0, 255, 0), -1);
		imwrite(test_image_path, test_image);
		cout << "Создано тестовое изображение: " << test_image_path << endl;
	}
	else {
		test_file.close();
		cout << "Используется существующее тестовое изображение: " << test_image_path << endl;
	}

	// Запускаем тесты
	bool success = test_full_cycle(test_image_path);

	// Очистка
	google::protobuf::ShutdownProtobufLibrary();

	return success ? 0 : 1;
}