# Масштабируемая система обработки видеопотока на базе ZeroMQ

Проект представляет собой распределенный масштабируемый программный комплекс для спектральной runtime-обработки изображений, поступающих с Web-камеры, на базе платформы ZeroMQ.

<br>

## Содержание
1. [Содержание веток репозитория](#1-содержание-веток-репозитория)
2. [Зависимости и библиотеки](#2-зависимости-и-библиотеки)
3. [Сборка проекта в Visual Studio](#3-сборка-проекта-в-visual-studio)
4. [Описание программных компонентов](#4-описание-программных-компонентов)
5. [Запуск системы](#5-запуск-системы)
6. [Результат работы системы](#6-результат-работы-системы)
7. [Иерархическая структура проекта](#7-иерархическая-структура-проекта)
8. [Источники](#8-источники)

<br>

## 1. Содержание веток репозитория

<ins>**0-Prototype**</ins> - ветка содержит приложения для проверки корректности подключения библиотек и проверки корректности сборки проекта.

> Ветка `0-Prototype` - для первоначальной проверки.

<ins>**1-All-ZMQ-Sockets-Examples**</ins> - ветка со всеми реализациями межкомпонентного взаимодействия. (см. таблицу 1)

> Ветка `1-All-ZMQ-Sockets-Examples` - для изучения и экспериментов.

<ins>**2-Best-(Router-Dealer)**</ins> -  ветка содержит реализацию **3-ROUTER-DEALER**, исключая другие решения.

> Ветка `2-Best-(Router-Dealer)` - готовое решение.

<br>

**Таблица 1: Реализации межкомпонентного взаимодействия ZeroMQ**
| ОК | Графики |
|:------------------------:|------------------------|
|:white_check_mark:|**1-PUSH-PULL (без ограничений)** — сокеты PUSH-PULL. Без ограничения очереди.|
|:x:|**2-PUSH-PULL (alpha)** — сокеты PUSH-PULL. Некорректно работает при масштабировании.|
|:white_check_mark:|**3-ROUTER-DEALER** — сокеты ROUTER-DEALER. Модель "запрос-ответ".|
|:white_check_mark:|**4-REQ-REP** — сокеты REQ-REP. Модель "запрос-ответ".|
|:white_check_mark::construction:|**5-PUSH-PULL** — сокеты PUSH-PULL. Пропускает кадры наборами.|
|:construction:|**6-PUSH-PULL (с АСК)** — сокеты PUSH-PULL. С подтверждением получения данных.|
|:white_check_mark::construction:|**7-PUSH-PULL (2 сокета)** — сокеты PUSH-PULL. С запросами по второму сокету.|

<br>

## 2. Зависимости и библиотеки
Для работы проекта требуются следующие библиотеки:
- **ZeroMQ (libzmq)** - высокопроизводительная асинхронная библиотека обмена сообщениями
  - Версия: 4.3.4 (v141 / x64)
  - Ссылка: https://github.com/zeromq/libzmq
- **OpenCV** - библиотека компьютерного зрения
  - Версия: 4.12.0
  - Ссылка: https://opencv.org/
- **Protocol Buffers** - система сериализации структурированных данных
  - Версия: 3.7.1
  - Ссылка: https://github.com/protocolbuffers/protobuf
  - NuGet:  https://www.nuget.org/packages/protobuf-v141/3.7.1

<br>

## 3. Сборка проекта в Visual Studio
### <ins>**Предварительные требования**</ins>
Установите Visual Studio 2022 с компонентами:
  - "Разработка классических приложений на C++"
  - "Пакет SDK для Windows 10/11"

### <ins>**3.1. Клонирование репозитория**</ins>
```
git clone https://github.com/Volshebnik-Wisard/6nd-UNN__ZeroMQCameraSystem.git
cd 6nd-UNN__ZeroMQCameraSystem
```

### <ins>**3.2. Установка зависимостей**</ins>
**ZeroMQ:**
  - распакуйте в директорию`ZeroMQCameraSystem/libzmq-v141-x64-4_3_4/`
  - поместите в директорию `x64/Release/` dll: `libzmq-v141-mt-4_3_4.dll`; `libsodium.dll`
    
**OpenCV:**
  - установите в директорию`ZeroMQCameraSystem/opencv-4.12.0/`
  - поместите в директорию `x64/Release/` dll: `opencv_world4120.dll`, `opencv_world4120d.dll`
    
**Protobuf:**
  - установите с помощью NuGet: ```Средства -> Диспетчер пакетов Nuget -> Управление пакетами Nuget для решения... -> Обзор -> В поиске: protobuf-v141 -> Установить```

### <ins>**3.3. Открытие решения и добавление зависимостей**</ins>
Откройте файл ZeroMQCameraSystem.sln в Visual Studio и для каждого проекта (Capturer, Worker, Composer):

В свойствах проекта → C/C++ → Общие → Дополнительные каталоги включаемых файлов:
```
.\libzmq-v141-x64-4_3_4\include
.\opencv-4.12.0\build\include
..\packages\protobuf-v141.3.7.1\build\native\include
..\packages\protobuf-v141.3.7.1\build\native\include\google\protobuf
```
В свойствах проекта → Компоновщик → Общие → Дополнительные каталоги библиотек:
```
.\libzmq-v141-x64-4_3_4\lib
.\opencv-4.12.0\build\x64\vc16\lib
..\packages\protobuf-v141.3.7.1\build\native\lib\x64\v141\Release\static
..\packages\protobuf-v141.3.7.1\build\native\lib\x64\v141\Debug\static
```
В свойствах проекта → Компоновщик → Ввод → Дополнительные зависимости:
```
libzmq-v141-mt-4_3_4.lib
libzmq-v141-mt-s-4_3_4.lib
opencv_world4120.lib
opencv_world4120d.lib
libprotobuf.lib
libprotobufd.lib
```

### <ins>3.4. **Сборка проекта**</ins>

Выберите конфигурацию `Release` и платформу `x64`.

```Вид -> Обозреватель решений (Ctrl+Alt+L) -> ЛКМ на решение -> Ctrl+B```

> или

```Вид -> Обозреватель решений (Ctrl+Alt+L) -> ПКМ на решение -> Собрать```

Программы соберутся в директории `x64/Release/`:
```
1_Capturer.exe
2_Worker.exe
3_Composer.exe
```

<br>

## 4. Описание программных компонентов

Система состоит из трех независимых программ, взаимодействующих через ZeroMQ. Каждый компонент выполняет строго определенную роль в конвейере обработки видео.

**Общая архитектура для `3-ROUTER-DEALER`**

```
                                    ┌─────────────────┐
               (ROUTER-DEALER)      │                 │      (PUSH-PULL)
         ┌------------------------> │     Worker 1    │-------------------------┐
         │                          │                 │                         │
         │                          └─────────────────┘                         ↓
┌────────────────┐                  ┌─────────────────┐                ┌────────────────┐
│                │ (ROUTER-DEALER)  │                 │  (PUSH-PULL)   │                │
│    Capturer    │----------------> │     Worker 2    │--------------> │    Composer    │
│                │                  │                 │                │                │
└────────────────┘                  └─────────────────┘                └────────────────┘
         │                               ..........                             ↑
         │                          ┌─────────────────┐                         │
         │     (ROUTER-DEALER)      │                 │      (PUSH-PULL)        │
         └------------------------> │     Worker N    │-------------------------┘
                                    │                 │
                                    └─────────────────┘
Worker ∈ (1; N)
```

**Принцип работы:**
 - **Capturer** захватывает кадры с камеры и помещает их в очередь
 - **Worker'ы** запрашивают кадры из очереди Capturer'а когда готовы к обработке
 - **Composer** собирает обработанные кадры от всех Worker'ов и сохраняет в видеофайлы


### <ins>**4.1. Capturer (`1_Capturer.exe`)**</ins>
**Захватчик видео** - отвечает за получение видеопотока с камеры и распределение кадров между Worker'ами.

**Алгоритм работы:**
```
1. **Инициализация**

2. **Основной цикл (до остановки):**

   2.1. **Захват кадра:** Чтение кадра с камеры через opencv
   
   2.2. **Обработка запросов от worker'ов:** Неблокирующая проверка ROUTER сокета
   
   2.3. **Распределение кадров:**
     - Извлечение первого доступного worker'а из очереди
     - Извлечение первого кадра из очереди кадров
     - Сериализация кадра в protobuf строку
     
   2.4. **Управление очередью:** Если размер очереди больше max_queue_size, то происходит удаление самого старого кадра из очереди
   
   2.5. **Вывод статистики (каждые 30 кадров):**

3. **Завершение**
```

**Взаимодействие с другими компонентами:**
```
Worker → Capturer:             "GET"                    (запрос кадра)
Capturer → Worker: VideoFrame: ImageData single_image (отправка кадра)
```

### <ins>**4.2. Worker (`2_Worker.exe`)**</ins>
**Обработчик кадров** - применяет визуальные эффекты к полученным кадрам. Может запускаться в нескольких экземплярах для параллельной обработки.

**Алгоритм работы:**
```
1. **Инициализация**
2. **Начальный запрос:** Отправка первого запроса "GET" Capturer'у
3. **Основной цикл (до остановки):**
   
   3.1. **Проверка входящих сообщений:** Неблокирующая проверка DEALER сокета
   
   3.2. **Обработка кадра от Capturer'а:** Десериализация protobuf сообщения в VideoFrame
   
   3.3. **Извлечение изображения**
   
   3.4. **Применение эффекта "Scanner Darkly"**
     - Квантование цвета
     - Детекция границ
     - Выделение контуров

   3.5. **Подготовка сообщения для Composer'а:**
     - Создание нового VideoFrame сообщения
     - Копирование метаданных из входного кадра
     - Создание пары изображений (ImagePair)
     - Сериализация сообщения в protobuf строку

   3.6. **Отправка результата в Composer:** Неблокирующая отправка через PUSH сокет
   
   3.7. **Вывод статистики (каждые 50 обработанных кадров)**
   
4. **Завершение**
```

**Взаимодействие с другими компонентами:**

```
Worker → Capturer:             "GET"                     (запрос кадра)
Capturer → Worker: VideoFrame: ImageData single_image  (отправка кадра)
Worker → Composer: VideoFrame: ImagePair image_pair (отправка 2 кадров)
```

### <ins>**4.3. Composer (`3_Composer.exe`)**</ins>

**Сборщик видео** - получает обработанные кадры от всех Worker'ов, упорядочивает их и сохраняет в видеофайлы.

**Алгоритм работы:**
```
1. **Инициализация**
2. **Основной цикл (до 10 секунд без кадров):**
   2.1. **Ожидание сообщений:** Опрос PULL сокета

   2.2. **Обработка полученного кадра:**
     - Десериализация protobuf сообщения в VideoFrame
     - Обновление времени последнего полученного кадра
     - Декодирование оригинального изображения из JPEG

   2.3. **Запись доступных кадров:** в output_original.avi и в output_processed.avi
   
   2.4. **Проверка условий остановки:**       
   
   2.5. **Вывод статистики (каждые 50 полученных кадров):**
   
   2.6. **Финальная обработка:** Для каждого пропущенного кадра производится запись черного кадра в оба видеофайла

3. **Завершение**
   3.1. **Вывод итоговой статистики**
```
**Взаимодействие с другими компонентами:**
```
Worker → Composer: VideoFrame: ImagePair image_pair (отправка 2 кадров)
```

<br>

## 5. Запуск системы

### <ins>**5.1. Конфигурация**</ins>
Настройки системы находятся в файле `.\x64\Release\config.txt`:
- Сетевые порты и адреса
- Параметры захвата видео
- Настройки эффекта обработки
- Размеры буферов и очередей

### <ins>**5.2. Последовательность запуска**</ins>

Для запуска программ с записью в .txt можно использовать Powershell скрипты `.ps1`, которые должны лежать в папке с соответствующими `*.exe` файлами: ```ПКМ -> Выполнить с помощью Powershell```

**5.2.1. Запустите Composer**

```
.\x64\Release\3_Composer.exe
```

 > Или используйте скрипт с записью в .txt
 
```
.\x64\Release\3_Composer.ps1
```
---
**5.2.2. Запустите один или несколько Worker'ов**

> Для лучшего масштабирования нагрузки запустите несколько экземпляров Worker'ов. (в отдельном окне или на другом компьютере)

```
.\x64\Release\2_Worker.exe
```
> Или используйте скрипт с записью в .txt
```
.\x64\Release\2_Worker.ps1
```
---
**5.2.3. Запустите Capturer**

```
.\x64\Release\1_Capturer.exe
```

 > Или используйте скрипт с записью в .txt
 
```
.\x64\Release\1_Capturer.ps1
```
---
### <ins>**5.3. Порядок остановки**</ins>
 - Нажмите Ctrl+C в окне Capturer для остановки захвата
 - Worker'ы автоматически завершатся при отсутствии кадров
 - Composer завершит запись видео и сохранит файлы
    - `output_original.avi` - исходное видео
    - `output_processed.avi` - видео с примененным эффектом

<br>

## 6. Результат работы системы

|![Test 3 - output_original](https://github.com/user-attachments/assets/80d96b24-7684-49e4-9e60-1c4c13944f99)|![Test 3 - output_processed](https://github.com/user-attachments/assets/96c25ac0-0ebf-470f-af8d-2f22b6b613f2)|
|:---:|:---:|
| Исходное изображение - output_original | Обработанное изображение - output_processed |

<ins>**Спецификация ПК (одинакова для всех тестов и компонентов):**</ins>
 - Процессор: Intel Core i7-12700; 12/20; 2.1/4.9 ГГц; 65 Вт
 - Оперативная память: 16Gb
 - Видеокарта: GTX 1650

<br>

**Таблица 2: Localhost тесты (компоненты на одном ПК)**
| Тесты | 1 Worker | 2 Workers | 3 Workers | 4 Workers | 5 Workers |
| :--- | ---: | ---: | ---: | ---: | ---: |
| **3-ROUTER-DEALER** | 58.9% (1062) | 25.3% (1048) | 2.9% (1079) | 1.3% (1089) | 0.0% (1059) |
| **4-REQ-REP** | 62.8% (1061) | 32.9% (1063) | 11.7% (1064) | 3.8% (1058) | 0.0% (1125) |
| **5-PUSH-PULL** | 29.7% (1050) | 0.6% (1086) | - | - | 0.0% (5278) |
| **6-PUSH-PULL (с АСК)** | - | - | - | - | - |
| **7-PUSH-PULL (2 сокета)** | 43.8% (1050) | 0.0% (1687) | - | - | - |

> Каждая ячейка таблицы содержит: `Процент пропущенных кадров (Всего кадров)`

<br>

**Таблица 3: IP тесты (компоненты на разных ПК)**
| Тесты | 1 Worker | 2 Workers | 3 Workers |
| :--- | ---: | ---: | ---: |
| **3-ROUTER-DEALER** | 54.2% (1057) | 9.1% (1051) | 0.0% (3022) |
| **4-REQ-REP** | 56.9% (1048) | 15.1% (1051) | 0.0% (3042) |
| **5-PUSH-PULL** | 33.7% (1108) | 0.0% (5009) | - |
| **6-PUSH-PULL (с АСК)** | - | - | - |
| **7-PUSH-PULL (2 сокета)** | 42.5% (1026) | 0.0% (1305) | - |

> Каждая ячейка таблицы содержит: `Процент пропущенных кадров (Всего кадров)`

| Тесты | 1 Worker | 2 Workers | 3 Workers |
| :--- | :---: | :---: | :---: |
| **3-ROUTER-DEALER** |![Test 1 - output_processed](https://github.com/user-attachments/assets/e8c4cf00-0381-4c7b-b747-32b823b15d1f)|![Test 2 - output_processed](https://github.com/user-attachments/assets/e817d512-5c93-44bc-a722-80d4349ab417)|![Test 3 - output_processed](https://github.com/user-attachments/assets/4ccbdd77-e474-48fa-bc13-94bb9f82d310)|
| **4-REQ-REP** |![Test 1 - output_processed](https://github.com/user-attachments/assets/7be28322-8146-4c49-b20c-d813e75407d8)|![Test 2 - output_processed](https://github.com/user-attachments/assets/342b6d13-8801-4b3c-895c-21cfddb89b16)|![Test 3 - output_processed](https://github.com/user-attachments/assets/c93e7fa6-e43b-4029-8f01-e75d603ee626)|
| **5-PUSH-PULL** |![Test 1 - output_processed](https://github.com/user-attachments/assets/c5a07002-6ef3-4c81-85d9-71fe7230c0ef)|![Test 2 - output_processed](https://github.com/user-attachments/assets/b28ce514-0404-47e3-82fc-b3d9afd9d33a)| - |
| **7-PUSH-PULL (2 сокета)** |![Test 1 - output_processed](https://github.com/user-attachments/assets/f880dffb-5756-44f6-8801-80e669f1d36d)|![Test 2 - output_processed](https://github.com/user-attachments/assets/67a4aa1f-0439-456b-adc0-f3972e019fd4)| - |

<br>

## 7. Иерархическая структура проекта

> Ветка: `2-Best-(Router-Dealer)`. В других ветках иначе.

```
ZeroMQCameraSystem/
│
├── ZeroMQCameraSystem.sln
│
├── Tests/
│   ├── 528 - Test.xlsx
│   ├── Alpha - 516 - Test.xlsx (недействительные)
│   ├── 3-ROUTER-DEALER/
│   ├── 4-REQ-REP/
│   ├── 5-PUSH-PULL/
│   └── 7-PUSH-PULL (2 сокета)/
│       ├── 0-Localhost тесты/ 
│       └── 1-Ip тесты/
│
├── Documentation/
│   ├── 0-(Router) Масштабируемая система для обработки видео на базе ZeroMQ.docx (.pdf)
│   ├── .png, .xml, json        (изображения схем)
│   ├── video_processing.proto  (с некоторыми копиями, которые отличаются комментариями)
│   ├── Инструменты.txt         (для редактирования схем)
│   └── Установка-настройка .proto, include, .lib.txt
│
├── ZeroMQCameraSystem/
│   ├── 1_Capturer.vcxproj      (.vcxproj.filters; .vcxproj.user)
│   ├── 2_Worker.vcxproj        (.vcxproj.filters; .vcxproj.user)
│   ├── 3_Composer.vcxproj      (.vcxproj.filters; .vcxproj.user)
│   ├── Capturer.cpp
│   ├── Composer.cpp
│   ├── config.txt
│   ├── config_loader.h
│   ├── packages.config         (после установки protobuf из NuGet)
│   ├── scanner_darkly_effect.hpp
│   ├── video_addresses.h
│   ├── video_processing.pb.cc
│   ├── video_processing.pb.h
│   ├── video_processing.proto
│   ├── Worker.cpp
│   │
│   ├── libzmq-v141-x64-4_3_4/
│   │   ├── include/
│   │   │   ├── zmq.h
│   │   │   ├── zmq.hpp
│   │   │   └── zmq_addon.hpp
│   │   │
│   │   └── lib/
│   │       ├── libzmq-v141-mt-4_3_4.lib
│   │       └── libzmq-v141-mt-s-4_3_4.lib
│   │
│   └── opencv-4.12.0/
│       └── build/
│            ├── include/
│            │
│            └── x64/vc16/lib/
│                 ├── opencv_world4120.lib
│                 └── opencv_world4120d.lib
├── x64/
│   └── Release/
│       ├── 1_Capturer.ps1
│       ├── 2_Worker.ps1
│       ├── 3_Composer.ps1
│       ├── config.txt
│       ├── libsodium.dll
│       ├── libzmq-v141-mt-4_3_4.dll
│       ├── opencv_world4120.dll
│       └── opencv_world4120d.dll
│
└── packages/
    └── protobuf-v141.3.7.1/
        └── build/native/
             ├── include/google/protobuf/
             │
             └── lib/x64/v141/
                  ├── Debug/static
                  │   └── libprotobufd.lib
                  │
                  └── Release/static
                      └── libprotobuf.lib
```

<br>

## 8. Источники
- ZeroMQ (libzmq) - https://github.com/zeromq/libzmq
- OpenCV - https://opencv.org/
- Protocol Buffers - https://www.nuget.org/packages/protobuf-v141/3.7.1
