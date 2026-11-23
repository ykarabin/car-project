# Документація розробника

Внутрішні нотатки для продовження роботи: чому прийняті рішення, як
перевіряти зміни на стенді та що ще не підтверджене. Огляд системи —
[README](../../README.md), архітектура — [architecture.md](../architecture.md),
підключення — [hardware.md](../hardware.md), збірка й калібрування —
[usage.md](../usage.md).
Плани й прогрес — [ROADMAP.md](../../ROADMAP.md).

| Документ | Зміст |
| --- | --- |
| [drive-control.md](drive-control.md) | Задній привід: режими, PI + feed-forward, рампи, реверс, допуски збоїв, історія налаштування |
| [steering.md](steering.md) | Кермо: MT6701, калібрування кнопкою, Flash, P-регулятор, повернення до центру |
| [encoders.md](encoders.md) | Задні MT6826S: активний RIGHT, вибір 1024 PPR, стан LEFT |
| [safety.md](safety.md) | FLT, IWDG, debug halt, аварійний шлях, апаратні pull-down |
| [rtos-and-logging.md](rtos-and-logging.md) | Задачі, володіння даними, UART-транспорт, формат RTT |
| [radio-setup.md](radio-setup.md) | Gateway: вибір джерела, ES900 / RadioMaster TX15, PWM-приймач |
| [bench.md](bench.md) | Стендовий порядок дій, інструменти, результати вимірювань |
| [misra.md](misra.md) | Статичний аналіз: cppcheck + MISRA, відхилення, хибні спрацювання, що лишилось |

## Вимоги до коду

Зафіксовані власником проєкту 18.11.2026.

1. **Простота.** Мінімальна достатня кількість модулів і станів, без
   універсальних фреймворків і функцій наперед.
2. **Embedded best practices.** Чітке володіння периферією, обмежений час
   виконання, фіксовані буфери, явні одиниці й діапазони, перевірка
   результатів HAL, коректна взаємодія ISR/RTOS, неблокуюча діагностика.
   У логіці керування — без динамічної пам'яті.
3. **Професійна якість.** Зрозумілі інтерфейси, відтворювана збірка,
   актуальна документація. Підтверджена поведінка відокремлена від припущень.
4. **Наближення до MISRA C.** Фіксовані цілочисельні типи, явні
   перетворення з відомим діапазоном, без невизначеної поведінки й рекурсії,
   мінімальна область видимості, `const` для незмінних даних.

Вимоги стосуються власного коду. CubeMX, HAL, FreeRTOS і SEGGER RTT не
переписуються заради стилю. Сертифікована відповідність MISRA не заявляється.

Локальна політика:

- `if` / `else` / `for` / `while` завжди з фігурними дужками.
- Дані й лічильники — фіксовані типи; розміри буферів та індекси — `size_t`.
- `int` лишається для результату `snprintf` і `remote_command_format`:
  спершу перевірка від'ємного значення, потім межі буфера.
- Ранні `return` дозволені для перевірок входу, аварійних виходів і таблиць
  назв станів — свідоме відхилення заради видимого порядку перевірок.
- Модуль числа — лише `int32_magnitude` (`int32_math.h`): приймає `int32_t`,
  повертає `uint32_t`, коректний для `INT32_MIN`.
- CubeMX зберігає призначення вже запаяних виводів: LEFT TIM2/A/B/Z,
  ADC1/CS/DMA та обидва Z залишаються в `.ioc` для майбутнього повернення.

## Збірка й тести

Збірка прошивок — [usage.md](../usage.md). Хостові тести (каталоги
`tests/` і `firmware/remote-gateway/tests/`) запускаються так:

```sh
cmake -S tests -B build/host-tests -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

Збірка `tests/` містить і тести Gateway: 39 тестів (34 ECU, 5 Gateway).
`firmware/remote-gateway/tests` можна зібрати й окремо. Тести моделюють
HAL, таймери та FreeRTOS; вони не замінюють перевірку на стенді.

## Форматування й статичний аналіз

Стиль задають `.clang-format` (C) і `.editorconfig` (решта файлів).
Перевіряється лише власний код — список у `tools/lint/sources.py`.

```sh
brew install cppcheck clang-format      # CI: cppcheck 2.22, clang-format 23.1
tools/lint/check.py format --fix        # відформатувати
tools/lint/check.py format              # перевірити, як у CI
tools/lint/check.py cppcheck            # cppcheck + MISRA, порівняння з базою
```

Для Gateway cppcheck бере `firmware/remote-gateway/build/compile_commands.json`
(після `idf.py build`); без нього Gateway локально пропускається. Правила,
відхилення й база MISRA — [misra.md](misra.md).

## CI

`.github/workflows/ci.yml` запускається на push у `main` / `develop`, на PR
і вручну:

- хостові тести ECU й Gateway (clang);
- ECU Debug і Release з Arm GNU Toolchain 14.3.rel1 та перевірка образу
  `tools/ci/check_ecu_image.py`: нічого не записується в резерв калібрування,
  образ уміщується в 508 КБ;
- Gateway в образі `espressif/idf:v6.0.1` і звіт `idf.py size`;
- дисплейний блок у тому ж образі;
- Lint: `tools/lint/check.py format` і `cppcheck` (cppcheck 2.22 збирається
  з джерел і кешується, clang-format 23.1.5 — з PyPI).

Зібрані ELF / BIN доступні як artifacts запуску. Локально ECU збирається
ST-версією компілятора (GNU Tools for STM32 14.3.1+st.2), у CI — офіційною
Arm 14.3.rel1; розміри образів можуть трохи відрізнятися.

## Що ще не виміряно

- Час циклу `defaultTask`, джитер і завантаження CPU.
- Фізичне інжектування FLT (тривалий LOW і серія імпульсів) — лише хостові тести.
- Рівень PWM на debug halt і контрольоване зависання задачі з IWDG-reset.
- Робота приводу й керма під навантаженням, на покритті.
- Електричне джерело NE-помилок UART і коротких імпульсів FLT.
