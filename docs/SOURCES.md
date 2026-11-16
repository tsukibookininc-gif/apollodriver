# Источники и границы доказательств

Проверено 16 ноября 2026. База протокола зафиксирована на open-apollo
[`6e0c5953c0f334dec17bcf0b34261263cad8acfd`](https://github.com/rolotrealanis98/open-apollo/tree/6e0c5953c0f334dec17bcf0b34261263cad8acfd).
Это источники реализации и выводов, а не заявление о совместимости Twin X с macOS.
Дополнительные источники аппаратуры и анализа Windows/macOS перечислены в
[HARDWARE.md](HARDWARE.md); проверка нашего кода — в [AUDIT.md](AUDIT.md).

## Исходники open-apollo — GPL-2.0

| Источник | Что использовано | Ограничение |
| --- | --- | --- |
| [README](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/README.md) | Заявленный статус моделей и USB-стека | Отчёты авторов, не наши измерения |
| [uausb/device.py](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/tools/uausb/device.py) | Vendor IN 00/04/06/0E/0F, 36-байтный статус, boot word 1069 | Сам upstream помечает этот слой как не проверенный на устройстве |
| [fx3-load.py](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/tools/fx3-load.py) | VID 2B5A, Twin X loader PID 000E, схема загрузки FX3 | Здесь загрузчик не реализован и не запущен |
| [usb-full-init.py](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/tools/usb-full-init.py) | Runtime PID 000F, проблемы инициализации Solo/Twin | Последовательность Solo не считается пригодной для Twin X |
| [uausb/dspcmd.py](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/tools/uausb/dspcmd.py) | Bulk header, wordcount и opcode DSP-команд | Используется только для офлайн-анализа, без отправки |
| [pcap-init-extract.py](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/tools/usb-re/pcap-init-extract.py) | Формат APLO2: magic + count, 16-байтные заголовки событий | APLO2 не содержит проверяемой идентичности модели/прошивки |
| [patch-snd-usb.py](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/tools/patch-snd-usb.py) | Linux-обходы для частот/feedback/endpoint/interface close | Это не исправления для AppleUSB Audio |
| [hardware_usb.py](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/mixer-engine/hardware_usb.py) | Разделение USB-управления и микшера | Не Core Audio backend |
| [apollo-twin-x-usb.json](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/devices/apollo-twin-x-usb.json) | Модельные данные, статус unverified | Числа каналов не подставляются вместо живых дескрипторов |
| [LICENSE](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/LICENSE), [NOTICE](https://github.com/rolotrealanis98/open-apollo/blob/6e0c5953c0f334dec17bcf0b34261263cad8acfd/NOTICE.md) | GPL v2 и разделение кода/ресурсов UA | UA-ресурсы не входят в лицензию проекта |

## Отчёты владельцев — свидетельства, не воспроизведённые нами

- [Twin USB DUO: issue 69](https://github.com/rolotrealanis98/open-apollo/issues/69):
  владелец сообщает о playback/capture со **своей Twin-specific инициализацией**.
  Это не Twin X и не стандартный install-usb.sh.
- [Twin X USB DUO: комментарий 7 ноября](https://github.com/rolotrealanis98/open-apollo/issues/69#issuecomment-6712605203):
  runtime PID 000F и ALSA-карта определяются, затем DSP init зависает;
  требуется выключить и включить устройство. Положительного аудиотеста нет.
- [Другой Twin USB: issue 128](https://github.com/rolotrealanis98/open-apollo/issues/128):
  загрузка firmware/сборка модуля проходят, но это не доказывает звук.
- [Asahi Linux: PR 93](https://github.com/rolotrealanis98/open-apollo/pull/93):
  сообщается о Thunderbolt x4 на M2 Ultra под Linux; это другой транспорт, чип и ОС.

## Первичные документы Apple, Universal Audio и GitHub

- [Apple: Working With USB Device Interfaces](https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/USBBook/USBDeviceInterfaces/USBDevInterfaces.html):
  IOUSBLib discovery, COM interfaces, opening interfaces. В этой реализации
  намеренно отсутствуют SetConfiguration, SetAlternateInterface и seize.
- [Apple: исходный заголовок IOUSBLib](https://github.com/apple-oss-distributions/IOUSBFamily/blob/main/IOUSBFamily/Headers/IOUSBLib.h):
  GetConfigurationDescriptorPtr возвращает cached descriptor без USBDeviceOpen;
  ControlRequestTO работает через открытую USB interface.
- [Apple: Creating an audio device driver](https://developer.apple.com/documentation/audiodriverkit/creating-an-audio-device-driver):
  AudioDriverKit, IOUserAudioDriver/Device, provisioning и entitlements.
- [Apple: Create audio drivers with DriverKit, WWDC21](https://developer.apple.com/videos/play/wwdc2021/10190/):
  AudioDriverKit вместе с USBDriverKit, связь с Core Audio.
- [Apple: TN3190 USB audio device design considerations](https://developer.apple.com/documentation/technotes/tn3190-usb-audio-device-design-considerations):
  взаимодействие своего драйвера с Apple USB Audio, свойства/codeless dext.
- [Apple: Requesting Entitlements for DriverKit Development](https://developer.apple.com/documentation/driverkit/requesting-entitlements-for-driverkit-development):
  entitlement нельзя получить из обычной локальной сборки или придумать.
- [UA: Apollo Desktop USB System Requirements](https://help.uaudio.com/hc/en-us/articles/207719966-Apollo-Desktop-USB-System-Requirements):
  официальная несовместимость Apollo USB с macOS.
- [GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners):
  macos-15 / macos-26 arm64 для облачной сборки. Это не M5 с Apollo.
