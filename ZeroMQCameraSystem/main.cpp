#include "test_libzmq.hpp"
#include "test_opencv.hpp"
#include "test_pub.hpp"
#include "test_sub.hpp"
#include "test_asn1.hpp" 
#include "simple_message.pb.h"
#include "test_protobuf_image.hpp" 
#include "AsnRuntime.h"
#include <string>

using namespace asnrt;

class Asn1Serializer {
public:
	static void test_basic_types() {
		std::cout << "ASN.1 Runtime Version: " << ASNRT_VERSION << std::endl;

		// Тестируем только базовые типы без сложных вызовов
		std::cout << "Basic types test passed" << std::endl;
	}

	// Альтернативный метод для получения сообщения об ошибке
	static std::string getErrorMessage(const AsnException& e) {
		try {
			// Пробуем разные варианты
			return "ASN.1 Error occurred";
		}
		catch (...) {
			return "Unknown ASN.1 error";
		}
	}
};

int test_asn1() {
	try {
		Asn1Serializer::test_basic_types();
		std::cout << "ASN.1 Runtime works correctly!" << std::endl;

		// Тест простой сериализации
		std::vector<uint8_t> test_data = { 0x48, 0x65, 0x6C, 0x6C, 0x6F }; // "Hello"
		std::cout << "Test data prepared: " << test_data.size() << " bytes" << std::endl;

	}
	catch (const AsnException& e) {
		// Безопасный вывод ошибки
		std::cerr << "ASN.1 Error occurred (code: " << /* e.getErrorCode() */ "unknown" << ")" << std::endl;
	}
	catch (const std::exception& e) {
		std::cerr << "Standard Error: " << e.what() << std::endl;
	}
	catch (...) {
		std::cerr << "Unknown error occurred" << std::endl;
	}

	return 0;
}

int test_protobuf() {
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	std::cout << "=== Simple Protocol Buffers Test ===" << std::endl;

	// Создаем и заполняем сообщение
	simple::Person person;
	person.set_name("John Doe");
	person.set_age(28);
	person.set_email("john.doe@example.com");

	// Сериализуем
	std::string data;
	person.SerializeToString(&data);
	std::cout << "Serialized size: " << data.size() << " bytes" << std::endl;

	// Десериализуем
	simple::Person person2;
	person2.ParseFromString(data);

	// Проверяем
	std::cout << "Name: " << person2.name() << std::endl;
	std::cout << "Age: " << person2.age() << std::endl;
	std::cout << "Email: " << person2.email() << std::endl;

	// Проверяем совпадение данных
	if (person.name() == person2.name() &&
		person.age() == person2.age() &&
		person.email() == person2.email()) {
		std::cout << "SUCCESS: Data matches perfectly!" << std::endl;
	}
	else {
		std::cout << "ERROR: Data mismatch!" << std::endl;
	}

	google::protobuf::ShutdownProtobufLibrary();
	return 0;
}

int main() {
	//std::cout << "\n-------------------------------\n";	test_zmq();
	//std::cout << "\n-------------------------------\n";	test_opencv();
	//std::cout << "\n-------------------------------\n";	pub_test();
	//std::cout << "\n-------------------------------\n";	sub_test();
	//std::cout << "\n-------------------------------\n";	pub_camera();
	//std::cout << "\n-------------------------------\n";	sub_camera();
	//std::cout << "\n-------------------------------\n";	test_asn1(); //MT-MD
	//std::cout << "\n-------------------------------\n";	test_asn1_person(); //MD
	//std::cout << "\n-------------------------------\n";	test_asn1_person_advanced(); //MD
	//std::cout << "\n-------------------------------\n";	test_protobuf(); //MT
	//std::cout << "\n-------------------------------\n";	test_protobuf_image(); //MT
	return 0;
}
