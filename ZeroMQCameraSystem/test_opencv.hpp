#include <iostream>
#include <opencv2/opencv.hpp>

int test_opencv() {
	std::cout << "OpenCV test: Successful!" << std::endl;
	std::cout << "OpenCV version: " << CV_VERSION << std::endl;

	// Просто создаем и показываем изображение
	cv::Mat image(100, 200, CV_8UC3, cv::Scalar(0, 255, 0));
	cv::putText(image, "OpenCV", cv::Point(10, 50),
		cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);

	cv::imshow("OpenCV Test", image);
	cv::waitKey(0);

	return 0;
}