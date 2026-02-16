#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

class ScannerDarklyEffect {
private:
	int canny_low_threshold_ = 50;
	int canny_high_threshold_ = 150;
	int gaussian_kernel_size_ = 3;  // Уменьшил для меньшего размытия
	int dilation_kernel_size_ = 1;  // Уменьшил для тонких контуров
	int color_quantization_levels_ = 8;
	bool black_contours_ = true;    // Флаг для черных контуров

public:
	ScannerDarklyEffect() = default;

	void setCannyThresholds(int low, int high) {
		canny_low_threshold_ = low;
		canny_high_threshold_ = high;
	}

	void setGaussianKernelSize(int size) {
		gaussian_kernel_size_ = size;
	}

	void setDilationKernelSize(int size) {
		dilation_kernel_size_ = size;
	}

	void setColorQuantizationLevels(int levels) {
		color_quantization_levels_ = levels;
	}

	void setBlackContours(bool black) {
		black_contours_ = black;
	}

	cv::Mat applyEffect(const cv::Mat& input_frame) {
		if (input_frame.empty()) {
			throw std::invalid_argument("Input frame is empty");
		}

		// 1. Упрощение цветов
		cv::Mat quantized = colorQuantization(input_frame);

		// 2. Выделение контуров (более тонких)
		cv::Mat edges = extractEdges(input_frame);

		// 3. Комбинирование с черными контурами
		cv::Mat result = combineEffect(quantized, edges);

		return result;
	}

private:
	cv::Mat colorQuantization(const cv::Mat& image) {
		cv::Mat data = image.reshape(1, image.rows * image.cols);
		data.convertTo(data, CV_32F);

		std::vector<int> labels;
		cv::Mat centers;

		cv::kmeans(data, color_quantization_levels_, labels,
			cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 10, 1.0),
			3, cv::KMEANS_PP_CENTERS, centers);

		cv::Mat quantized(image.size(), image.type());
		for (int i = 0; i < image.rows * image.cols; i++) {
			int cluster_idx = labels[i];
			cv::Vec3b new_color = centers.at<cv::Vec3f>(cluster_idx);
			quantized.at<cv::Vec3b>(i) = cv::Vec3b(
				static_cast<uchar>(new_color[0]),
				static_cast<uchar>(new_color[1]),
				static_cast<uchar>(new_color[2])
			);
		}

		return quantized;
	}

	cv::Mat extractEdges(const cv::Mat& image) {
		cv::Mat gray, blur, edges;

		// Конвертация в grayscale
		cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

		// Меньшее размытие для более четких контуров
		cv::GaussianBlur(gray, blur,
			cv::Size(gaussian_kernel_size_, gaussian_kernel_size_), 0);

		// Детекция границ Кэнни
		cv::Canny(blur, edges, canny_low_threshold_, canny_high_threshold_);

		// Убрал dilation для тонких контуров
		// Если нужны чуть толще контуры, можно раскомментировать:
		/*
		if (dilation_kernel_size_ > 0) {
			cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT,
							cv::Size(dilation_kernel_size_, dilation_kernel_size_));
			cv::dilate(edges, edges, kernel);
		}
		*/

		return edges;
	}

	cv::Mat combineEffect(const cv::Mat& quantized, const cv::Mat& edges) {
		cv::Mat result = quantized.clone();

		if (black_contours_) {
			// ЧЕРНЫЕ КОНТУРЫ: просто рисуем черные линии поверх
			result.setTo(cv::Scalar(0, 0, 0), edges); // Черный цвет где есть контуры
		}
		else {
			// БЕЛЫЕ КОНТУРЫ (старый вариант)
			cv::Mat white_edges_bgr;
			cv::cvtColor(edges, white_edges_bgr, cv::COLOR_GRAY2BGR);
			result += white_edges_bgr;
		}

		return result;
	}
};