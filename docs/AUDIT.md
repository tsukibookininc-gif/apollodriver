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

APLO2-анализатор проверяет формат/wordcount, но не доказывает полноту исходного
capture: все правила sequence-number continuity не проверяются; формат не
содержит проверяемой модели/firmware и не подтверждает ответы DSP. Его отчёт не
разрешает replay: replay_safe всегда false. Реального capture Twin X у нас нет.

Не добавлены OUT-запросы, firmware upload, DSP init, ResetDevice, seize,
SetConfiguration, SetAlternateInterface или системные установки.
