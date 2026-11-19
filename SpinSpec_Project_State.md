# SpinSpec — технический паспорт проекта

**Обновлено:** 2026-11-18  
**Версия ПО:** SpinSpec 1.1.0 (по состоянию обсуждения)  
**Назначение:** технический контекст для продолжения разработки, в том числе в новых чатах.

> **Правило:** этот документ — снимок подтверждённого состояния и планов, а не гарантия актуальности сервера. Перед правками сверять пути, службы и версии с текущей установкой. Не публиковать токены, SSH-ключи, пароли и пользовательские данные.

## 1. Назначение

SpinSpec моделирует спектры ЯМР на основе спинового гамильтониана: учитывает химические сдвиги, магнитное поле, скалярные J-взаимодействия, вычисляет собственные значения и векторы, частоты и интенсивности переходов. Поддерживаются расчёты от сильного до нулевого поля. В версии 1.1.0 добавлен расчёт вкладов отдельных групп спинов и интерференции между ними.

## 2. Архитектура веб-сервиса

- Публичная точка входа: **https://nmrspec.org**
- Размещение: Linux/VPS; обратный прокси **Caddy**; backend **FastAPI / Python 3**; вычислительное ядро на **C**, графики через Gnuplot.
- Каталог работающего приложения: `/opt/nmr-web/`.
- Служба: `nmr-web.service`, рабочий каталог `/opt/nmr-web/app`, запуск `/usr/bin/python3 /opt/nmr-web/app/app.py`.
- Backend слушает `127.0.0.1:8000` (подтверждено 2026-11-18).
- Тестовая копия: `/opt/spinspec-v1.1-test/`; отдельный порт `127.0.0.1:8001` (запущена вручную, HTTP 200 подтверждён 2026-11-18).

### Основные файлы и каталоги

| Путь относительно корня приложения | Назначение |
| --- | --- |
| `app/app.py` | FastAPI, API вычислений, обработка результатов |
| `templates/index.html` | Англоязычный интерфейс калькулятора |
| `templates/ru.html` | Русскоязычная страница (отдельный шаблон) |
| `templates/dev_spectrum.html` | Экспериментальный интерактивный просмотр спектра |
| `static/` | Статические ресурсы |
| `bin/engine_matrix` | Вычислительное ядро |
| `bin/matrix_graph` | График общего спектра |
| `bin/group_graph` | График групповых вкладов |
| `jobs/` | Результаты расчётных заданий |
| `examples/` | Примеры входных файлов |
| `logs/` | Журналы и аудит |

Сопутствующие исходники десктопного проекта: `engine_matrix.c/.h`, `matrix_graph.c/.h`, `GnuGraph.c`, `cre_SVG.c`, `Matrix_launcher.py`.

## 3. Критическая особенность тестовой установки

В исходной тестовой копии до исправления файл `/opt/spinspec-v1.1-test/app/app.py` содержал:

```python
BASE_DIR = Path("/opt/nmr-web")
```

**Исправлено и проверено 2026-11-18:** тестовый `app.py` использует `BASE_DIR = Path("/opt/spinspec-v1.1-test")`, `127.0.0.1:8001`; отсутствовавшие каталоги (включая `jobs/`) созданы. `python3 -m py_compile` выполнен без ошибок; `engine_matrix`, `matrix_graph`, `group_graph` присутствуют и исполняемы. Тестовый сервер вручную запущен командой `cd /opt/spinspec-v1.1-test/app && python3 app.py`; запрос `GET /` вернул HTTP 200. Работающую службу на 8000 не изменяли.

**Ограничение:** тестовый сервер пока запущен вручную, отдельная systemd-служба не создана, процесс может остановиться после завершения терминала.

Не изменять и не перезапускать публичную службу, пока тест не пройден.

## 4. API и групповые спектры

Основная точка: `POST /api/calculate`.

Параметр `observer_groups` задаёт идентификатор группы **для каждого спина в порядке входного файла**, например `"0 0 0 1"` для трёх протонов и одного фосфора.

API возвращает, среди прочего:

- `image_url`, `spectrum_url`, `result_url` — общий спектр и расчётные данные;
- `group_image_url` — график групповых вкладов;
- `group_spectrum_url` — CSV инвариантных сил переходов.

Файл `Sp_invariant_00000.csv` содержит `frequency_hz`, `total_strength`, `interference`, `strength_g0`, `strength_g1` и информацию об энергетических блоках. Для каждого перехода:

```text
total_strength = sum(strength_g*) + interference
```

Интерференция может быть отрицательной. Не интерпретировать сумму вкладов без интерференции как полный сигнал.

### Контрольный расчёт, 2026-11-18

Система `1H,1H,1H,31P` при `B=0`, `J(P,H)=11 Hz`, группы `0 0 0 1`.

| Частота | Полная сила | g0 | g1 | Интерференция |
| --- | ---: | ---: | ---: | ---: |
| 11 Hz | 0.17707399384059491 | 0.50001223846976861 | 0.081975607182504184 | -0.40491385181167788 |
| 22 Hz | 0.22134249230074357 | 0.62501529808721068 | 0.10246950897813026 | -0.50614231476459737 |

Значения соответствуют контрольной аналитической картине для XA3: ненулевые линии на 11 и 22 Hz. В CSV также встречаются нулечастотные записи и практически нулевые запрещённые переходы (порядка 1e-30).

Успешно проверено: HTTP 200 от API; присутствие всех пяти ссылок в ответе; получение `group_spectrum_00000.jpg` локально и через публичный домен (170976 байт в обоих случаях). **Это не заменяет визуальной проверки графика.**

## 5. Текущий HTML-интерфейс

`templates/index.html` уже содержит:

- контейнер `.page` с `grid-template-columns: 460px 1fr`;
- `.left-panel` с полной формой `#nmrForm`;
- `.right-panel` с `#previewImage` и `#groupPreviewImage`;
- поле `#observerGroups`, где группы вводятся вручную;
- клиентскую логику отправки `observer_groups` и отображения `group_image_url`.

Не удалять и не упрощать имеющиеся элементы формы: пользователь требует **сохранить все параметры, примеры, загрузку/сохранение .set, кнопки и интерактивный режим**.

## 6. Текущая задача: переработка интерфейса

1. **Слева** — вся существующая форма, фиксированная ширина **460 px** на широком экране, прокручиваемая независимо (**проверено в браузере**).
2. **Справа** — область изображения спектра занимает оставшуюся ширину и остаётся на месте при прокрутке параметров; при необходимости её содержимое прокручивается отдельно (**проверено в браузере**).
3. Адаптивная компоновка для небольших экранов.
4. Добавить удобный выбор групп спинов в форме вместо необходимости вручную набирать строку идентификаторов.
5. Определять **кандидатные** группы из `.set` по совпадению типа ядра/гиромагнитного отношения, химического сдвига и картины связей с остальными спинами; совпадение одного `gamma` или одной J-константы недостаточно для строгой магнитной эквивалентности.
6. Предусмотреть ручное исправление групп и передачу в существующий `observer_groups`.
7. Отображать вклад каждой группы, интерференцию (в том числе отрицательную) и общий спектр, сохраняя корректное физическое толкование.
8. Ориентироваться на функциональность десктопной версии, не менять без необходимости расчётное ядро.

### Статус задачи

**2026-11-18: компоновка установлена в тестовый `templates/index.html` и проверена через SSH-туннель в Chrome по `http://127.0.0.1:18001/` (локальный порт 18001 → серверный 8001).** CSS-маркер: `SPINSPEC_FIXED_LAYOUT`; на широком экране `grid-template-columns: 460px minmax(0, 1fr)` по смыслу (проверь фактическую строку CSS перед правками). При пробном расчёте отображался спектр примера 1,3,5-trifluorobenzene. Пользователь подтвердил, что ширина 460 px удобнее 40%. Существующие поля формы сохранены. **Публичный интерфейс не меняли.**

**Обновление 2026-11-18:** на тестовую установку загружен новый `index.html` с блоком *Group-resolved spectrum*: включение расчёта, *Auto-detect groups*, *Separate all spins*, выбор группы для каждого ядра и расширенный ввод ID. Пользователь подтвердил, что автоматическое разбиение работает и ручной выбор групп работает. Пример ручного выбора для шестиспиновой системы: `0 0 0 1 1 2` (спины 0–2, 3–4, 5). Ранее предполагавшаяся проблема с пропуском номера группы была ошибочной интерпретацией скриншота. **Не завершено:** основной график `previewImage` всё ещё отображает спектры отдельных спинов по `Displayed spin numbers`. Отдельный график групп `groupPreviewImage` находится ниже по странице; необходимо проверить его фактическое содержимое и реализовать удобное переключение между режимами *по ядрам / по группам* в правой области без изменения физического расчёта.

**Дополнение 2026-11-18:** тестовая страница показывает групповой график после исправления CSS, однако у него абсолютная ось частот (~8×10^7–2.2×10^8 Hz при B=5000 мТл), поэтому локальные J-линии практически не видны. Контрольный job `b53b103641f9` (TriEthyl Phosphate, 6 спинов, 5000 мТл): максимум в обычном `Sp_00000.dat` на 212889706.206348 Hz, в инвариантном `Sp_invariant_00000.csv` на 212889712.36139005 Hz. Это *максимумы по разным операторам наблюдения*, не тест тождественности одной линии. Новый экспериментальный `group_graph.c` подготовлен в чате и проверен GCC/синтетическим CSV, но **пока не установлен и не проверен реальным Gnuplot на сервере**. Идея: читать `Graph` и полуширину окна из `input.set`, получать точные центры из `Res_<spin>_<frame>.dat` (как обычный GnuGraph), строить по группам локальные окна `nu - nu0` и показывать суммарную кривую, группы и знаковую интерференцию. Изменение ограничено визуализатором без изменения ядра.

## 7. Порядок продолжения в новом чате

1. Прочитать этот файл и проверить фактическую версию в рабочем/тестовом каталогах.
2. Убедиться, что тестовый сервер на 8001 работает (он запускался вручную), а публичный сервер на 8000 не затронут.
3. Считать двухпанельную компоновку тестовой страницы согласованной (левая панель 460 px, отдельная прокрутка); при необходимости повторно проверить в браузере.
4. Реализовать автоматическое предложение групп и ручное редактирование.
5. Повторить контрольный расчёт H3–31P, проверить общий и групповой графики и CSV.
6. Только после проверки переносить изменения в рабочий сервис; обновить этот документ с датой, проверками и следующими задачами.

## 8. Безопасность и сопровождение

- Не хранить в репозитории административные токены, SSH-ключи, пароли, пользовательские расчётные данные или содержимое закрытых конфигураций.
- Административный токен службы был случайно выведен в терминальном отчёте в чате 2026-11-18; **необходимо ротировать его**, не копируя новое значение в чат.
- Перед заменой файлов делать резервные копии и фиксировать, какая версия установлена.
- После каждого значимого изменения актуализировать **дату, статус проверки и следующий шаг** в этом файле.

---
*Источник: последовательная проверка конфигурации и контрольных расчётов в чате проекта 18 ноября 2026 года. Неподтверждённые планы явно отмечены.*


**Групповой визуализатор, проверка 2026-11-18:** пользователь скомпилировал `group_graph_new.c` в `/opt/spinspec-v1.1-test/bin/group_graph_test` и проверил на job `d4d050e6dede` без пересчёта, получено `group_spectrum_00000.jpg (3 frequency windows, 3 groups)`. Затем `group_graph_test` установлен вместо прежнего `group_graph` (с резервной копией `group_graph.before-windowed-groups.bak`) в **тестовой** версии. Новый веб-расчёт дал три локальных частотных окна -20…+20 Гц с общим спектром, групповыми кривыми и знакопеременной интерференцией. В присланном скриншоте центры ¹H 21288.97128 и 21288.91248 Гц, ³¹P 8620.00000 Гц, то есть фактическое поле **0.5 мТл**, а не предполагавшиеся 0.05 мТл. Пользователю предложен новый тест при 0.05 мТл. Две протонные панели почти дублируются, так как разность центров ~0.059 Гц меньше ширины линии 0.5 Гц. **Предстоящее:** проверить слабопольный 0.05 мТл график, затем организовать переключатель обычного / группового изображений в UI и при необходимости улучшить подписи (Group вместо Spin). Публичный сайт без изменений.


**2026-11-19, CSV export task (prepared, NOT yet deployed):** User requested documented numerical export for both ordinary and group spectra. Studied real C-engine formats. Ordinary `Sp_00000.dat`: `n x1 x2 i j I`; last field `I` is product-basis lookup and should NOT be exposed as eigenstate-transition spin assignment. Prepared test-site Python patch to export `Sp_transitions_00000.csv` (`state_i,state_j,frequency_hz,relative_strength`), add `spectrum_csv_url` to FastAPI `JobResult`, a unified collapsible export widget that follows `Calculate contributions of spin groups`, and detailed EN site tutorial + brief RU site description. No numerical engine modifications. Existing group output `Sp_invariant_00000.csv` retains 9 base columns (`block_a,block_b,dim_a,dim_b,energy_a_hz,energy_b_hz,frequency_hz,total_strength,interference`) and `strength_gN` per group. Mode strengths have distinct operator normalizations; do not compare directly. Added full reference `docs/SPECTRUM_CSV_FORMAT.md` in GitHub commit ab49d1f89d071033a522f0b01222437fd5fa099b. The script is locally syntax checked and synthetic conversion tested, but has **NOT been run on the user's test server**. Need test with backups, restart test FastAPI only, verify both modes, then consider public deployment.


**CSV export verification 2026-11-19:** test patch installed in `/opt/spinspec-v1.1-test` (`app/app.py`, `templates/index.html`, `static/tutorial.html`, `templates/ru.html`); test FastAPI restarted PID `2134595`, HTTP GET `/` returned 200. `ensure_ordinary_csv` was executed manually on job `b9b9ced01522`, successfully created CSV with columns `state_i,state_j,frequency_hz,relative_strength` and meaningful sample rows. **Ordinary automatic CSV export confirmed** on new web job `198fb77cfb6e` at `/opt/spinspec-v1.1-test/jobs/198fb77cfb6e/Sp_transitions_00000.csv` (54 KB, created 2026-11-19 10:49 server time). **Group CSV and download links/tutorial not yet verified end-to-end**. Next: new web calculation with group option checked; inspect `Sp_invariant_00000.csv` (9 base columns plus strength_gN), then check links to documentation. Public site remains unchanged.


**2026-11-19, CSV exports end-to-end confirmed:** User verified successful browser downloads of BOTH ordinary `Sp_transitions_00000.csv` and grouped `Sp_invariant_00000.csv`, and existence of grouped auto-export at job `ef8d3373eeff` (170 KB) with three group columns. Numerically checked row `0,1`: `total_strength=0.56663863443933615`, `interference=-0.095885962485749987`, `strength_g0=0.0034877098620350187`, `strength_g1=0.65903688706305108`, `strength_g2=0` and equality. Both mode-specific export controls and tutorial link visible. **Next task:** on group mode auto-select one display representative per selected group instead of manual `Displayed spin numbers`; sample `0 0 1 1 1 2` with user Graph `0 2` should generate `Graph 0 2 5` with three plot windows, preserving manual Graph unchanged in ordinary mode. Also preserve coincident reference windows (especially at B=0). Candidate local test-only patch `/mnt/data/spinspec_group_windows_patch.py` created, syntax-checked on current app copy, compiled C and tested example grouping, but **not installed or server-tested**. It changes only `app/app.py` and `bin/group_graph_new.c`, creates backups, and does not restart services. User prefers step-by-step shell commands. Public website unchanged.


**2026-11-19, automatic group windows validated in browser:** User installed `/opt/spinspec-v1.1-test/spinspec_group_windows_patch.py`, which updated `app/app.py` and `bin/group_graph_new.c` with dated backups. Python `py_compile` passed; `gcc -O2 -Wall -Wextra -o group_graph_test group_graph_new.c -lm` passed cleanly; executable replaced **only in test** with backup. Verified test server process PID `2134595` on port 8001 before restart; screenshot after restart shows `Calculation completed` with **three separate group-resolved subplots** for group membership `0 0 1 1 1 2`, representatives spins `0,2,5`, with labels group 0, group 1, group 2. Reference frequencies in screenshot: group0 2128.84166 Hz, group1 2002.75580 Hz, group2 2128.85000 Hz. Distinct panels maintained despite group0 and group2 nearly overlapping in absolute frequency (0.00834 Hz difference). Group ordinary/manual-mode regression and exact-zero-field coincidence case **still to be tested**. Public nmrspec.org remains unchanged.
