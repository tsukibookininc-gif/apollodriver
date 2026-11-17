# Проверка кода первой версии

Проверка 16 ноября 2026, исходная версия ce350d2. Аппаратных тестов нет.
Исправления относятся к диагностике; аудиопотоки и команды записи не добавлены.
[Исследование аппаратуры](HARDWARE.md).

## Найденные проблемы

1. **Проверялись длины, но не согласованность USB-структуры.** Новые тесты на
   исходной версии воспроизвели принятие неверных bNumInterfaces/bNumEndpoints,
   дублей интерфейса/altset, дублей endpoints и orphan SuperSpeed companion.
   Теперь эти противоречия, отсутствие default altset 0 и вложенные configuration
   descriptors отвергаются. Ранее синтетическая фикстура тоже не содержала default
   altset; она исправлена, а не выдана за реальный дескриптор Apollo.
2. **USB-ошибка могла завершаться exit 0.** Ошибка была в JSON, но вызывающий
   скрипт мог принять запуск за успех. Теперь requested USB diagnostic failure
   даёт exit 2 и usb_diagnostics_ok=false. Exit 0 не доказывает обнаружение/звук.
   Ошибки Core Audio inventory учитываются отдельно от USB.
3. **COM user client создавался до отбора vendor class.** Теперь iterator сразу
   ограничен классом FF, затем проверяется interface 0. Так программа не создаёт
   ненужные user clients аудиоинтерфейсов. Это исправление пути выбора по review;
   реальный сбой на карте не воспроизводился.

## Дополнения и проверки

- Добавлен разбор UAC2 AS_GENERAL, Type I subslot/bit resolution и Clock Source.
  Неполные известные структуры отвергаются; UAC1/неизвестные vendor records
  остаются raw и не интерпретируются как UAC2.
- Общая последовательность status reads проверена fault injection. Timeout и
  короткий ответ останавливают её после первого запроса, неверный status magic —
  после status. Искусственные ответы не являются обменом с картой.
- macOS CI проверяет отказ от status при отсутствии Apollo и неверного location.
  При наличии карты автоматический тест активных запросов пропускается.
- Linux: 18 Python-тестов (1 пропуск macOS-only), 1409 C++ проверок и 10000
  malformed-input случаев под ASan/UBSan проходят. Результаты macOS на конкретном
  commit доступны во вкладке Actions.

## Оставшиеся ограничения

Ни открытие интерфейса Twin X, ни реальные control reads, ни отключение карты
во время запроса не проверены. IN-команды известны по upstream, не по нашему
аппаратному тесту. audio_streaming_implemented и hardware_audio_verified остаются false.

APLO2-анализатор не доказывает полноту исходного capture: формат не содержит
проверяемой модели/firmware и не подтверждает ответы DSP. Последующая проверка
наблюдаемой последовательности кадров описана ниже. Его отчёт не разрешает replay:
replay_safe всегда false. Реального capture Twin X у нас нет.

Не добавлены OUT-запросы, firmware upload, DSP init, ResetDevice, seize,
SetConfiguration, SetAlternateInterface или системные установки.

## Повторная полная проверка перед первым тестом

Повторно прочитаны оба C++ заголовка, все C++ исходники, Python-анализатор,
тесты, Makefile, workflow, README и ограничения в документации. Среди доступных
skills нет навыка для C++/IOKit driver review; UX/Figma skills к нему не применялись.
Вся работа выполнена в облаке. Подробности запуска — [FIRST_TEST.md](FIRST_TEST.md).

Дополнительные исправления:

- Cached descriptor с ошибкой семантики сохраняется вместе с raw_hex и parse_error.
  Ошибка остаётся ошибкой, не скрывается exit 0. Если одновременно запрошен status,
  он не выполняется после этой ошибки. Недоступный cache по-прежнему сообщает ошибку.
- Location разбирается по строгой грамматике: 1–8 hex digits с необязательным 0x.
  std::stoull раньше принимал, например, пробелы и « -0»; теперь это ошибка аргумента
  до обращения к платформенному backend.
- CF-объекты освобождаются scope-guard при исключениях конвертации строк.
- APLO2 wordcounts обрабатываются потоком без списка всех DSP-слов в памяти.
  Сравнивается наблюдаемый host bulk sequence modulo 256 между flags. При разрыве,
  повторе или reset выводятся метаданные, но opcodes/EEPROM-presence становятся null.
  Не делается ложный вывод, что неизвестных записей не было. Сброс sequence может
  быть допустимым новым сеансом; discrepancy сам по себе не доказывает потерю пакета.
- Архив .tar.gz сохраняет mode 0755 для исполняемого файла. Исправлен прежний
  manifest: строка build/apollo-inspect не соответствовала корню downloaded artifact.
  Теперь SHA256SUMS ссылается на файлы относительно распакованного каталога.
  Добавлены LICENSE, source commit metadata и короткая инструкция.
- CI отдельно проверяет static analysis, ARM64 архитектуру, распаковку,
  executable bit, checksums, подпись и --help из распакованного архива.
  Push запускает CI только для main, PR проверяется своим event; одинаковые
  ветки больше не занимают одновременно два набора runners.

Локально в Linux-облаке проходят 24 Python-теста (один macOS-only пропускается),
1425 C++ проверок и 10000 malformed-input случаев. GCC -fanalyzer компилирует
production code; macOS CI запускает Clang Static Analyzer и ASan/UBSan. Конкретные
результаты новой версии публикуются в Actions и не подменяются старым зелёным commit.

У предыдущего commit e1970ac красный aggregate result вызван отсутствием runner
для macOS 15: аннотация «The job was not acquired by Runner of type hosted even
after multiple attempts». Его Linux/macOS 26 jobs прошли. Это инфраструктурная
ошибка; проверка macOS 15 сохраняется, исключения для её результата не добавлены.

Облако не проверяет USBDeviceOpen/USBInterfaceOpen с Twin X, фактический control
transfer, hot-unplug устройства и звук. В первом пользовательском тесте используются
IORegistry и cached descriptors, без status requests, установки или изменения безопасности.

## Отдельный FX3 RAM boot эксперимент

17 ноября 2026 подготовлен отдельный `apollo-boot`, описанный в
[BOOT_EXPERIMENT.md](BOOT_EXPERIMENT.md). Предыдущие ограничения выше относятся
к проверявшемуся apollo-inspect; этот диагностический инструмент не получил OUT-запросов.

Новый код целиком проверяет CY/B0 image, word lengths, checksum, неперекрывающиеся
разрешённые RAM ranges и entrypoint. Выполнение допускается только при совпадении
всего файла с Twin X SHA256 pin, после явных CLI флагов и выбора текущего location.
Перед первой записью повторно проверяются VID/PID/location. После каждой записи
сверяются точная длина и полный readback. Ошибки не приводят к retry или jump.
Jump — отдельный zero-length запрос, его ACK не считается подтверждением runtime.

В Linux-облаке: 28 Python-тестов (1 macOS-only skip), 1425 protocol checks,
8448 boot checks и по 10000 malformed-input случаев для обоих parsers проходят.
GCC -fanalyzer и ASan/UBSan проходят. SHA проверен на опубликованных NIST-векторах;
результат на присланном образе отдельно совпал с Python hashlib и upstream pin.
Неверная модель отвергнута. Fuzz cases включают случайные байты и мутации валидной
синтетической структуры. Fault injection проверяет остановку на каждом transfer,
short replies и неверном readback, без USB и без firmware payloads в тестах.

macOS CI должен отдельно собрать новый IOUSBLib код и проверить CommonCrypto,
Clang Static Analyzer, sanitizers и архив. Ни эти checks, ни облачный разбор файла
не проверяют настоящие RAM writes/readbacks, USB hot-unplug или исполнение firmware.
Согласия на hardware-запуск пока нет; на карту ничего не отправлено.
