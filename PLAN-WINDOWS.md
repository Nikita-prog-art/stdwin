# Поддержка Windows в `std`: два плана

Документ описывает два независимых пути получения работоспособной версии `std`
под Windows и критерии, по которым их можно сравнивать.

| | Вариант A | Вариант B |
|---|---|---|
| Название | **MinGW-w64: нативный Win32** | **MSYS2 runtime: POSIX-слой поверх Win32** |
| Что на выходе | `.a` + заголовки, обычный Windows-бинарь | `.a` + заголовки, бинарь, требующий `msys-2.0.dll` |
| Объём работ (1 разработчик) | **~18–23 дня** | **~5–7 дней** |
| Риск | средний/высокий | низкий |
| Готовность к поставке пользователю | да | нет (см. §6.4) |
| Производительность I/O | близка к нативной | заметно ниже (эмуляция POSIX) |

Рекомендация в конце документа: **A — единственный путь для продукта**, B — полезный
короткий спайк (1 неделя), который проверяет самую рискованную часть (переключение
контекста и корутины) и даёт работающий Windows-прогон тестов почти сразу.

---

## 0. Статус реализации

Порт по варианту A выполнен и проверен. Сборка — кросс-компилятором clang из
nixpkgs (`pkgsCross.mingw-ucrt-x86_64-llvm.buildPackages.llvmPackages_23.clang`),
прогон тестов — нативно на Windows (через WSL interop, без Wine):
**1397 OK, 0 ERR, 0 SKIP**; Linux-прогон (GCC 16) — те же 1397 OK.
Дополнительно проверена сборка и работа с c-ares и xxhash (Windows-бэкенды DNS и
хеша): полный прогон — 1397 OK, 0 ERR.

Отличия от плана ниже:

- компилятор — clang 23 + libc++/libunwind, GCC не используется; вместо ручной
  сборки в MSYS2 UCRT64 — кросс-сборка из Linux со статической линковкой
  (бинарь зависит только от системного UCRT и `WS2_32.dll`);
- CI не добавлялся, а существовавший `.github/workflows/ci.yml` удалён: все
  прогоны делаются локально перед коммитами;
- фазы A6.1 (санитайзеры clang) и A7 (IOCP, бенчмарки) в текущий объём не входят;
- проверка выполнения — запуск `.exe` на Windows, а не Wine;
- OpenSSL-бэкенд TLS на Windows не проверен: кросс-сборка openssl в nixpkgs падает
  на компиляции `.rc` (`winver.h`); сам `ssl_socket.cpp` от платформы не зависит и
  собирается с включённым бэкендом на других платформах.

Реализованный платформенный слой: `std/sys/plat.h` + `std/sys/plat.cpp`, тег
сокетов в `int`, `WSAPoll`-поллер, UDP-пара как `EventFD`/`JoinPipe`,
Win64-переключение контекста с публикацией стека в TEB, Win32-примитивы потоков
(`SRWLOCK`, `CONDITION_VARIABLE`, `CreateSemaphore`, `CreateThread`),
UTF-8 → UTF-16 на границе Win32.


## 1. Терминология: что здесь называется «mingw» и «msys2»

Это важно зафиксировать, потому что словами «mingw» и «msys2» называют две разные вещи.

**MSYS2 — это дистрибутив** (пакетный менеджер + окружения). Внутри него живут окружения
`MSYS`, `UCRT64`, `CLANG64`, `CLANGARM64`, `MINGW64` (плюс legacy `MINGW32`/`CLANG32`) — см.
[документацию MSYS2](https://www.msys2.org/docs/environments/).
Они дают два совершенно разных рантайма:

- **`UCRT64` / `CLANG64` / `MINGW64`** — компиляторы `mingw-w64`, которые собирают
  **нативные** Windows-бинарники: Win32 ABI, никакого POSIX-слоя, зависимость только от
  системного `ucrtbase.dll` (+ `libwinpthread-1.dll`, если не линковать статически).
  **Это и есть вариант A.** Тулчейн при этом обычно берут именно из MSYS2 (или
  кросс-компилятором `x86_64-w64-mingw32-g++` на Linux).
- **`MSYS`** — окружение с рантаймом `msys-2.0.dll` (форк Cygwin). Бинарник получает
  полноценный POSIX: `fork`, `poll` по пайпам, `sys/uio.h`, pthreads, конвертацию путей.
  **Это и есть вариант B.**

Если под «msys2» имелась в виду «собрать нативную библиотеку тулчейном из MSYS2», то это
не отдельный план, а фаза A0 варианта A: код и рантайм те же, отличается только способ
запуска сборки. Ниже я развожу эти два смысла явно.

---

## 2. Цель и критерии готовности

Цель: `std` собирается и проходит свои тесты на Windows x64 **без изменения сигнатур
публичного API** (смысл `int fd` при этом меняется: на Windows он перестаёт быть
универсальным handle и несёт вид дескриптора, см. D1).

Критерии готовности (одинаковы для обоих вариантов, кроме пункта про рантайм):

1. `./build -j N` собирает `libstd.a` и все тестовые бинарники.
2. `./.build/tst/test --top=20` проходит полностью; список скипов — явный и маленький
   (цель: 0 скипов; допустимо ≤2 с письменным обоснованием).
3. TCP/HTTP/HTTP-сервер/TLS/DNS работают на loopback (соответствующие `*_ut.cpp` зелёные).
4. Корутины, `ThreadPool`, `Channel`, `Semaphore`, `Event` проходят stress-прогон
   (`dev/stress.sh` для Linux; для Windows — аналог, 10+ минут без падений).
5. CI-job на `windows-latest` зелёный и обязательный.
6. Публичные заголовки `std/*/*.h` не содержат Win32-заголовков; весь `#ifdef` — в `.cpp`
   и в одном служебном заголовке `std/sys/plat.h`.

Не входит в цель (для v1): IOCP-реактор, `msghdr`-специфичные фичи (`SCM_RIGHTS`,
`UDP_GRO`), `io_uring`, huge pages на Windows, ARM64.

---

## 3. Аудит: что уже переносимо

### 3.1 Методика

Прогнал каждый `.cpp` через mingw-w64-компилятор только на синтаксис:

```sh
x86_64-w64-mingw32-g++ -std=c++26 -I. -D_WIN32_WINNT=0x0A00 \
    -D__is_trivially_destructible=__has_trivial_destructor \
    -fsyntax-only std/<mod>/<file>.cpp
```

Оговорки:

- `-D__is_trivially_destructible=...` нужен только потому, что локальный компилятор —
  GCC 14 (Debian mingw-w64), а `std/typ/intrin.h` использует встроенный
  `__is_trivially_destructible`, которого в GCC 14 нет. MSYS2 `UCRT64` поставляет
  **GCC 16.2**, а CI-джоб «Tests Glibc» собирает проект GCC 16, — то есть на GCC 16 этот
  встроенный, скорее всего, доступен; это надо подтвердить первым же пунктом A0 (если нет —
  в `std/typ/intrin.h` добавляется `#if !defined(__is_trivially_destructible)` с
  `__has_trivial_destructor` как запасным вариантом, это ~5 строк и не блокер).
- `-fsyntax-only` не покрывает линковку и не ловит отсутствующие *реализации* (например,
  `SemImpl` компилируется в «нет символа»). Поэтому 173 успеха — это **нижняя** оценка
  переносимости.

### 3.2 Результат

| Метрика | Значение |
|---|---|
| Всего translation unit (`std/*/*.cpp` + `tst/*.cpp`) | 209 |
| Компилируются mingw-компилятором как есть | **173 (83%)** |
| Падают | **36** |
| Файлов с уже существующими `#if defined(__linux__/__APPLE__/...)` | 10 |

### 3.3 Карта платформенных зависимостей

Уже изолировано (правки точечные, часто их вообще не нужно):

| Файл | Что изолировано |
|---|---|
| `std/thr/poller.cpp` | `epoll` (Linux) / `kqueue` (BSD/macOS); уже есть `PollPoller` и `STD_POLL` |
| `std/sys/event_fd.cpp` | `eventfd` (Linux) / пайп (fallback) |
| `std/sys/mem_fd.cpp` | `memfd_create` / `mkstemp` + `unlink` |
| `std/sys/num_cpu.cpp` | cgroup + `sched_getaffinity` / `sysctl` / `1` |
| `std/thr/semaphore.cpp` | `dispatch_semaphore` (macOS) / `sem_t` (Linux) |
| `std/thr/io_classic.cpp` | `accept4` (Linux) vs `accept` |
| `std/mem/obj_pool.cpp` | `mmap(MAP_HUGETLB)` / fallback через исключение |
| `std/thr/context.cpp` | `__x86_64__` (SysV asm) / `__aarch64__` (AAPCS asm) |
| `std/sys/crt.cpp` | `monotonicNowUs` (POSIX `clock_gettime`) |
| `std/tst/ctx.cpp` | `execinfo.h` за `__has_include` |

Нужен новый слой (нет ветки для Win32 вовсе):

`std/sys/fd.cpp`, `std/sys/fs.cpp`, `std/ios/fs_utils.cpp`, `std/ios/output.cpp`,
`std/ios/stream_tcp.cpp`, `std/net/tcp_socket.cpp`, `std/net/http_srv.cpp`,
`std/net/http_io.cpp`, `std/thr/io_classic.cpp`, `std/thr/poll_fd.cpp`,
`std/thr/reactor_poll.cpp` (косвенно), `std/dns/*.cpp`, `std/ios/sys.cpp` (консоль),
`build` (сборочный раннер).

### 3.4 Инвентарь падений (36 файлов, по причинам)

| Причина | Файлов | Файлы |
|---|---|---|
| нет `<sys/uio.h>` (`iovec`, `readv/writev`) | 9 | `std/sys/fd.cpp`, `fd_ut.cpp`, `std/ios/in_fd_ut.cpp`, `out_buf.cpp`, `out_fd_ut.cpp`, `stream_tcp.cpp`, `std/net/http_io.cpp`, `tcp_socket.cpp`, `std/thr/io_classic.cpp` |
| нет `<sys/socket.h>` / `<netinet/in.h>` / `<arpa/inet.h>` / `<netdb.h>` | 14 | `std/dns/{config,ares,record,system,iface_ut}.cpp`, `std/net/{http_srv,http_client_ut,http_srv_ut,tcp_socket_ut}.cpp`, `std/ios/stream_tcp_ut.cpp`, `std/thr/io_uring.cpp`, `tst/{http_serve_ok,http_serve_files,http_load}.cpp` |
| нет `<poll.h>` | 4 | `std/thr/poller.cpp`, `std/thr/poll_fd.cpp`, `std/thr/parker_ut.cpp`, `std/sys/event_fd_ut.cpp` |
| нет `<alloca.h>` | 3 | `std/thr/coro.cpp`, `std/thr/context_ut.cpp`, `std/ios/output.cpp` |
| нет `memmem()` | 1 | `std/str/view.cpp` |
| `dirent` без `d_type` | 1 | `std/sys/fs.cpp` |
| нет `setenv`/`unsetenv` | 1 | `std/sys/num_cpu_ut.cpp` |
| нет `_SC_PAGESIZE`/`sysconf` | 1 | `std/thr/thread_ut.cpp` |
| нет `pread`/`pwrite` | 1 | `std/thr/io_classic_ut.cpp` |
| нет реализации `Semaphore` для Win32 | 1 | `std/thr/semaphore.cpp` |

Вывод: **почти всё — это отсутствующие заголовки и 6 функций**. Настоящая работа не в
них, а в четырёх вещах: модель дескриптора, poller, eventfd-пробуждение и ABI
переключения контекста.

---

## 4. Общие проектные решения

Эти решения нужны в обоих вариантах. Для B часть из них не применяется (указано явно).

### D1. Модель файлового дескриптора (`int fd`)

Публичный API весь на `int`. На POSIX `fd` — универсальный handle: один и тот же
`read/write/close/poll/fcntl` работает для файлов, пайпов и сокетов, и весь код `std`
на этом построен. На Windows это неверно: **CRT-fd** (`_open`, `_pipe`) и **`SOCKET`**
(`UINT_PTR`) — независимые пространства имён с разными `close`/`read`/`write`/`fcntl`,
и числа в них могут совпасть (CRT-fd 4 и `SOCKET` 4 существуют одновременно).

В коде это уже видно: принятый сокет живёт в `ScopedFD` (`http_srv.cpp:343`,
`tcp_socket.cpp:154`), а `ScopedFD::~ScopedFD` зовёт `FD::close()` → `::close()`. На
Windows для сокета нужен `closesocket`, иначе `_close` вернёт `EBADF` и **сокет тихо
утечёт** (не крэш, не ошибка — просто утечка на каждом соединении). То же с
`FD::read/write/writeV/setNonBlocking`: на сокете нужны `recv/send/WSASend/ioctlsocket`.

Отсюда правило: **на Windows `int` перестаёт быть универсальным handle**, и вариант
«таблица вида по номеру» (ленивая классификация через `getsockopt(SO_TYPE)`) несостоятелен:

- одно число может быть и CRT-fd, и сокетом одновременно → одна таблица не может
  ответить верно для обоих;
- номера переиспользуются (`_open` отдаёт минимальный свободный, ядро переиспользует
  handle'ы), а закрытия идут и мимо `FD::close` — `::close` есть в `tcp_socket.cpp`,
  `http_srv.cpp` и в тестах, так что кэш протухает и врёт.

Два рабочих варианта:

**D1a. Тег в значении.** Вид кодируется битом 30: `sock | 0x40000000` (реальные значения
handle'ов ≤ 2^26, CRT-fd малы, так что бит свободен). `isSocket(fd)` — битовый тест,
`toNative(fd)` — снятие тега на границе Win32 (`WSAPoll`, `closesocket`, `ioctlsocket`,
`CreateIoCompletionPort`, c-ares). Все точки создания fd — в `std` (`socket`, `accept`,
`_open`, `_pipe`), внешние значения проходят через единственную фабрику (`stdOpen`).
Плюсы: вид есть у любого `int`, включая `PollFD` и `IoReactor`, таблиц нет, переиспользование
номеров не страшно. Минус: публичный `fd` перестаёт быть «сырым» числом (сравнивать
`ev->fd == sock.fd` по-прежнему можно — обе стороны тегированы, но печать и передача в
сторонние API идут через `toNative`).

**D1b. Вид внутри `FD`.** `FD` получает приватное `FdKind kind_`, `FD(int)` = File,
`FD::fromSocket(int)` для `accept`, `xchg` переносит вид, деструктор и `read/write`
диспетчеризуются по нему. Сигнатуры публичного API не меняются вовсе. Минус: `PollFD` и
`IoReactor` остаются без вида → нужен явный контракт «на Windows поллер принимает только
сокеты» и `STD_INSIST` в debug; `FD(int)` с сокетом всё ещё теряет вид.

Рекомендация: **D1a** — он закрывает `FD`, `PollFD`, `IoReactor` и будущий IOCP одним
механизмом. **D1b** — если не хочется менять смысл публичного `int`.

**Стоимость конверсии.** Тег — это один `and`/`or` в inline-функции: ~0,3 нс, без доступа
к памяти и без ветвления. Конверсия происходит **один раз на syscall** (никогда — на байт)
и только в `plat.cpp`: `recv/send/accept/connect/close/ioctlsocket/WSAPoll`,
`CreateIoCompletionPort` (будущее) и адаптер c-ares — около десяти точек. Для сравнения,
один вызов Winsock — сотни наносекунд, т.е. накладные <0,1%. На POSIX `toNative()` —
тождество, и компилятор его вырезает полностью. Дороже были бы отвергнутые варианты:
ленивая классификация через `getsockopt` — лишний syscall **на каждую операцию**,
поиск по таблице — лишнее обращение к памяти на каждой и вторая точка отказа.

Важно: разделение существует **только на Windows и только внутри платформенного слоя**.
Публичный API остаётся `int fd` для всех платформ, на POSIX значения остаются сырыми,
`toNative()` — тождество, а `isSocket()` в Linux-ветках не используется. Разделять надо
уровень syscall (`stdRecv`/`stdReadFile` в `plat.cpp`), а не уровень API: политика
(циклы по `EAGAIN`, retry на `EFAULT`, таймауты) остаётся общей — иначе две реализации
`FD::read` разойдутся.

Что нужно в обоих случаях:

- ассерт (debug) на то, что `SOCKET` влезает в `int`: handle'ы Windows — 32-битные индексы
  со знаком, усечение практически недостижимо, но инвариант надо зафиксировать и покрыть
  self-test'ом (создать N сокетов/файлов, проверить round-trip);
- сентинел `-1`: `INVALID_SOCKET` = все единицы, что совпадает с «закрыт» — удобно, но
  означает, что настоящий handle не должен иметь бит 31 (см. предыдущий пункт).

Для B (MSYS2 runtime) это решение не нужно: там `int` и есть POSIX-fd.

### D2. `eventfd`-эквивалент (пробуждение реактора)

`EventFD` используется `Parker`-ом и реактором; он обязан **поллиться**. Пайп на Windows
не поллится (`WSAPoll`/`select` работают только с сокетами).

Решение — пара соединённых UDP-сокетов на loopback (`bind 127.0.0.1:0`, узнать адрес,
`connect` навстречу): `signal()` пишет 1 байт, `drain()` читает до `EWOULDBLOCK`.
Дешевле TCP-пары (нет `listen`/`accept`), поллится везде. Альтернатива — `AF_UNIX`
(Windows 10 1803+), но поддержка `WSAPoll` для `AF_UNIX` не гарантирована — не берём.

`createPipeFD` остаётся **настоящим** пайпом (`_pipe` + `_O_BINARY | _O_NOINHERIT`).
Но `JoinPipe` в `std/thr/coro.cpp` зовёт `w.setNonBlocking()`, а анонимные пайпы Windows
в non-blocking не переводятся (`ioctlsocket` — только сокеты, `PIPE_NOWAIT` — только
именованные пайпы) → `JoinPipe` переводим на `createSocketPairFD`; `FD::setNonBlocking`
для пайпа возвращает `ENOTSUP`, а не делает вид, что сработал.

`createSocketPairFD` добавляем в `std/sys/fd.h` (на POSIX — `socketpair(AF_UNIX,
SOCK_STREAM)`, на Windows — UDP/TCP-пара на loopback). Так тесты идут по реальному
сценарию «сокет», а не через суррогат.

### D3. Poller

- A: `WSAPoll` (`PollPoller` уже есть, `STD_POLL` уже переключается на `WSAPoll`).
  `WaitablePoller::create()` для Windows возвращает `nullptr` → `HybridPoller` берёт
  `PollPoller`. `EpollPoller`-аналог не нужен.
- B: настоящий `poll(2)` от Cygwin; `epoll` в msys2-runtime нет (в дереве
  `winsup/cygwin/include/sys` заголовка `sys/epoll.h` нет) → тот же `PollPoller`.

Нюансы `WSAPoll`, которые надо закрыть кодом:

1. Константы `POLL*` в Winsock другие (`POLLIN=0x0300`, `POLLOUT=0x0010`,
   `POLLERR=0x0001`, `POLLHUP=0x0002`), поэтому «быстрая» ветка `poll_fd.cpp` под
   `__linux__` на Windows не компилируется — работает общая ветка, она корректна.
2. Записи с `fd < 0` (используются как «просто поспать», `IoReactor::sleep`) перед
   `WSAPoll` надо отфильтровывать.
3. Есть сообщения о некорректной обработке неудачного неблокирующего `connect`
   (не выставляется ошибка) — страховка уже есть в `io_classic::connect`: после
   `POLLOUT` читаем `SO_ERROR`.
4. `WSAPoll` — O(n) на вызов. Для серверов на тысячи соединений это потолок;
   снимается только IOCP (см. A7).
5. Один не-сокет в массиве — ошибка всего вызова (`WSAENOTSOCK`), а не «этот fd не
   работает»: пайп/файл/консоль ломают ожидание всех остальных. Поэтому на Windows
   контракт поллера — «только сокеты», проверяемый `STD_INSIST`/`ENOTSOCK` в `arm()`
   (см. D1), а не «любой fd, как на POSIX».

   Это ограничение **нельзя** делать общим: реактор будит себя через `Parker`, а
   `reactor_poll.cpp:151` армит `parker_.fd()` — на Linux это `eventfd`, на macOS/BSD
   пайп, и то и другое не сокет, но `epoll`/`kqueue` их принимают. Интерфейс
   `PollerIface` не меняется; различается только допустимое множество в бэкенде.

### D4. Переключение контекста (самое рискованное место)

`std/thr/context.cpp` содержит рукописный `swapContext` под SysV x86-64 и AAPCS aarch64.
Windows x64 — **другой ABI**: callee-saved = `rbx, rbp, rdi, rsi, r12–r15` **плюс
`xmm6–xmm15`, `MXCSR` и управляющее слово x87**. Кроме того, нет red zone, а аргументы
передаются в `rcx/rdx`, не в `rdi/rsi`.

План: добавить в `context.cpp` ветку `#if defined(_WIN32)` (и `__CYGWIN__` для B), с тем
же каркасом «naked-функция + inline asm + trampoline», но с правильным набором регистров:

```
frame (от младших адресов к старшим), которую swapContext восстанавливает:
  xmm6..xmm15 (160B) | mxcsr (4B) | x87cw (2B) | pad
  r15 r14 r13 r12 rsi rdi rbp rbx
  адрес возврата → trampoline (в rbx лежит this)
```

Отдельно проверить/закрыть:

- `trampoline` не должен зависеть от того, что компилятор не тронул `rbx` в прологе:
  сделать его `naked` и вызывать из asm `extern "C"`-функцию входа, а `this` передавать
  в `rcx`;
- выравнивание стека: как и сейчас, на входе трамплина `rsp % 16 == 8` (как после `call`);
- SEH: разворачивание через `swapContext` не происходит никогда (трамплин `[[noreturn]]`,
  тело корутины ловит исключения внутри `Runable`), поэтому отсутствие `.pdata` у naked
  функции допустимо — но это надо проверить тестом `context_ut` + броском исключения
  внутри корутины;
- `AsanFiberState` на Windows — no-op (у MinGW нет `__sanitizer_start_switch_fiber`);
- fallback на случай проблем: Windows Fibers (`ConvertThreadToFiber` / `CreateFiber` /
  `SwitchToFiber`) за тем же интерфейсом `Context`, включаемый флагом
  `-DSTD_CTX_FIBERS=1`. Медленнее и дороже по памяти, но снимает весь риск asm.

Для B есть более дешёвый путь: Cygwin предоставляет `ucontext` (`getcontext`/`swapcontext`),
и вариант B может использовать его, не трогая asm вообще (см. B2).

### D5. Файловый ввод-вывод

- `pread`/`pwrite` уже уходят в `offload` (thread pool) — это правильная модель
  для Windows; внутри заменяем на `ReadFile`/`WriteFile` с `OVERLAPPED.Offset` (без гонки
  `seek`), а не на `_lseeki64 + _read`.
- `fsync` → `_commit(fd)`; `fdatasync` → `FlushFileBuffers(handle)`.
- **Все** `open`/`pipe`/`fopen` обязаны получать `_O_BINARY`: иначе CRT сделает
  CRLF-трансляцию и «съест» `0x1A`. Это тихая порча данных в тестах и в HTTP-сервере
  (`tst/http_serve_files.cpp`).
- `stat`/`chsize` → `_stat64`/`_chsize_s`.

### D6. Пути и кодировки

Внутри всё в UTF-8 (`StringView` — байты). На границе Win32 конвертируем в UTF-16
(`MultiByteToWideChar(CP_UTF8)`) и вызываем `_wopen`, `FindFirstFileW`, `CreateFileW`.
Это не «улучшение», а требование корректности: ANSI-варианты ломаются на не-ASCII путях.
Для B ничего не нужно — Cygwin сам конвертирует UTF-8 → UTF-16.

### D7. Ошибки: `errno` vs коды Winsock

Код на каждом шаге проверяет `errno` (`EAGAIN`, `EINPROGRESS`, `ENOENT`). Winsock же
возвращает коды через `WSAGetLastError()` (`WSAEWOULDBLOCK = 10035` и т.д.).

Решение — тонкие обёртки в `std/sys/plat.h` (`stdRecv`, `stdSend`, `stdAccept`,
`stdConnect`, ...), которые при ошибке делают `errno = mapWsaError(WSAGetLastError())` и
возвращают `-1`. Тогда весь существующий код (`if (errno != EAGAIN) return errno;`)
продолжает работать без правок. Маппинг: `WSAEWOULDBLOCK → EAGAIN`,
`WSAEINPROGRESS → EINPROGRESS`, `WSAECONNRESET → ECONNRESET`, `WSAENOTSOCK → ENOTSOCK`,
остальное — по таблице; незнакомый код → `EIO`.

### D8. `iovec`

На Windows `struct iovec` нет. Вводим его в `std/sys/plat.h` (только для Win32) и
конвертируем в `WSABUF` (`{ULONG len; CHAR* buf}` — обратный порядок полей!) в
`writev`/`sendmsg`. Это дешевле, чем менять сигнатуры `IoReactor`.

### D9. `WSAStartup`

Один ленивый вызов из `TcpSocket::socket`/`createSystemDnsResolver` через
function-local static. `WSACleanup` не зовём (процесс умирает — ресурсы освободит ОС).

### D10. Публичный API не меняется

Все `#ifdef` — в `.cpp`, плюс один новый внутренний заголовок `std/sys/plat.h`
(включается только из `.cpp`). `plat.h` парный к `std/sys/plat.cpp` — по конвенции
«каждый `xxx.h` имеет `xxx.cpp`».

---

## 5. Вариант A — MinGW-w64 (нативный Win32)

### 5.1 Целевая конфигурация

| Параметр | Значение |
|---|---|
| Тулчейн | MSYS2 `UCRT64` (GCC 16.2) или кросс `x86_64-w64-mingw32-g++`; `CLANG64` (clang + lld) — вторая сборка, санитайзерная (A6.1); локально `llvm-mingw` кросс-компилятор |
| Стандарт | `-std=c++26` (GCC 16 поддерживает нужное подмножество: корутины, concepts) |
| CRT | UCRT (входит в Windows 10+) |
| Потоки | модель `posix` → winpthreads (линковать `-static` по умолчанию, чтобы не тащить `libwinpthread-1.dll`) |
| Минимальная ОС | Windows 10 x64 (для `UCRT` и `WSAPoll`) |
| Зависимости | `ws2_32`, `iphlpapi` (c-ares), `dbghelp` (трейсы, опционально), `winpthread` |
| Опционально | OpenSSL (ucrt64), c-ares (ucrt64), xxhash (ucrt64) |

### 5.2 Фазы

#### A0. Сборочный раннер и каркас CI — 2 дня

`build` — самодостаточный Python-раннер (3.5k строк) с предположениями POSIX:

- `native_target()` зовёт `os.uname()` (на нативном Windows-Python его нет) — нужен
  fallback через `platform.machine()/system()`;
- выходные файлы без `.exe`: MinGW при `-o .build/tst/test` создаёт `test.exe`, а раннер
  и `install()` ждут ровно `.build/tst/test` → добавлять суффикс на Windows;
- `./libstd` — удобный symlink (`os.symlink`) → на Windows без Developer Mode падает;
  нужен fallback на `shutil.copy2` (в CAS-части fallback уже есть);
- `os.link` (жёсткие ссылки CAS) на NTFS работает — оставить;
- флаги: убрать/обусловить `-mno-omit-leaf-frame-pointer` (проверить на GCC 16),
  `-mcx16` оставить только для x86_64, добавить `-D_WIN32_WINNT=0x0A00
  -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS`;
- линковка: `-lws2_32 -liphlpapi -ldbghelp`, `-static` для winpthreads;
- `dev/run.sh` использует `sh`, `timeout`, `ix` — на Windows не работает; нужен
  `dev/win.sh` (bash из MSYS2) или прямой вызов `./build` + `test.exe`.

**Приёмка:** на `windows-latest` (MSYS2 UCRT64) `./build -j 8` доходит до компиляции и
падает только на известных 36 файлах; `build --help` печатает Windows-target.

#### A1. Платформенный слой и дескрипторы — 3 дня

Новые файлы: `std/sys/plat.h`, `std/sys/plat.cpp`.

Содержимое `plat.h`: `<winsock2.h>`, `<ws2tcpip.h>`, `<windows.h>`, `<io.h>`, `<fcntl.h>`,
`<malloc.h>`, `<direct.h>`; `ssize_t`, `socklen_t`, `off_t`, `iovec`; `MSG_NOSIGNAL 0`;
`SHUT_*`; обёртки сокетов с маппингом `WSAGetLastError → errno` (D7); `createSocketPairFD`;
`toWide`/`fromWide` (D6); `fdKind(int)` (D1); `wsaInit()` (D9).

`plat.cpp`: реализация конверсий, таблицы видов fd, `WSAStartup`, маппинг кодов.

Правки: `std/sys/fd.cpp` (`_read/_write/_close/_commit`, `_pipe` с `_O_BINARY`),
`std/ios/fs_utils.cpp` (`_wopen`), `std/ios/sys.cpp` (`_setmode`, включить
`ENABLE_VIRTUAL_TERMINAL_PROCESSING`, чтобы ANSI-цвета в тестах не печатались мусором),
`std/ios/output.cpp` (подключить `plat.h` ради `iovec`), `std/sys/mem_fd.cpp`
(ветка Windows: `CreateFileW` c `FILE_FLAG_DELETE_ON_CLOSE`), `std/sys/fs.cpp`
(FindFirstFileW), `std/thr/thread_ut.cpp` (`GetSystemInfo` вместо `sysconf`),
`std/sys/num_cpu_ut.cpp` (`_putenv_s`).

**Приёмка:** `fd_ut`, `fs_ut` зелёные; `memFD` проверен вручную (отдельного теста в дереве
нет); чтение файла с не-ASCII именем работает; `cat`-подобный тест не портит бинарные данные.

#### A2. Poller, socketpair, eventfd — 3 дня

Правки: `std/thr/poll_fd.cpp` (`#include <winsock2.h>` вместо `<poll.h>`, оставить
не-Linux ветку маппинга, добавить `static_assert` соответствия `PollFlag` своим `PollFD`),
`std/thr/poller.cpp` (`STD_POLL` уже есть; отфильтровать `fd < 0`; `WaitablePoller::create`
→ `nullptr`), `std/sys/event_fd.cpp` (ветка Windows по D2), `std/sys/fd.cpp` +
`std/sys/plat.h` (`createSocketPairFD`), `std/thr/poller_ut.cpp` и `std/thr/io_classic_ut.cpp`
(5 тестов поллят `createPipeFD`) — перевести на `createSocketPairFD`; добавить тест
«`arm()` не-сокета даёт понятную ошибку и не ломает ожидание остальных fd».

**Приёмка:** `poller_ut` (все 10 тестов, включая `ArmWritable`/`ArmReadable`/`FdReported`),
`event_fd_ut`, `parker_ut` зелёные; `poller_ut` дополнительно прогоняется на
реальном loopback-сокете.

#### A3. Сокеты, реактор, ошибки — 5 дней

Правки: `std/net/tcp_socket.cpp` (WSASocket с `WSA_FLAG_OVERLAPPED` |
`WSA_FLAG_NO_HANDLE_INHERIT`, `ioctlsocket(FIONBIO)`, `closesocket`, `shutdown(SD_*)`,
`TCP_NODELAY`; `SO_REUSEADDR` оставляем, но помним, что на Windows он шире — разрешает
захват занятого порта, поэтому `http_srv` при желании переводим на отдельный
`setExclusiveAddr`), `std/thr/io_classic.cpp`
(обёртки из `plat.h`, `accept` без `accept4`, `EINPROGRESS` → `WSAEWOULDBLOCK`, `pread/pwrite`
по D5, `writev` через `WSASend`, `sendmsg/recvmsg/recvmmsg` → `ENOTSUP` с документированием),
`std/net/http_io.cpp`, `std/net/http_srv.cpp`, `std/ios/stream_tcp.cpp`,
`std/thr/io_uring.cpp` (заголовки `<sys/socket.h>` убрать под `__has_include`/`#if`, чтобы
TU был пустым вне Linux).

`std/net/ssl_socket.cpp` **правок не требует**: TLS реализован поверх `Input`/`Output`
через собственную BIO-методику (`BIO_meth_new` + `BIO_meth_set_read/write`), fd в этом
слое нет вообще. Это снимает целый класс рисков: OpenSSL/c-ares-подобных `int`-конверсий
в TLS-пути не будет.

**Приёмка:** `tcp_socket_ut`, `stream_tcp_ut`, `http_srv_ut`, `http_client_ut`,
`http_io_ut` зелёные; ручной `http_load`/`http_serve_ok` на loopback.

#### A4. Файлы, DNS, TLS — 3 дня

`std/dns/config.cpp` (SOCK_STREAM, `AF_INET/6` — из `ws2tcpip.h`), `std/dns/record.cpp`
(`inet_ntop` есть), `std/dns/system.cpp` (`getaddrinfo` + `WSAStartup`, `EAI_*`),
`std/dns/ares.cpp` (осторожно: `ares_socket_t` на Windows — `SOCKET`, не `int`; нужен
каст и `STD_ASSERT` на влезание в `int`), `std/sys/fs_utils.cpp` (`readFileContent`
через `_wopen` + `_O_BINARY`), тестовые `tst/http_*.cpp` (заголовки).

**Приёмка:** `iface_ut` (DNS), `record_ut`; HTTPS-запрос к внешнему хосту вручную.

#### A5. Потоки, корутины, переключение контекста — 4 дня

- `std/thr/context.cpp`: ветка Windows x64 (D4) + `STD_CTX_FIBERS` fallback;
- `std/thr/semaphore.cpp`: ветка `_WIN32` (`CreateSemaphore`/`WaitForSingleObject`/
  `ReleaseSemaphore`) либо `sem_t` winpthreads — выбрать по результату теста;
- `std/thr/thread.cpp`: `pthread_attr_setstack` в winpthreads объявлен, но нужно проверить,
  что он реально использует переданный стек (иначе — `startWithStack` через
  `CreateThread` + переключение на внешний стек, либо `ENOTSUP` и документирование);
- `std/thr/coro.cpp`, `std/ios/output.cpp`, `std/thr/context_ut.cpp`: `alloca.h` →
  `<malloc.h>` (или, лучше, заменить `alloca` на фиксированный буфер: `Context::implSize()`
  мал и константен);
- `std/str/view.cpp`: своя `memmem` (в `plat.cpp`, 20 строк);
- `std/sys/num_cpu.cpp`: `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`;
- `std/tst/ctx.cpp`: трейсы через `CaptureStackBackTrace` + `addr2line` (или оставить
  пустыми — не блокер).

**Приёмка:** `context_ut`, `coro_ut`, `mutex_ut`, `cond_var_ut`, `semaphore_ut`,
`thread_ut`, `pool_ut`, `channel_ut`, `wait_group_ut` зелёные; 10-минутный stress
без падений и без роста памяти.

#### A6. Полный прогон, CI, документация — 3 дня

- `windows-latest` + `msys2/setup-msys2` (`msystem: UCRT64`, пакеты `gcc`, `python`,
  `openssl`, `c-ares`, `xxhash`), шаги `./build` → `test.exe --top=20`;
- второй job `CLANG64` (clang + lld) как проверка второго компилятора;
- `BUILD.md`: раздел «Windows», список зависимостей, известных отличий;
- отчёт: какие тесты скипнуты и почему (цель — 0).

**Приёмка:** оба job'а зелёные на master; `BUILD.md` позволяет новому человеку собрать
за 10 минут.

#### A6.1 clang-ветка: санитайзеры — 2 дня (сильно рекомендуется)

clang на Windows даёт ASAN и UBSAN (`llvm-mingw`: «Address Sanitizer only works on x86»,
«ASAN only works properly with UCRT»), а TSAN на Windows **не поддерживается ни одним
тулчейном** (в документации clang перечислены Android, Darwin, FreeBSD, Linux, NetBSD —
Windows отсутствует). Отсюда:

- отдельный job `CLANG64` (`msystem: CLANG64`) с
  `-fsanitize=address,undefined -fno-sanitize-recover=undefined -g -O1` — **только тесты**;
- локально — кросс-компилятор `llvm-mingw` на Linux (умеет ASAN для mingw-цели, удобно
  для итераций без Windows-машины);
- `libclang_rt.asan-x86_64.dll` рядом с `test.exe` или в `PATH`, иначе бинарь не стартует;
- проверить три clang-специфичных места: `__attribute__((naked))` на x86_64 (нужен для
  `swapContext`), экспорт `__cxa_get_globals` из libc++abi (или `-static`), наличие
  `__sanitizer_start_switch_fiber` на Windows. Если fiber-хуков нет — корутино-тесты
  (`context/coro/pool/channel`) гонять в неинструментированной сборке, флаг
  `-DSTD_ASAN_NO_CORO=1`.

Что это ловит именно у нас:

- **18 приведений `iface → *Impl`** (например `(ContextImpl&)target` в `context.cpp`,
  `((CoroExecutorImpl*)this)` в `coro.cpp`) — это ровно то, что проверяет
  `-fsanitize=vptr`;
- переполнения в `str/buffer`, парсер HTTP, работа с `iovec`;
- и — после небольшой доработки — **use-after-destroy в пуле**: обычный ASAN его не видит,
  потому что `ObjPool` раздаёт память из больших чанков. Поэтому отдельная задача:
  `__asan_poison_memory_region` на слот после деструктора и `__asan_unpoison_memory_region`
  при выдаче (`std/mem/disposer.cpp`, `free_list.cpp`). В arena-архитектуре это
  единственный способ поймать обращение к уничтоженному объекту — и главный практический
  аргумент за санитайзерную сборку.

Поставка остаётся за GCC/UCRT64: libc++ и libstdc++ несовместимы на уровне объектных
файлов (это прямо сказано в README llvm-mingw), поэтому `.a` из clang-сборки нельзя
смешивать с GCC-потребителями. Если решим поставлять clang-артефакт — это отдельное
решение о том, для кого мы собираем.

#### A7. Производительность и (опционально) IOCP — 5+ дней

- бенчмарк: `perf_objpool`, `http_load` (req/s), пинг-понг TCP — сравнить с Linux;
- A/B-замер стоимости тега: собрать с `-DSTD_FD_TAG=0` (сырой cast без тега) и сравнить
  `http_load` + новый `perf_syscall` (цикл `recv/send` на loopback); критерий приёмки —
  разница ≤1%, иначе пересматриваем механизм;
- `WSAPoll` упирается в O(n); если нужны тысячи соединений — `IocpIoReactor` за тем же
  `IoReactor`, включается `USE_IOCP_REACTOR=1` (как `USE_POLL_POLLER`): `WSARecv`/`WSASend`,
  `ConnectEx`/`AcceptEx` через `WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)`,
  `CreateIoCompletionPort`, `GetQueuedCompletionStatusEx`;
- huge pages: `VirtualAlloc(MEM_LARGE_PAGES)` (требует привилегии) как аналог
  `ObjPool::hugePages` — сейчас фолбэк уже корректен, можно оставить.

### 5.3 Объём изменений

| Тип | Кол-во |
|---|---|
| Новые файлы | 2 (`std/sys/plat.h`, `std/sys/plat.cpp`) |
| Изменённые `.cpp` | ~18 |
| Изменённые публичные заголовки | 1 (`std/sys/fd.h` — `createSocketPairFD`; аддитивно) |
| Оценка диффа | ~1800–2500 строк |
| Оценка трудозатрат | 18–23 человеко-дня (без A7) |

### 5.4 Осознанно не входит в v1

`recvmsg`/`sendmsg` с control-сообщениями (`SCM_RIGHTS`, `UDP_GRO`, `UDP_SEGMENT`) —
возвращают `ENOTSUP`; `recvmmsg` — эмуляция циклом `WSARecvFrom`; IOCP — A7;
ARM64 — отдельная фаза (`context.cpp` для Windows ARM64 отличается: `x18` — TEB,
нельзя трогать); `AF_UNIX`; `fork`-подобные сценарии (их в библиотеке нет).

---

## 6. Вариант B — MSYS2 runtime

### 6.1 Что это

Сборка в окружении `MSYS` (`x86_64-pc-msys-g++`), линковка с `msys-2.0.dll` — форком
Cygwin. Код получает POSIX-семантику: `poll` по пайпам и сокетам, `sys/uio.h`,
`readv/writev`, pthreads, `opendir/readdir`, `mkstemp` + `unlink` открытого файла,
конвертацию UTF-8 путей. `_WIN32` при этом **не** определён — определён `__CYGWIN__`,
поэтому все существующие `#ifdef _WIN32`-ветки не активируются, а POSIX-ветки работают.

### 6.2 Что придётся править (по данным аудита)

| Файл | Проблема | Правка |
|---|---|---|
| `std/thr/poller.cpp` | нет `epoll`/`kqueue` | ничего: `WaitablePoller::create` → `nullptr` → `PollPoller` c настоящим `poll(2)` |
| `std/thr/poll_fd.cpp` | `#include <poll.h>` есть у Cygwin | ничего (общая ветка маппинга; значения `POLL*` у Cygwin = Linux, можно расширить `static_assert`) |
| `std/sys/event_fd.cpp` | нет `eventfd` | ничего: уже есть пайп-fallback |
| `std/sys/mem_fd.cpp` | нет `memfd_create` | ничего: `mkstemp`+`unlink` работает |
| `std/sys/num_cpu.cpp` | нет ветки | добавить `__CYGWIN__` → `sysconf(_SC_NPROCESSORS_ONLN)`, иначе вернёт 1 |
| `std/thr/semaphore.cpp` | нет ветки | `__CYGWIN__` → POSIX `sem_t` |
| `std/sys/fs.cpp` | `d_type` (проверить) | если нет — `stat()` по имени (дёшево, один вызов) |
| `std/mem/obj_pool.cpp` | нет huge pages | ничего: `fromHugePages` ловит `ChunkMapFailed` и отдаёт slave |
| `std/thr/context.cpp` | SysV asm | **главная работа**: `__CYGWIN__` → `ucontext` (`swapcontext`) или Windows-ABI asm из D4 |
| `std/thr/thread.cpp` | `pthread_attr_setstack` | проверить; если `EINVAL/ENOTSUP` — игнорировать стек |
| `std/ios/output.cpp`, `coro.cpp`, `context_ut.cpp` | `alloca.h` | у Cygwin есть |
| `std/str/view.cpp` | `memmem` | у Cygwin есть |
| `std/sys/num_cpu_ut.cpp`, `thread_ut.cpp` | `setenv`, `sysconf` | у Cygwin есть |
| `std/thr/io_uring.cpp` | `<sys/socket.h>` | у Cygwin есть; liburing нет → TU пустой |
| `build` | те же POSIX-предположения | `os.uname()` под msys-python есть; нужен `.exe` и fallback symlink'а (как A0) |

Итого: **~10–12 файлов, почти все — однострочные ветки**, плюс одна содержательная задача
(переключение контекста).

### 6.3 Фазы

#### B0. Окружение и сборочный раннер — 1 день

`pacman -S msys/gcc msys/python msys/make msys/openssl msys/libcares-devel` (что есть в
репозитории `msys`; отсутствующие опциональные библиотеки просто не подхватятся —
`__has_include`-фолбэки уже есть). Правки `build` — как в A0 (суффикс `.exe`, symlink).

**Приёмка:** `./build -j 8` доходит до компиляции; `test.exe --top=20` запускается.

#### B1. Компиляция и контекст — 3 дня

Ветки из §6.2 + контекст на `ucontext` (или Windows-ABI asm, если хочется сразу
переиспользовать работу в A). `ucontext` предпочтительнее для спайка: ~30 строк вместо
asm-отладки.

**Приёмка:** всё компилируется и линкуется; `context_ut` и `coro_ut` зелёные.

#### B2. Прогон тестов и триаж — 1–2 дня

Полный `test --top=20`. Ожидаемые проблемы: таймауты из-за медленной эмуляции,
тайминги в `channel_ut`/`parker_ut` (увеличить допуски или пометить
платформенно-зависимыми), `num_cpu` (после B1). Настоящий `poll(2)` по пайпам и сокетам
должен пройти без правок — это сильная сторона B.

**Приёмка:** ≥95% тестов зелёные, остальные — с письменным обоснованием.

#### B3. CI и документация — 1 день

`windows-latest` + `msys2/setup-msys2` (`msystem: MSYS`), `pacman -S gcc python make`.
В `BUILD.md` — раздел «MSYS2 runtime (не для поставки)».

**Приёмка:** job зелёный.

### 6.4 Ограничения и когда вариант уместен

1. **Бинарь требует `msys-2.0.dll`.** У конечного пользователя его нет; чтобы поставить
   библиотеку, придётся тащить рантайм. MSYS-окружение позиционируется как среда для
   инструментов, а не как целевая платформа приложения — вопросы распространения и
   лицензирования рантайма надо решать отдельно (`cygwin1.dll`/`msys-2.0.dll` — GPLv3+;
   это не юридическая консультация, а флаг на проверку).
2. **Производительность.** Каждый POSIX-вызов проходит через слой эмуляции с конвертацией
   путей; `poll`/`select` реализованы поверх `select` с фоновыми потоками ожидания; нет
   `epoll`; `fork` дорогой. Для библиотеки, у которой в README первым аргументом идёт
   скорость, это неприемлемо как продуктовый вариант.
3. **Отладка** сложнее: два рантайма, символика прошита через `msys-2.0.dll`.
4. **Не проверяет** самое важное для A: модель дескрипторов, `WSAPoll`, `WSAStartup`,
   UTF-16 границу. Проверяет только платформенно-нейтральный слой.

Где B действительно полезен: **дешёвый ранний сигнал** о том, что корутины, пулы,
контейнеры и тесты живут на Windows; проверка коммитов без Windows-машины (CI);
внутренние инструменты, которым POSIX-семантика важнее скорости.

---

## 7. Сравнение

| Критерий | A: MinGW-w64 | B: MSYS2 runtime |
|---|---|---|
| Целевой ABI | нативный Windows x64 | POSIX-эмуляция поверх Windows |
| Зависимости у пользователя | UCRT (в системе) + опц. OpenSSL/c-ares | `msys-2.0.dll` + все библиотеки MSYS2 |
| Поставка | да | нет |
| Трудозатраты | 18–23 дня | 5–7 дней |
| Изменения в коде | новый слой + 18 файлов | 10–12 файлов, точечно |
| Переключение контекста | свой asm под Win64 ABI (риск) | `ucontext` (дёшево) |
| I/O | прямые Win32-вызовы, `WSAPoll` O(n) | эмуляция, ещё дороже |
| Масштаб соединений | сотни–тысячи (IOCP — потом) | ограничен `select`-эмуляцией |
| Отладка | gdb/lldb, WinDbg; ASAN только в `CLANG64` (clang), у GCC/MinGW его нет | gdb, но через слой |
| Кросс-сборка с Linux | да (`x86_64-w64-mingw32-g++`) | нет |
| Запуск тестов локально на Linux | через Wine (частично) / CI / VM | только на Windows |
| Риск провала | средний | низкий |
| Ценность для продукта | высокая | нулевая (проверочная) |

---

## 8. Рекомендация и порядок работ

**Основной путь — A.** B не даёт поставляемого результата и не снимает ни одного
из ключевых рисков A (fd-модель, `WSAPoll`, `WSAStartup`, UTF-16), но стоит 5–7 дней.

**Если нужен ранний сигнал — делать B первым**, потому что пересечение работ велико:

| Работа | B | Переиспользуется в A |
|---|---|---|
| Правки `build` (`.exe`, symlink) | да | да (100%) |
| мелкие правки (`alloca`, `num_cpu_ut`, `thread_ut`) | да | да (100%) |
| Ветки `event_fd`,`mem_fd`,`poller`,`semaphore` | проверяются | частично (логика та же) |
| Переключение контекста | `ucontext` | нет (в A свой asm), но тесты `context_ut` уже готовы |
| fd-модель, `WSAPoll`, сокеты, UTF-16 | нет | — |

То есть B-first стоит ~6 дней, из которых ~4 переиспользуются; чистые потери ~2 дня.
Взамен на первой неделе появляется зелёный (или почти зелёный) Windows-прогон, который
показывает реальный объём проблем в тестах — это сильно снижает неопределённость оценки A.

**Если B-first:**
неделя 1 — B (B0–B3);
недели 2–5 — A (A0 частично уже сделан, A1–A6), A7 — по потребности.

**Если только A:** 4–5 недель одним разработчиком.

Вехи:

| Веха | Что готово | Когда (B-first) |
|---|---|---|
| M1 | libstd собирается, poller/eventfd/socketpair-тесты зелёные | конец недели 2 |
| M2 | TCP/HTTP/DNS на loopback | конец недели 3 |
| M3 | корутины/потоки/stress | середина недели 4 |
| M4 | CI зелёный, полный прогон | конец недели 4 |
| M5 | перф-отчёт, решение по IOCP | неделя 5 |

---

## 9. CI

```yaml
  windows:
    name: Build & test Windows (UCRT64)
    runs-on: windows-latest
    defaults:
      run:
        shell: msys2 {0}
    steps:
      - uses: actions/checkout@...
      - uses: msys2/setup-msys2@...
        with:
          msystem: UCRT64
          update: true
          install: >-
            mingw-w64-ucrt-x86_64-gcc
            mingw-w64-ucrt-x86_64-openssl
            mingw-w64-ucrt-x86_64-c-ares
            mingw-w64-ucrt-x86_64-xxhash
            make python
      - run: ./build -j "$(nproc)"
      - run: ./.build/tst/test.exe --top=20
```

Плюс отдельный (не блокирующий) job `MSYS` для варианта B, если решим его поддерживать.

Локальная разработка: кросс-компиляция `x86_64-w64-mingw32-g++` на Linux даёт быстрый
цикл «компилируется/не компилируется», но прогон — только Windows/VM/Wine.

---

## 10. Риски

| # | Риск | Вероятность | Влияние | Митигация |
|---|---|---|---|---|
| R1 | ABI переключения контекста (xmm6–15, MXCSR, SEH, red zone) | средняя | высокое | `context_ut` + бросок исключения внутри корутины; fallback на Fibers флагом; stress 10 мин |
| R2 | `SOCKET` не влезает в `int` | низкая | высокое | handle Windows — 32-битный индекс, усечение практически недостижимо; ассерт в debug + self-test round-trip; при проявлении — handle-таблица за тем же `int` |
| R3 | `WSAPoll`: O(n), баги с `connect`, лимиты | средняя | среднее | проверка `SO_ERROR`; фильтр `fd<0`; бенчмарк; IOCP в A7 |
| R4 | Неполлерные handle'ы (файлы, пайпы, консоль) в реакторе | средняя | среднее | `FdKind` (D1); блокирующие операции для файлов через `offload`; пайпы не армим |
| R5 | `pthread_attr_setstack` в winpthreads не использует стек | средняя | низкое | тест; fallback `CreateThread`/`ENOTSUP` |
| R6 | `_O_BINARY` забыт где-то → тихая порча данных | средняя | высокое | правило в `plat.h` (`stdOpen` — единственная точка); тест на бинарный файл с `0x1A` и `\r\n` |
| R7 | Не-ASCII пути | средняя | среднее | UTF-16 на границе (D6) + тест |
| R8 | `errno` vs WSA-коды | высокая | среднее | обёртки (D7) + тест на каждый сценарий (timeout, refused, reset) |
| R9 | Сборочный раннер (symlink, `.exe`, `os.uname`) | высокая | низкое | A0; CI как источник истины |
| R10 | Медленные/флакающие тесты на Windows (тайминги) | средняя | низкое | увеличить допуски точечно, не отключать тесты |
| R11 | ASAN на Windows есть только у clang (GCC/MinGW его не даёт), TSAN на Windows нет вообще | высокая | среднее | clang-job с `-fsanitize=address,undefined` (A6.1) + пул-пуазинг; гонки остаются за Linux-CI |
| R12 | c-ares: `ares_socket_t` (SOCKET) vs `int` | средняя | низкое | каст + ассерт; либо `getaddrinfo`-резолвер по умолчанию на Windows |

---

## 11. Открытые вопросы

1. **Минимальная версия Windows.** Win10 x64 (UCRT есть в системе) или нужно Win7/8
   (тогда MSVCRT-окружение и UCRT отдельным пакетом)? Это влияет на A0/A1.
2. **ARM64** нужен в v1 или отдельной фазой? (Для A это отдельная asm-ветка.)
3. **Структура кода:** ок ли `std/sys/plat.h` + `#if defined(_WIN32)` в существующих
   `.cpp`, или хочется отдельное дерево `std/plat/win32/`?
4. **`int fd`**: согласны оставить с допущением «`SOCKET` влезает в `int`» (D1), или
   принципиально нужен `intptr_t`/handle в публичном API?
5. **winpthreads**: только статическая линковка (`-static`) или допустима DLL?
6. **IOCP** в v1 или после релиза? (Сильно влияет на A7 и на позиционирование.)
7. **TLS**: достаточно OpenSSL или нужен mbedTLS (например, для статической сборки)?
8. **`recvmsg`/`recvmmsg`-фичи** (`SCM_RIGHTS`, UDP GRO): `ENOTSUP` приемлемо?
9. **Сколько тестов** допустимо скипнуть на Windows и какие именно?
10. **Поставка**: только `.a` + заголовки, или ещё CMake/`pkg-config`-файл?
11. Есть ли **Windows-машина/VM** для отладки, или всё через CI? (От этого зависит
    скорость A5.)

---

## 12. Приложения

### A. Воспроизведение аудита

```sh
ls std/*/*.cpp tst/*.cpp | xargs -P16 -I{} sh -c '
  x86_64-w64-mingw32-g++ -std=c++26 -I. -D_WIN32_WINNT=0x0A00 \
    -D__is_trivially_destructible=__has_trivial_destructor \
    -fsyntax-only "{}" 2>&1 | grep -m1 "error:" \
    && echo "FAIL {}" || echo "OK   {}"'
```

### B. Соответствие POSIX → Win32

| POSIX | Win32 / MinGW | Комментарий |
|---|---|---|
| `open/read/write/close` | `_open/_read/_write/_close` | CRT fd; обязателен `_O_BINARY` |
| `pread/pwrite` | `ReadFile`/`WriteFile` + `OVERLAPPED.Offset` | атомарно, без гонки `seek` |
| `fsync/fdatasync` | `_commit` / `FlushFileBuffers` | |
| `pipe()` | `_pipe(..., _O_BINARY)` | не поллится |
| `socket/closesocket` | `socket` / `closesocket` | `SOCKET` ≠ fd |
| `fcntl(F_SETFL, O_NONBLOCK)` | `ioctlsocket(FIONBIO)` | |
| `poll()` | `WSAPoll` | только сокеты |
| `epoll/kqueue` | — | `PollPoller`; IOCP — A7 |
| `accept4` | `accept` + `WSA_FLAG_NO_HANDLE_INHERIT` | `SOCK_CLOEXEC` эмулируется |
| `connect` + `EINPROGRESS` | `connect` + `WSAEWOULDBLOCK`, затем `SO_ERROR` | |
| `shutdown` | `shutdown(SD_SEND/SD_BOTH)` | |
| `MSG_NOSIGNAL` | — | на Windows нет `SIGPIPE` |
| `writev/readv` | `WSASend/WSARecv` (`WSABUF`) | конверсия в `plat.h` |
| `recvmsg/sendmsg/recvmmsg` | `WSARecvMsg` / — | v1: `ENOTSUP` |
| `getaddrinfo` | `getaddrinfo` (`ws2tcpip.h`) + `WSAStartup` | |
| `inet_pton` | `InetPtonA` (в mingw `inet_pton` — макрос) | |
| `opendir/readdir` | `FindFirstFileW`/`FindNextFileW` | `d_type` → `dwFileAttributes` |
| `memfd_create` | `CreateFileW(..., FILE_FLAG_DELETE_ON_CLOSE)` | |
| `eventfd` | пара UDP-сокетов на loopback | обязана поллиться |
| `pthread_*` | winpthreads | модель потоков `posix` |
| `sched_yield` | `SwitchToThread` | |
| `clock_gettime` | `clock_gettime` (mingw) | |
| `sysconf(_SC_PAGESIZE)` | `GetSystemInfo().dwPageSize` | |
| `sysconf(_SC_NPROCESSORS_ONLN)` | `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)` | |
| `alloca.h` | `<malloc.h>` | или заменить на буфер |
| `memmem` | своя реализация | 20 строк |
| `setenv/unsetenv` | `_putenv_s` | только тесты |
| `backtrace` | `CaptureStackBackTrace` + `addr2line` | не блокер |

### C. Эскиз `std/sys/plat.h` (не финальный код)

```cpp
#pragma once

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
    #include <malloc.h>

    struct iovec {
        void* iov_base;
        size_t iov_len;
    };

    // ssize_t / socklen_t / off_t у mingw уже есть в <sys/types.h>

    #define MSG_NOSIGNAL 0
    #define SHUT_RD SD_RECEIVE
    #define SHUT_WR SD_SEND
    #define SHUT_RDWR SD_BOTH

    enum class FdKind : u8 { Unknown, Socket, File, Pipe };

    void wsaInit();
    int  mapWsaError(int code);
    FdKind fdKind(int fd);
    void registerFd(int fd, FdKind kind);

    size_t toWide(const u8* utf8, wchar_t* out, size_t cap);
    int stdOpen(const u8* path, int flags);

    // обёртки: при ошибке ставят errno из WSAGetLastError() и возвращают -1
    int  stdRecv(int fd, void* buf, size_t len, int flags);
    int  stdSend(int fd, const void* buf, size_t len, int flags);
    int  stdAccept(int fd, sockaddr* addr, int* addrLen);
    int  stdConnect(int fd, const sockaddr* addr, int addrLen);
    void stdClose(int fd);
#endif
```

### D. Эскиз Windows-ветки `context.cpp` (не финальный код)

```cpp
#if defined(_WIN32) && defined(__x86_64__)
void ContextImpl::swapContext(u64*, u64*) {
    __asm__(
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %rdi\n\t"
        "pushq %rsi\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "subq $168, %rsp\n\t"
        "movdqu %xmm6,   0(%rsp)\n\t"
        // ... xmm7..xmm14 ...
        "movdqu %xmm15,144(%rsp)\n\t"
        "stmxcsr 160(%rsp)\n\t"
        "fnstcw   164(%rsp)\n\t"
        "movq %rsp, (%rcx)\n\t"   // Windows x64: 1-й аргумент в rcx
        "movq (%rdx), %rsp\n\t"   // 2-й в rdx
        "movdqu   0(%rsp), %xmm6\n\t"
        // ...
        "ldmxcsr 160(%rsp)\n\t"
        "fldcw    164(%rsp)\n\t"
        "addq $168, %rsp\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rsi\n\t"
        "popq %rdi\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "retq\n\t");
}
#endif
```

Начальный кадр корутины строится «снизу вверх»: `xmm6..15 | mxcsr | x87cw | pad |
r15 r14 r13 r12 rsi rdi rbp rbx | адрес трамплина`, причём `rbx = this`; `trampoline`
делаем `naked` и передаём `this` в `rcx` перед `call` во `extern "C"`-функцию входа.
Проверять обязательно тестом с броском/перехватом исключения внутри корутины.

---

## 13. Как не потерять кроссплатформенность

Замер текущего состояния (все `std/*/*.{h,cpp}` + `tst/*.cpp`):

| Метрика | Сейчас |
|---|---|
| Всего строк | 38 414 |
| Строк под платформенными `#if` (`__linux__`, `__APPLE__`, `__FreeBSD__`, `_WIN32`, …) | **865 (2,3%)** |
| Файлов с такими `#if` | **11** |
| Из них в `sys` / `thr` / `mem` / `dns` | 3 / 6 / 1 / 1 |
| В `alg`, `lib`, `str`, `sym`, `map`, `ptr`, `rng`, `typ`, `ios`, `net`, `dbg`, `tst` | **0** |

То есть «кроссплатформенность» здесь уже устроена как «один интерфейс + изолированные
бэкенды»: `poller.cpp` держит `epoll` и `kqueue`, `event_fd.cpp` — `eventfd` и пайп,
`context.cpp` — SysV и AAPCS. Windows добавляет третий бэкенд, а не развилку в логике.
Оценка после порта: **3,5–4,5% строк и 17–19 файлов**, все — в тех же каталогах
(`sys`, `thr`, `net`, `dns`, `mem`, `ios`) плюс новый `plat.*`.

Правила, которые это удерживают:

1. **`plat.h` — единственный заголовок с Win32-инклюдами.** В публичные заголовки он не
   попадает; `std/*/*.h` остаются платформенно-нейтральными.
2. **`plat.h` подставляет POSIX-поверхность**, а не «виндовый API»: `iovec`, `SHUT_*`,
   `MSG_NOSIGNAL`, `stdOpen`/`stdClose`/`stdRecv`. Тогда `net/`, `dns/`, `ios/` остаются
   без ветвлений (меняются только строки `#include` и пара вызовов), а ветвления
   сосредоточены в примитивах `plat.cpp` и в бэкендах `sys`/`thr`.
3. **Политика общая, примитив платформенный.** Циклы по `EAGAIN`, дедлайны, retry на
   `EFAULT`, буферизация — один экземпляр кода; платформенно-специфичен только syscall.
   Иначе две реализации `FD::read` разойдутся через полгода.
4. **Никаких платформенных решений в контракте.** «Поллер принимает только сокеты» —
   свойство Windows-бэкенда, а не `PollerIface` (иначе ломается Linux-пробуждение
   реактора и macOS, где `EventFD` — пайп). То же для `ENOTSUP`-фич: это документированное
   свойство платформы, а не новая семантика API.
5. **Позитивные правки вместо веток.** `alloca` → фиксированный буфер, `memmem` → своя
   реализация в общем коде, `sysconf(_SC_PAGESIZE)` → собственный хелпер. Часть работ
   по порту не добавляет `#ifdef`, а убирает платформенную связность.
6. **Линт на инвариант.** В `style.py` (или отдельным шагом CI): запрет платформенных
   макросов в защищённых каталогах (`std/alg`, `std/lib`, `std/str`, `std/sym`, `std/map`,
   `std/ptr`, `std/rng`, `std/typ`) и бюджет guarded-строк: превышение baseline
   (865 + согласованная дельта) — красный CI. Тогда «ifdef creep» ловится сразу, а не
   через год.
7. **Матрица CI на каждый PR:** Linux glibc, Linux musl, Windows UCRT64 (+ Windows CLANG64
   с санитайзерами, см. A6.1; + macOS, когда появится раннер). Без этого Linux-путь тихо сломается на первой же правке `fd.cpp` —
   это главный реальный риск, а не количество `#ifdef`.

Честная оговорка: часть различий убрать нельзя, потому что они — свойство ОС, а не нашего
кода: на Windows нельзя поллить пайп, нельзя сделать анонимный пайп non-blocking, нет
`SCM_RIGHTS`/`UDP_GRO`. Для Linux это ничего не меняет (там возможности остаются), для
Windows API документирует `ENOTSUP`. Сделать семантику *везде одинаковой* можно только
эмуляцией — это вариант B (MSYS2), и он выбран не был.
