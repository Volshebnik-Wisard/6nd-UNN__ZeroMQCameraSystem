#include <iostream>
#include <string>
#include <opencv2/opencv.hpp>
#include "scanner_darkly_effect.hpp"

int main() {
	std::cout << "=== Scanner Darkly Effect - Comparison ===" << std::endl;

	std::string input_path = "C:/test/input.jpg";

	cv::Mat input_image = cv::imread(input_path);
	if (input_image.empty()) {
		std::cerr << "Error: Could not load image!" << std::endl;
		return -1;
	}

	// Создаем разные варианты эффекта
	ScannerDarklyEffect effect1, effect2, effect3;

	// Вариант 1: Тонкие черные контуры
	effect1.setCannyThresholds(60, 160);
	effect1.setGaussianKernelSize(3);
	effect1.setDilationKernelSize(0);
	effect1.setColorQuantizationLevels(8);
	effect1.setBlackContours(true);

	// Вариант 2: Чуть толще черные контуры
	effect2.setCannyThresholds(50, 150);
	effect2.setGaussianKernelSize(5);
	effect2.setDilationKernelSize(1);
	effect2.setColorQuantizationLevels(8);
	effect2.setBlackContours(true);

	// Вариант 3: Белые контуры (оригинальный)
	effect3.setCannyThresholds(50, 150);
	effect3.setGaussianKernelSize(5);
	effect3.setDilationKernelSize(2);
	effect3.setColorQuantizationLevels(8);
	effect3.setBlackContours(false);

	// Применяем все эффекты
	cv::Mat result1 = effect1.applyEffect(input_image);
	cv::Mat result2 = effect2.applyEffect(input_image);
	cv::Mat result3 = effect3.applyEffect(input_image);

	// Сохраняем результаты
	cv::imwrite("C:/test/effect_thin_black.jpg", result1);
	cv::imwrite("C:/test/effect_thick_black.jpg", result2);
	cv::imwrite("C:/test/effect_white.jpg", result3);

	// Показываем сравнение
	cv::imshow("Original", input_image);
	cv::imshow("Thin BLACK Contours", result1);
	cv::imshow("Thick BLACK Contours", result2);
	cv::imshow("WHITE Contours", result3);

	std::cout << "All effects applied and saved!" << std::endl;
	std::cout << "1. Thin BLACK contours: effect_thin_black.jpg" << std::endl;
	std::cout << "2. Thick BLACK contours: effect_thick_black.jpg" << std::endl;
	std::cout << "3. WHITE contours: effect_white.jpg" << std::endl;
	std::cout << "Press any key to exit..." << std::endl;

	cv::waitKey(0);
	return 0;
}