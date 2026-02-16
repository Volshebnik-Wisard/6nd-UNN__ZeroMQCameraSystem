#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

class ScannerDarklyEffect {
private:
	int canny_low_threshold_ = 50; // Нижний порог детектора Кэнни
	int canny_high_threshold_ = 150; // Верхний порог детектора Кэнни
	int gaussian_kernel_size_ = 3; // Размер ядра размытия Гаусса  // Уменьшил для меньшего размытия
	int dilation_kernel_size_ = 1; // Размер ядра дилатации контуров  // Уменьшил для тонких контуров
	int color_quantization_levels_ = 8; // Количество уровней квантования цвета
	bool black_contours_ = true;  // Флаг для черных контуров

public:
	ScannerDarklyEffect() = default;  // Конструктор по умолчанию

	// Сеттеры для настройки параметров эффекта
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

		// 1. Упрощение цветов (квантование)
		cv::Mat quantized = colorQuantization(input_frame);

		// 2. Выделение контуров (более тонких)
		cv::Mat edges = extractEdges(input_frame);

		// 3. Комбинирование с черными контурами
		cv::Mat result = combineEffect(quantized, edges);

		return result;
	}

private:
	cv::Mat colorQuantization(const cv::Mat& image) {
		// Преобразование изображения в одномерный массив пикселей
		cv::Mat data = image.reshape(1, image.rows * image.cols);
		data.convertTo(data, CV_32F);  // Конвертация в float для k-means
		std::vector<int> labels;  // Метки кластеров для каждого пикселя
		cv::Mat centers;  // Центры кластеров (цвета)
		// Алгоритм k-means для квантования цвета
		cv::kmeans(data, color_quantization_levels_, labels,
			cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 10, 1.0),
			3, cv::KMEANS_PP_CENTERS, centers);
		// Создание квантованного изображения
		cv::Mat quantized(image.size(), image.type());
		for (int i = 0; i < image.rows * image.cols; i++) {
			int cluster_idx = labels[i];  // Индекс кластера для пикселя
			cv::Vec3b new_color = centers.at<cv::Vec3f>(cluster_idx);  // Новый цвет из центра кластера
			quantized.at<cv::Vec3b>(i) = cv::Vec3b(
				static_cast<uchar>(new_color[0]),  // Синий канал
				static_cast<uchar>(new_color[1]),  // Зеленый канал  
				static_cast<uchar>(new_color[2])   // Красный канал
			);
		}
		return quantized;
	}

	cv::Mat extractEdges(const cv::Mat& image) {
		cv::Mat gray, blur, edges;

		// Конвертация в оттенки серого для детектора краев
		cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

		// Размытие Гаусса для уменьшения шума // Меньшее размытие для более четких контуров
		cv::GaussianBlur(gray, blur,
			cv::Size(gaussian_kernel_size_, gaussian_kernel_size_), 0);

		// Детекция границ алгоритмом Кэнни
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
		cv::Mat result = quantized.clone(); // Клонирование квантованного изображения

		if (black_contours_) {
			// Установка черного цвета в местах контуров, рисуем черные линии поверх.
			result.setTo(cv::Scalar(0, 0, 0), edges); // Черный цвет где есть контуры
		}
		else {
			// Белые контуры (альтернативный вариант) (старый вариант)
			cv::Mat white_edges_bgr;
			cv::cvtColor(edges, white_edges_bgr, cv::COLOR_GRAY2BGR); // Конвертация в BGR
			result += white_edges_bgr; // Добавление белых контуров
		}
		return result;
	}
};