#pragma once
#include <iostream>
#include <zmq.hpp>

int test_zmq() {
	std::cout << "Testing ZeroMQ..." << std::endl;

	try {
		// Просто создаем контекст и сокет
		zmq::context_t ctx;
		zmq::socket_t socket(ctx, ZMQ_REQ);

		std::cout << "ZeroMQ test: Successful!" << std::endl;
		std::cout << "Libraries loaded correctly." << std::endl;
	}
	catch (const std::exception& e) {
		std::cout << "FAILED: " << e.what() << std::endl;
		return 1;
	}

	/*
	std::cout << "Press Enter to exit...";
	std::cin.get();
	*/
}
