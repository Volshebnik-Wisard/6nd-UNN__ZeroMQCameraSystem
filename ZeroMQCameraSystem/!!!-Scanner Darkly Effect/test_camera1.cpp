#include <iostream>
#include <opencv2/opencv.hpp>
#include "scanner_darkly_effect.hpp"

int main() {
	std::cout << "=== Scanner Darkly Effect - Web Camera Test ===" << std::endl;
	std::cout << "Press 'q' or ESC to exit" << std::endl;

	// Открытие веб-камеры
	cv::VideoCapture cap(0); // 0 - первая камера
	if (!cap.isOpened()) {
		std::cerr << "Error: Could not open web camera!" << std::endl;
		return -1;
	}

	// Установка разрешения
	cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
	cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

	ScannerDarklyEffect effect;
	effect.setCannyThresholds(50, 150);
	effect.setGaussianKernelSize(5);
	effect.setDilationKernelSize(2);
	effect.setColorQuantizationLevels(8);

	cv::Mat frame, result;

	while (true) {
		cap >> frame;
		if (frame.empty()) {
			std::cerr << "Error: Could not grab frame from camera!" << std::endl;
			break;
		}

		// Применение эффекта в реальном времени
		result = effect.applyEffect(frame);

		// Отображение результатов
		cv::imshow("Original Camera", frame);
		cv::imshow("Scanner Darkly Effect - Live", result);

		// Выход по нажатию 'q' или ESC
		char key = cv::waitKey(1);
		if (key == 'q' || key == 27) {
			break;
		}
	}

	cap.release();
	cv::destroyAllWindows();
	std::cout << "Camera test completed!" << std::endl;

	return 0;
}