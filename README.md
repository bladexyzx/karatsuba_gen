# karatsuba_gen — генератор конвейерного умножителя Карацубы на Verilog

## Назначение

`karatsuba_gen` — консольная программа на C++17 для Linux. Она принимает
натуральное число `N` (разрядность) и записывает Verilog-описание (`*.v`)
схемы, которая умножает два неотрицательных целых `N`-битных числа по
**алгоритму Карацубы**:

* результат (`2N` бит) выдаётся на **3-й такт** после подачи операндов;
* новые операнды можно подавать на каждом такте, результаты тоже выходят на
  каждом такте (**конвейер**);
* код — синтезируемый Verilog-2001, проверен в Icarus Verilog и Verilator.

Документация к коду (Doxygen): [docs/html/index.html](docs/html/index.html).

## Возможности

| Функция | Как включить |
|---|---|
| Умножитель `N × N → 2N` для любого `N` от 1 до 4096 | `karatsuba_gen N` |
| Выбор файла результата или вывод в stdout | `-o FILE`, `-o -` |
| Своё имя модуля верхнего уровня (можно подключить несколько умножителей в один проект) | `-m NAME` |
| Разрядность, на которой рекурсия Карацубы останавливается и ставится обычный умножитель | `-b W` (3…32) |
| Листовые умножители через `*`, чтобы синтезатор ПЛИС отдал их DSP-блокам | `--leaf-star` |

Интерфейс сгенерированного модуля:

| Порт | Ширина | Назначение |
|---|---|---|
| `clk` | 1 | тактовый сигнал, всё работает по переднему фронту |
| `rst_n` | 1 | синхронный сброс, активный 0; сбрасывает только `out_valid` |
| `in_valid` | 1 | на входах `a`, `b` новые данные |
| `a`, `b` | `N` | множители |
| `out_valid` | 1 | на выходе `p` готовый результат (через 3 такта после `in_valid`) |
| `p` | `2N` | произведение `a · b` |

Если флаги не нужны, достаточно подать `in_valid = 1` и не смотреть на
`out_valid`: произведение всё равно появится на `p` через 3 такта.

## Установка

### Требования

| Компонент | Версия | Для чего |
|---|---|---|
| Linux | любой современный дистрибутив | сборка и запуск |
| `g++` или `clang++` | с поддержкой C++17 (g++ 7+, clang 5+) | сборка |
| CMake | 3.20+ (для `ctest --test-dir`; на 3.16…3.19 — `cd build && ctest`) | сборка, тесты, документация |
| Интернет при первой сборке | — | CMake скачивает [Doctest](https://github.com/doctest/doctest) 2.4.11 для тестов |
| Icarus Verilog (`iverilog`, `vvp`) | 10+ (проверено на 12.0), необязательно | моделирование сгенерированных схем (цель `demo`, сценарий 2) |
| Verilator | 4.x или 5.x, необязательно | статическая проверка (lint) сгенерированных схем (сценарий 2) |
| Doxygen | 1.9+, необязательно | пересборка документации `docs/html` |
| clang-format | 14+, необязательно | форматирование исходников |

Ubuntu/Debian:

```sh
sudo apt-get install g++ cmake iverilog verilator doxygen clang-format
```

### Зачем нужны Icarus Verilog и Verilator

Для сборки и работы генератора они не нужны: `karatsuba_gen` только пишет
текст `*.v`. Эти программы проверяют уже сгенерированную схему:

* **Icarus Verilog** — симулятор. `iverilog` компилирует схему вместе с
  тестбенчем `tests/tb.v`, `vvp` запускает моделирование. Так проверяется
  поведение схемы: произведение `a · b` правильное, приходит ровно на 3-м такте
  и выдаётся на каждом такте (конвейер). Используется целью `demo` и в
  сценарии 2.
* **Verilator** — здесь используется только как линтер (`--lint-only`), без
  моделирования. Он проверяет сам текст схемы: ширины операндов и присваиваний
  совпадают, все сигналы объявлены и используются, нет защёлок и
  комбинационных петель, код синтезируем. В сгенерированном файле стоят
  комментарии `verilator lint_off/lint_on`: они отключают два предупреждения,
  которые возникают намеренно. Это неиспользуемые старшие биты `zd` и несколько
  модулей в одном файле.

### Сборка

Всё собирается CMake:

```sh
git clone <адрес репозитория> karatsuba_gen
cd karatsuba_gen
cmake -B build              # настройка (скачивает Doctest, ищет iverilog, doxygen, clang-format)
cmake --build build         # сборка build/karatsuba_gen и build/unit_tests
```

Сборка без интернета (без тестов): `cmake -B build -DKARATSUBA_TESTS=OFF`.
Если `iverilog`, `vvp` или `clang-format` установлены не в
стандартные каталоги, пути задаются при настройке:
`cmake -B build -DIVERILOG=/opt/bin/iverilog -DVVP=/opt/bin/vvp`.

### Тесты

```sh
ctest --test-dir build --output-on-failure
```

19 тестов на Doctest проверяют функции генератора и разбора аргументов: для
каждой функции — правильные и неправильные входные данные. Модель арифметики
Карацубы на C++ сверяется с обычным умножением для всех `N` ≤ 32.

Сгенерированную схему проверяет моделирование с тестбенчем `tests/tb.v`
(сценарий 2 ниже). Быстрый способ — цель `demo`, если установлен Icarus Verilog:

| Команда | Что делает |
|---|---|
| `cmake --build build --target demo` | сгенерировать умножитель на 32 бита и промоделировать его с `tests/tb.v` (другая ширина: `cmake -B build -DDEMO_N=48`) |
| `cmake --build build --target docs` | пересобрать `docs/html` (Doxygen) |
| `cmake --build build --target format` | отформатировать код clang-format |

## Использование

```
build/karatsuba_gen N [опции]
```

| Опция | Назначение |
|---|---|
| `N` | разрядность операндов, 1…4096 |
| `-o, --output FILE` | выходной файл (по умолчанию `<модуль>.v` в текущем каталоге); `-` — в stdout |
| `-m, --module NAME` | имя модуля верхнего уровня (по умолчанию `karatsuba_mul`); идентификатор Verilog, не ключевое слово |
| `-b, --base W` | разрядность, на которой рекурсия останавливается, 3…32 (по умолчанию 4) |
| `--leaf-star` | листовые умножители через `*` |
| `-h, --help` | справка |

Путь в `-o` может быть любым, относительным или абсолютным.
Коды возврата: `0` — успешно, `1` — не удалось записать файл, `2` — неверные
аргументы.

### Сценарий 1. Сгенерировать умножитель

```console
$ build/karatsuba_gen 32
karatsuba_gen: N = 32, latency 3 clocks -> karatsuba_mul.v
```

Файл `karatsuba_mul.v` содержит модули-листы, комбинационные шаги Карацубы и
верхний модуль. Верхний модуль для `N = 8` (`build/karatsuba_gen 8 -o -`):

```verilog
module karatsuba_mul (
    input  wire clk,
    input  wire rst_n,
    input  wire in_valid,
    output wire out_valid,
    input  wire [7:0] a,
    input  wire [7:0] b,
    output wire [15:0] p
);
    reg [2:0] vld;
    always @(posedge clk) vld <= rst_n ? {vld[1:0], in_valid} : 3'b000;
    assign out_valid = vld[2];

    // stage 1: split, pre-additions
    reg  [3:0] a0;
    ...
    always @(posedge clk) begin
        a0 <= a[3:0];
        a1 <= a[7:4];
        ...
        sa <= {1'b0, a[3:0]} + {1'b0, a[7:4]};
        sb <= {1'b0, b[3:0]} + {1'b0, b[7:4]};
    end

    // stage 2: three half width products
    karatsuba_mul_leaf_w4 u_z0 (.a(a0), .b(b0), .p(m0));
    karatsuba_mul_leaf_w4 u_z2 (.a(a1), .b(b1), .p(m2));
    karatsuba_mul_kara_w5 u_zm (.a(sa), .b(sb), .p(mm));
    ...

    // stage 3: z1 = zm - z2 - z0, recombination
    wire [9:0] zd = zm - {{2{1'b0}}, z2} - {{2{1'b0}}, z0};
    wire [8:0] z1 = zd[8:0];
    reg  [15:0] pr;
    always @(posedge clk) begin
        pr <= {z2, {8{1'b0}}}
            + {{3{1'b0}}, z1, {4{1'b0}}}
            + {{8{1'b0}}, z0};
    end

    assign p = pr;
endmodule
```

### Сценарий 2. Проверить схему в симуляторе

Тестбенч `tests/tb.v` подходит для любого `N`: разрядность передаётся
параметром `-P tb.N=...` и должна совпадать с той, что была у генератора.

```console
$ build/karatsuba_gen 32 -o mul32.v
karatsuba_gen: N = 32, latency 3 clocks -> mul32.v
$ iverilog -g2005 -P tb.N=32 -o tb.vvp mul32.v tests/tb.v
$ vvp -n tb.vvp
testbench for karatsuba_mul, N = 32
 latency    : 3 clocks, as required
 corners    : done
 random     : 256 pairs, then 256 with gaps
 N = 32, products checked = 408, errors = 0
RESULT: PASS
```

Тестбенч проверяет, что результат приходит ровно на 3-м такте и что на
каждом такте `p` и `out_valid` совпадают с эталоном `a * b`. Проверяются
граничные значения, все пары чисел при `N ≤ 8` и случайные числа, подаваемые
на каждом такте и с паузами. Другое число случайных пар и другое начальное
значение генератора случайных чисел: `vvp tb.vvp +nvec=100000 +seed=7`.

Временные диаграммы:

```sh
iverilog -g2005 -DDUMP_VCD -P tb.N=32 -o tb.vvp mul32.v tests/tb.v && vvp tb.vvp
gtkwave tb.vcd
```

Статическая проверка Verilator (тестбенч сюда не передаётся, проверяется
только схема). Если Verilator ничего не вывел и вернул код 0, замечаний нет:

```sh
verilator --lint-only -Wall --top-module karatsuba_mul mul32.v
```

### Сценарий 3. Умножитель для ПЛИС с DSP-блоками

```sh
build/karatsuba_gen 128 -b 16 --leaf-star -m mul128 -o rtl/mul128.v
```

Рекурсия Карацубы остановится на 16 битах, листы будут записаны как `a * b`,
и синтезатор положит их на аппаратные умножители. Модуль называется `mul128`,
его подмодули — `mul128_leaf_w…`, `mul128_kara_w…`, поэтому в одном проекте
можно использовать несколько умножителей разной ширины.

### Сценарий 4. Использование в своём проекте

```verilog
wire [63:0] x, y;
wire [127:0] prod;
wire prod_valid;

mul64 u_mul (.clk(clk), .rst_n(rst_n), .in_valid(x_valid),
             .a(x), .b(y), .out_valid(prod_valid), .p(prod));
// prod = x * y через 3 такта после x_valid; новые x, y — на каждом такте
```

### Сообщения об ошибках

```console
$ build/karatsuba_gen 4097
karatsuba_gen: N must be 1..4096, got '4097'
$ build/karatsuba_gen 16 -m wire
karatsuba_gen: 'wire' is not a usable Verilog module name
$ build/karatsuba_gen 8 -o /nonexist/x.v
karatsuba_gen: cannot open /nonexist/x.v
```

## Структура проекта

```
CMakeLists.txt         сборка, Doctest, ctest, цели demo, docs, format
Doxyfile               настройки Doxygen
.clang-format          стиль кода (Google, отступ 4, 100 колонок)
README.md              это руководство
docs/html/             документация к коду, собранная Doxygen
src/generator.h/.cpp   генерация Verilog
src/cli.h/.cpp         разбор аргументов, запись файла
src/main.cpp           точка входа, обработка исключений
tests/unit_tests.cpp   тесты C++ (Doctest)
tests/tb.v             тестбенч для моделирования схемы
```
