# Более безопасный этап: USB-таблицы из файла прошивки

17 ноября 2026. После успешного `--plan` на Mac владелец попросил более безопасный
путь. Hardware-загрузка отложена: согласия на RAM writes/jump нет. Следующий
анализ выполнен целиком в облаке, без USB, запуска ARM-кода, новых программ на Mac
и новых изменений его настроек безопасности.

## Источник и способ проверки

Собственный ApolloTwinX.bin владельца: 147416 байт, SHA256
`bdd9db3f2d139f615c4f1e4e0261d24d90a6f95f8c6e1afe34e0c1a8fbd35fcc`.
Образ CY/B0 уже проверен по layout/checksum; [подробности](BOOT_EXPERIMENT.md).

В payload выполнен поиск заголовков USB device/configuration descriptors.
Configuration candidates ограничивались по длине/числу интерфейсов, затем
проверялись имеющимся strict offline decoder `apollo-inspect --decode-config`.
Две таблицы прошли проверку целостности descriptor chain, interface/altset/endpoint
counts. Обе находятся внутри загружаемой секции, не пересекают заголовки image.
Firmware bytes и полные таблицы в GitHub не копируются; ниже только метаданные.

Это статически найденные **кандидаты таблиц**, а не live USB response. Не доказано,
какой callback выбирает их, модифицируются ли они при запуске и как они выглядят
после DSP initialization. Даже строгий разбор не доказывает возможность audio I/O.

## Найденные конфигурации

| Свойство | Таблица A | Таблица B |
| --- | --- | --- |
| File offset | 39388 | 40156 |
| Длина | 440 | 287 |
| Интерфейсов | 4 | 4 |
| Interface 0 | Vendor FF; bulk 01 OUT / 81 IN, interrupt 86 IN | Vendor FF; bulk 01 OUT / 81 IN, interrupt 86 IN |
| Bulk max packet | 1024 | 512 |
| SuperSpeed companions | Присутствуют | Отсутствуют |
| Interface 1 | AudioControl, protocol 20h, clock source ID 80h | AudioControl, protocol 20h, clock source ID 80h |
| Interfaces 2/3 | AudioStreaming class 01, subclass 02, protocol 20h | Vendor class FF, subclass 01, protocol 00 |

Таблица A соответствует форме SuperSpeed-конфигурации; B — форме High-Speed
конфигурации. Это вывод по companion records/packet sizes, не измерение скорости
Apollo владельца. Поле iokit_speed_code=21 из loader-отчёта не используется для
подтверждения этой гипотезы.

В A для AudioStreaming описаны:

| Интерфейс / направление | Alternate setting | Advertised channels | Формат | Endpoint |
| --- | --- | --- | --- | --- |
| 2 / host → Apollo | 1 | 10 | PCM, subslot 4 bytes, significant bits 24 | 03 OUT, isochronous |
| 2 / host → Apollo | 2 | 6 | То же | 03 OUT, isochronous |
| 3 / Apollo → host | 1 | 24 | То же | 83 IN, isochronous |
| 3 / Apollo → host | 2 | 18 | То же | 83 IN, isochronous |
| 3 / Apollo → host | 3 | 10 | То же | 83 IN, isochronous |

У обоих streaming interfaces есть alt 0 без endpoints. Изохронные endpoints
помечены asynchronous; 83 IN имеет usage type 2 (implicit feedback data).
Эти channel counts не означают такое количество физических аналоговых разъёмов,
успешный routing или пригодность конкретного altset при выбранной частоте.
Частоты, clock requests и реальная упаковка потока этим поиском не установлены.

В B потоковые интерфейсы имеют vendor class. Их class-specific bytes не выдаются
декодером за стандартный UAC2 format. Одна прошивка с такими разными таблицами
не даёт оснований обещать одинаковое поведение Apple USB Audio на разных линках.

Также найдены два согласованных device-descriptor candidates с VID/PID
2b5a:000f: file offsets 40540 и 40572, bcdUSB 0320h и 0210h соответственно,
class EFh и по одной configuration. bcdUSB — поле версии спецификации, а не
доказательство физической скорости/поколения контроллера или текущего runtime PID.

## Что это меняет

Для дальнейшей архитектуры теперь есть модельные endpoint/format сведения из
конкретного известного Twin X image, а не подстановка Solo. Vendor streaming
interfaces в B и implicit feedback в A требуют отдельного внимания при проверке
совместимости со штатным Apple USB Audio. Простое появление PID 000F и попытка
автоматического matching не гарантируют рабочий звук.

Более безопасная последовательность — продолжать offline-анализ descriptor
selection, clock handling и адресов инициализации; сохранять отсутствие device
writes до нового согласования. Возможный будущий RAM-тест можно разделить на
запись/readback без jump и отдельное исполнение. Для этого позднее подготовлен
режим `--write-verify-only`, см. [операцию и ограничения](BOOT_EXPERIMENT.md).
Он всё равно пишет RAM и пока не запускался на карте. Разрешение на него
в этом этапе не подразумевается; ранее скачанный бинарник этот режим не содержит.

Уже подтверждённое на Mac: discovery/cached descriptors loader и успешный offline
plan с checksum/pin match, device_opened=false и device_writes_performed=false.
Метод, которым владелец разрешил запуск после Gatekeeper refusal, из нового лога
не установлен. Его успешный `--plan` не является согласием на firmware execution.

Полностью исключить риск, сохранив экспериментальную инициализацию и звук, нельзя.
Этот этап уменьшает неизвестность без изменения карты; он не заменяет будущий
hardware-тест и не объявляет драйвер готовым.
