## 1. Ядро і конфігурація

| Параметр | Значення | Чому так (типове значення) |
| :--- | :--- | :--- |
| Ядро | FreeRTOS-Kernel **V11.1.0**, git submodule `firmware/Middlewares/FreeRTOS` (коміт `dbf7055`) | офіційний репозиторій, зафіксований тег |
| Порт | `portable/GCC/ARM_CM4F` | Cortex-M4 з FPU, `-mfloat-abi=hard` |
| Heap | `heap_4`, `configTOTAL_HEAP_SIZE` = 24 КБ | об'єднує вільні блоки; розмір = сума стеків і TCB із запасом, перевіряється командою `HEAP` |
| Тік | `configTICK_RATE_HZ` = 1000 | та сама роздільна здатність 1 мс, що й `HAL_GetTick` у ЛР3 |
| `configMAX_PRIORITIES` | 8 | продукт використовує 1…6, 7 — вимірювальна задача (типово 5) |
| `configUSE_PORT_OPTIMISED_TASK_SELECTION` | 1 | вибір задачі інструкцією CLZ |
| `configUSE_RECURSIVE_MUTEXES` | 1 | замок консолі береться і в `printf`, і навколо блоку рядків |
| `configUSE_NEWLIB_REENTRANT` | 0 | `printf` серіалізовано м'ютексом (див. 4); `struct _reent` на задачу не потрібна |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | перевірка шаблону стека при кожному перемиканні (типово 0) |
| `configUSE_MALLOC_FAILED_HOOK` | 1 | `FATAL FreeRTOS heap exhausted` у консоль |
| `configUSE_TICK_HOOK` | 1 | службовий пін PC11 і розширення лічильника DWT до 64 біт |
| `configGENERATE_RUN_TIME_STATS` | 1, лічильник DWT CYCCNT, `configRUN_TIME_COUNTER_TYPE` = `uint64_t` | 32-бітний CYCCNT переповнюється кожні 51 с; розширено в тік-хуку |
| `configUSE_TRACE_FACILITY`, `..._STATS_FORMATTING_FUNCTIONS` | 1 | команди `TASKS`, `STACK` |
| `traceTASK_SWITCHED_IN` | `Platform_TraceSwitchedIn()` | кільцевий буфер перемикань з мітками DWT + службові піни |
| `configUSE_TIMERS` | 1, служба таймерів пріоритет 2 | таймер LED (FR-08) |
| `configMAX_SYSCALL_INTERRUPT_PRIORITY` | 5 | ISR з викликами `FromISR` мають пріоритет ≥ 5 |

Значення в блоках `#ifndef` (`configTICK_RATE_HZ`, `configTOTAL_HEAP_SIZE`, розміри стеків `STACK_*`) перевизначаються при збірці для сценаріїв 7.2:

```bash
make build EXTRA_CFLAGS="-DconfigTICK_RATE_HZ=100"
```

### Переривання і час HAL

| Переривання | Пріоритет NVIC | Що робить |
| :--- | :--- | :--- |
| TIM5 | 0 | `HAL_IncTick()` — час HAL (`HAL_GetTick`, `HAL_Delay`, тайм-аути драйверів). API ядра не викликає |
| USART2 RX | 5 | байт у кільцевий буфер (як у ЛР3) + `vTaskNotifyGiveFromISR(cmd)` |
| EXTI15_10 (B1) | 6 | мітка DWT, PC9 = 1 (як у ЛР3) + `vTaskNotifyGiveFromISR(btn)` |
| SysTick, PendSV, SVC | 15 (ядро) | тік і перемикання задач FreeRTOS |

* **Час HAL.** SysTick тепер належить ядру, тому HAL переведено на TIM5 («Timebase Source = TIM5» у термінах CubeMX, файл `stm32f4xx_hal_timebase_tim.c`). Так `HAL_GetTick()` правильний і до старту планувальника (самоперевірка), і при зміні частоти тіку.
* **Групи пріоритетів.** `HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4)` — у ЛР3 було `GROUP_0`, з яким `configASSERT` порту зупинив би програму.
* Пріоритети USART2 (було 0) і EXTI (було 1) знижено: інакше виклик `FromISR` з них порушив би правило `configMAX_SYSCALL_INTERRUPT_PRIORITY`.

## 2. Задачі

Правило призначення пріоритетів: чим жорсткіший дедлайн (`docs/tasks.md`), тим вищий пріоритет; довга операція — найнижчий.

| Задача | Пріоритет | Запуск | Стек, слів | Тіло (з ЛР3) | FR | Дедлайн | Чому такий пріоритет |
| :--- | :---: | :--- | :---: | :--- | :--- | :--- | :--- |
| `btn` | 6 | сповіщення з EXTI | 256 | `Process_Button_Event()`, запуск `frame` | вимірювання | ≤ 50 мс | найжорсткіший дедлайн; ЛР3: 61 мс / 19,5 с (обмеження 2) |
| `temp` | 4 | `vTaskDelayUntil` 1000 мс | 320 | `App_TempRun()`: читання MPU6050, рішення про заслінку | FR-01…03 | 1000 ± 10 мс | жорсткий період, коротка робота (≈ 1 мс I2C) |
| `tick` | 3 | `vTaskDelayUntil` 1000 мс | 320 | `App_TickRun()` + рядок `LOAD` | діагностика | ± 10 мс | метрика джитера; нижче за `temp`, бо лише діагностика |
| `cmd` | 3 | сповіщення на кожен прийнятий байт | 768 | `Console_Poll()` | команди | ≤ 100 мс | людина чекає відповіді; рівний `tick` — ділять час |
| `camera` | 2 | 500 мс після кожної зйомки | 320 | `App_CameraRun()`: 8 рядків, жест | FR-04…06 | жест ≤ 2 кадри (1,2 с) | дедлайн у секундах |
| `journal` | 2 | черга від логіки заслінки | 448 | `Journal_Append()` | FR-07 | можна відкласти | flash повільна (стирання до 400 мс) |
| `Tmr Svc` | 2 | ядро; таймер `led` 20 мс | 256 | `App_LedRun()` | FR-08 | ≤ 50 мс | 20 мс із запасом під 50 мс |
| `frame` | 1 | сповіщення від `btn` | 448 | `Console_CaptureFrame()` (≈ 31 с) | ручна зйомка | — | довга операція має поступатися всім (обмеження 1) |
| `IDLE` | 0 | ядро | 128 | — | — | — | час простою = вільний процесор |

Вимірювальні задачі (створюються на старті, заблоковані до команди): `csw` (7, `CSWITCH`), `hog` (5, `STARVE`), `inv_hi` / `inv_med` / `inv_lo` (5 / 4 / 1, `INVERT`).

Розміри стеків — стартова оцінка (`.su`: `Console_Handle` 360 Б + `w25_self_test` 528 Б для `cmd`, плюс `printf`); остаточні значення — за командою `STACK` після S3 (учасник 3).

## 3. Взаємодія

```
 EXTI B1 ──notify──> [btn 6] ──notify──> [frame 1] ──printf──┐
 USART2 RX ──ring + notify──> [cmd 3] ──printf───────────────┤
 тік ──delayUntil──> [temp 4] ──┬─ stateMutex ─┬──> заслінка, серво
                                │              │
 500 мс ──delay──> [camera 2] ──┘              └──qJournal──> [journal 2] ──SPI──> W25Q64
 тік ──delayUntil──> [tick 3] ──printf─────────────────────────────────────────────┤
 таймер 20 мс ──> [Tmr Svc 2] ──> LED                                              │
                                                       консольний м'ютекс ──> USART2 TX
 [temp], [cmd: ID, MODE], самоперевірка ──── м'ютекс I2C1 (MPU6050, GY-89, SCCB камери)
 [journal], [cmd: F*] ────────────────────── м'ютекс SPI2 (W25Q64)
```

| Від | До | Механізм | Що передається | Чому цей механізм |
| :--- | :--- | :--- | :--- | :--- |
| EXTI B1 | `btn` | сповіщення задачі | факт натискання (час — `btn_t_event`) | отримувач відомий, найлегший механізм |
| USART2 RX | `cmd` | кільцевий буфер ЛР3 + сповіщення (лічильник = кількість байтів) | байти команди | лічильники `ore` / `drop` з ЛР3 зберігаються без змін, тож логи порівнянні |
| `btn` | `frame` | сповіщення задачі | «зняти кадр» | натискання під час передачі ігнорується, як у ЛР3 |
| `temp`, `camera` | заслінка | м'ютекс `stateMutex` (успадкування пріоритету) | стан `damper` / `vision` | обидві задачі змінюють той самий стан |
| `temp`, `camera` | `journal` | черга `qJournal`, 8 записів по 12 Б | запис журналу (копія) | запис у flash (до 400 мс) виноситься з задачі, що приймає рішення |
| усі | USART2 TX | рекурсивний м'ютекс консолі в `__wrap_printf` | рядки логу | один `printf` — неподільний рядок |
| `temp`, `cmd` | I2C1 | рекурсивний м'ютекс шини (у `platform_i2c.c`, `platform_sccb.c`) | право на шину | MPU6050 і SCCB камери на одній шині |
| `journal`, `cmd` | SPI2 | рекурсивний м'ютекс шини, береться разом із CS (`platform_spi.c`) | право на шину | транзакція flash = від CS↓ до CS↑ |

ISR лише фіксують подію і будять задачу; уся робота — у задачах. До старту планувальника всі замки — порожні операції, тому самоперевірка і хостові тести працюють як у ЛР3.

## 4. Що змінено в коді ЛР3

| Файл | Зміна |
| :--- | :--- |
| `App/Src/app.c` | тіла задач винесено у функції `App_TempRun`, `App_CameraRun`, `App_LedRun`, `App_TickRun` (super loop викликає їх за тими самими перевірками часу); слабкі хуки `App_StateLock/Unlock`, `App_JournalWrite` |
| `App/Src/console.c` | слабкі хуки `Console_ExtraCommand`, `Console_ExtraHelp`; команди ЛР3 без змін |
| `App/Src/app_rtos.c` | **новий**: задачі продукту, черга, таймер; слабкі хуки `AppRtos_MetricsInit/Tick` для вимірювального шару |
| `App/Src/rtos_metrics.c` | **новий** (вимірювальний шар): команди `TASKS` … `JSTAT`, рядок `LOAD`, задачі сценаріїв 7.2 |
| `Core/Src/main.c` | замість `while (1)` — `AppRtos_Start()`; пріоритет EXTI 6 |
| `Core/Src/stm32f4xx_it.c` | SVC / PendSV / SysTick — з порту; EXTI будить `btn` |
| `Core/Src/stm32f4xx_hal_msp.c` | `NVIC_PRIORITYGROUP_4`, пріоритет USART2 5 |
| `Core/Src/stm32f4xx_hal_timebase_tim.c` | **новий**: час HAL на TIM5 |
| `Platform/Src/platform_rtos.c` | **новий**: замки шин, хуки переповнення стека / heap / `configASSERT` |
| `Platform/Src/platform_trace.c` | **новий** (вимірювальний шар): трасування перемикань, 64-бітний лічильник DWT, тік-хук, службові піни PC10 / PC11, MSP |
| `Platform/Src/platform_{i2c,sccb,spi}.c` | замок шини навколо кожної транзакції |
| `Platform/Src/platform_board.c` | `__wrap_printf` під замком консолі; сповіщення з ISR прийому |
| `Platform/Src/platform_time.c` | `Platform_DelayMs` у задачі — `vTaskDelay` |
| `firmware/Makefile` | джерела ядра, `-fno-builtin-printf`, `-Wl,--wrap=printf`, `EXTRA_CFLAGS`; вимірювальний шар — `-DLAB4_METRICS=1` |

Хостові тести ЛР3: **126 / 126** проходять без змін.

## 5. Команди вимірювань (нові, команди ЛР3 не змінено)

Вимірювальний шар — окремі файли `rtos_metrics.c` і `platform_trace.c`, увімкнені прапорцем `LAB4_METRICS` (Makefile). Без нього продукт працює на ядрі сам: run-time stats, тік-хук і трасування у `FreeRTOSConfig.h` вимкнені, нових команд немає.

| Команда | Вивід | Пункт методички |
| :--- | :--- | :--- |
| `TASKS` | run-time stats (`vTaskGetRunTimeStats`) і `vTaskList` | 6.2 |
| `STACK` | `STACK task=… free_min_words=…` для кожної задачі + MSP | 6.3 |
| `HEAP` | `HEAP free=… min_ever_free=… total=… used_peak=…` | 6.4 |
| `TRACE [n]` | `TRC t_us=… dt_us=… task=…` — останні n перемикань | 6.5, хронологія |
| `CSWITCH [n]` | `CSWITCH … min_ns avg_ns max_ns` — від `xTaskNotifyGive` до пробудження задачі з вищим пріоритетом | 6.5 |
| `STARVE <ms>` | задача пріоритету 5 активно чекає ms | 7.2-3 |
| `INVERT M` / `INVERT S` | `INV lock=mutex|binsem hi_wait_us=…` | 7.2-1 |
| `QFLOOD [n]` | `QFLOOD sent queued dropped` — черга журналу 8 записів | 7.2-4 |
| `JSTAT` | лічильники журналу і проігнорованих натискань | — |
| `LOAD pct=` (кожну секунду) | 100 % − частка IDLE за секунду | 6.2; у ЛР3 метрика була некоректна |

Службові піни: PC8 — виконується будь-яка задача, крім IDLE; PC9 — від переривання B1 до задачі `btn` (як у ЛР3); PC10 — виконується `frame` (видно, як її витісняють); PC11 — імпульс тіку.

Сценарії, що потребують перезбірки: переповнення стека (`-DSTACK_TICK=48`), вичерпання heap (`-DconfigTOTAL_HEAP_SIZE=8192`), частота тіку (`-DconfigTICK_RATE_HZ=100`).

## 6. Очікувані нові проблеми (перевірити на стенді)

* **Переривання вимкнені під час читання камери.** `camera` (≈ 16 мс на кадр) і `frame` (≈ 64 мс на смугу) вимикають переривання — разом із тіком ядра. ОС цього не прибирає: джитер `tick` до 16 мс штатно і до 64 мс під час передачі кадру — обмеження 4 ЛР3 залишається.
* **Активне очікування кадру.** `cam_sync_frame_start()` крутиться в циклі до 100 мс; задача `camera` (пріоритет 2) при цьому не віддає процесор `journal` і `frame`.
* **Рядки base64 перемежовуються з `TICK`.** Тепер `tick` друкує посеред передачі кадру; скрипт декодування кадру має пропускати чужі рядки.
* **Довгі команди в `cmd`.** `FRAME`, `SNAP`, `FERASE` виконуються в задачі з пріоритетом 3 і блокують `camera` та `journal`.
