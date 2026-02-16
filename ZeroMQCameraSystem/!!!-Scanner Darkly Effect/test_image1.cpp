#include <iostream>
#include <string>
#include <opencv2/opencv.hpp>
#include "scanner_darkly_effect.hpp"

int main() {
	std::cout << "=== Scanner Darkly Effect - Image Test ===" << std::endl;

	std::string input_path = "C:/test/input.jpg";  // Измените на ваш путь
	std::string output_path = "C:/test/output.jpg"; // Измените на ваш путь

	std::cout << "Input path: " << input_path << std::endl;
	std::cout << "Output path: " << output_path << std::endl;

	try {
		// Загрузка изображения
		cv::Mat input_image = cv::imread(input_path);
		if (input_image.empty()) {
			std::cerr << "Error: Could not load image from " << input_path << std::endl;
			std::cerr << "Check if the file exists and path is correct!" << std::endl;
			return -1;
		}

		std::cout << "Image loaded successfully: " << input_image.cols << "x" << input_image.rows << std::endl;

		// Создание и настройка эффекта
		ScannerDarklyEffect effect;
		effect.setCannyThresholds(50, 150);
		effect.setGaussianKernelSize(5);
		effect.setDilationKernelSize(2);
		effect.setColorQuantizationLevels(8);

		// Применение эффекта
		std::cout << "Applying Scanner Darkly effect..." << std::endl;
		cv::Mat output_image = effect.applyEffect(input_image);
		std::cout << "Effect applied successfully!" << std::endl;

		// Сохранение результата
		bool success = cv::imwrite(output_path, output_image);
		if (success) {
			std::cout << "Result saved to: " << output_path << std::endl;
		}
		else {
			std::cerr << "Error: Could not save image to " << output_path << std::endl;
			std::cerr << "Check if output directory exists!" << std::endl;
		}

		// Показ результатов
		cv::imshow("Original Image", input_image);
		cv::imshow("Scanner Darkly Effect", output_image);

		std::cout << "Press any key to exit..." << std::endl;
		cv::waitKey(0);

	}
	catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}

	return 0;
}