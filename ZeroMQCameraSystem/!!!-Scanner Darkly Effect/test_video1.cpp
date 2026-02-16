#include <iostream>
#include <string>
#include <opencv2/opencv.hpp>
#include "scanner_darkly_effect.hpp"

int main() {
	std::cout << "=== Scanner Darkly Effect - Video Test ===" << std::endl;

	std::string input_path = "C:/test/input_video.avi";    // Измените на ваш путь
	std::string output_path = "C:/test/output_video.avi";  // Измените на ваш путь

	std::cout << "Input video: " << input_path << std::endl;
	std::cout << "Output video: " << output_path << std::endl;

	cv::VideoCapture cap(input_path);
	if (!cap.isOpened()) {
		std::cerr << "Error opening video stream or file: " << input_path << std::endl;
		std::cerr << "Check if the file exists and path is correct!" << std::endl;
		return -1;
	}

	// Получение параметров видео
	int frame_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
	int frame_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
	double fps = cap.get(cv::CAP_PROP_FPS);

	std::cout << "Video info: " << frame_width << "x" << frame_height << " at " << fps << " FPS" << std::endl;

	// Создание VideoWriter
	cv::VideoWriter out(output_path,
		cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
		fps,
		cv::Size(frame_width, frame_height));

	if (!out.isOpened()) {
		std::cerr << "Error creating video writer for: " << output_path << std::endl;
		return -1;
	}

	ScannerDarklyEffect effect;
	effect.setCannyThresholds(50, 150);
	effect.setGaussianKernelSize(5);
	effect.setDilationKernelSize(2);
	effect.setColorQuantizationLevels(8);

	cv::Mat frame, result;
	int frame_count = 0;

	std::cout << "Processing video..." << std::endl;
	std::cout << "Press 'q' or ESC to stop processing" << std::endl;

	while (true) {
		cap >> frame;
		if (frame.empty()) {
			std::cout << "\nEnd of video reached" << std::endl;
			break;
		}

		// Применение эффекта
		result = effect.applyEffect(frame);

		// Запись кадра
		out.write(result);

		// Отображение
		cv::imshow("Scanner Darkly Effect - Video", result);

		frame_count++;
		std::cout << "Processed frame: " << frame_count << "\r" << std::flush;

		// Выход по нажатию 'q' или ESC
		char key = cv::waitKey(1);
		if (key == 'q' || key == 27) {
			std::cout << "\nProcessing stopped by user" << std::endl;
			break;
		}
	}

	cap.release();
	out.release();
	cv::destroyAllWindows();

	std::cout << "Video processing completed!" << std::endl;
	std::cout << "Total frames processed: " << frame_count << std::endl;
	std::cout << "Output saved to: " << output_path << std::endl;

	return 0;
}