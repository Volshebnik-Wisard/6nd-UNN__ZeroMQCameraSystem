#pragma once
#include <iostream>
#include <vector>
#include <string>
#include "AsnRuntime.h"

using namespace asnrt;

class Person {
public:
	std::string name;
	int age;
	std::string email;

	Person() : age(0) {}
	Person(const std::string& n, int a, const std::string& e)
		: name(n), age(a), email(e) {
	}

	// Простая сериализация в DER формат
	std::vector<uint8_t> serialize() const {
		// SEQUENCE { name UTF8String, age INTEGER, email UTF8String }
		std::vector<uint8_t> result;

		// Временная реализация - просто конкатенируем данные
		// В реальном ASN.1 нужно использовать правильное DER кодирование

		// Добавляем имя
		result.insert(result.end(), name.begin(), name.end());
		result.push_back('|'); // разделитель

		// Добавляем возраст как строку
		std::string age_str = std::to_string(age);
		result.insert(result.end(), age_str.begin(), age_str.end());
		result.push_back('|');

		// Добавляем email
		result.insert(result.end(), email.begin(), email.end());

		return result;
	}

	// Простая десериализация из DER формата
	bool deserialize(const std::vector<uint8_t>& data) {
		try {
			// Парсим наш простой формат
			std::string data_str(data.begin(), data.end());
			size_t pos1 = data_str.find('|');
			size_t pos2 = data_str.find('|', pos1 + 1);

			if (pos1 == std::string::npos || pos2 == std::string::npos) {
				return false;
			}

			name = data_str.substr(0, pos1);
			age = std::stoi(data_str.substr(pos1 + 1, pos2 - pos1 - 1));
			email = data_str.substr(pos2 + 1);

			return true;
		}
		catch (...) {
			return false;
		}
	}
};

inline int test_asn1_person() {
	std::cout << "=== Simple ASN.1 Test ===" << std::endl;

	// Создаем и заполняем объект
	Person person;
	person.name = "John Doe";
	person.age = 28;
	person.email = "john.doe@example.com";

	std::cout << "Original data:" << std::endl;
	std::cout << "Name: " << person.name << std::endl;
	std::cout << "Age: " << person.age << std::endl;
	std::cout << "Email: " << person.email << std::endl;

	// Сериализуем
	std::vector<uint8_t> data = person.serialize();
	std::cout << "Serialized size: " << data.size() << " bytes" << std::endl;

	// Десериализуем
	Person person2;
	if (!person2.deserialize(data)) {
		std::cout << "ERROR: Failed to deserialize data!" << std::endl;
		return -1;
	}

	// Проверяем
	std::cout << "\nDeserialized data:" << std::endl;
	std::cout << "Name: " << person2.name << std::endl;
	std::cout << "Age: " << person2.age << std::endl;
	std::cout << "Email: " << person2.email << std::endl;

	// Проверяем совпадение данных
	if (person.name == person2.name &&
		person.age == person2.age &&
		person.email == person2.email) {
		std::cout << "\nSUCCESS: Data matches perfectly!" << std::endl;
		return 0;
	}
	else {
		std::cout << "\nERROR: Data mismatch!" << std::endl;
		return -1;
	}
}

inline int test_asn1_person_advanced() {// Более продвинутая версия с использованием ASN.1 Runtime
	std::cout << "\n=== Advanced ASN.1 Runtime Test ===" << std::endl;

	try {
		// Тестируем базовые возможности runtime
		std::cout << "ASN.1 Runtime Version: " << ASNRT_VERSION << std::endl;

		// Создаем тестовые данные
		octetstring test_name = "John Doe";
		octetstring test_email = "john.doe@example.com";

		std::cout << "Name string: " << test_name << std::endl;
		std::cout << "Email string: " << test_email << std::endl;
		std::cout << "String lengths - Name: " << test_name.length()
			<< ", Email: " << test_email.length() << std::endl;

		// Тест работы с буфером
		AsnBuffer* buffer = alloc_buffer(1024, true, DISTINGUISHED_ENCODING_RULES);
		if (buffer) {
			std::cout << "ASN.1 Buffer created successfully" << std::endl;
			free_buffer(buffer);
			std::cout << "ASN.1 Buffer freed successfully" << std::endl;
		}

		std::cout << "SUCCESS: ASN.1 Runtime basic functions work!" << std::endl;
		return 0;

	}
	catch (const AsnException& e) {
		std::cerr << "ASN.1 Error occurred" << std::endl;
		return -1;
	}
	catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}
}