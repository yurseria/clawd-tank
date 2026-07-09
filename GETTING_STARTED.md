# Getting Started — WT32-SC01 Plus (ESP32-S3)

Clawd Tank를 WT32-SC01 Plus 보드에서 실행하기 위한 가이드입니다. 보드 수령부터 Claude Code 연동까지 전체 과정을 다룹니다.

## 준비물

- [ ] WT32-SC01 Plus 보드 (ESP32-S3-WROOM-1, 3.5" 480×320 LCD, FT6336 터치)
- [ ] USB-C 케이블 (데이터 전송 가능한 것 — 충전 전용 ❌)
- [ ] Mac

---

## 1. ESP-IDF 개발 환경

이 프로젝트는 ESP-IDF 5.3.2를 사용합니다.

> **중요**: `idf.py`는 디렉토리에 있는 파일이 아닙니다. ESP-IDF 환경이 활성화되어야 PATH에 잡히는 명령어입니다. 아래 단계를 따라 환경을 먼저 설정하세요.

### 1-1. ESP-IDF 설치 (최초 1회)

```bash
cd clawd-tank
./setup_env.sh        # direnv, ESP-IDF 5.3.2 설치 (esp32c6 타깃만 포함)
```

`setup_env.sh`는 ESP-IDF를 `bsp/esp-idf/`에 설치합니다. 이 디렉토리는 `.gitignore`에 있어 저장소에 포함되지 않으므로, 새 환경에서는 반드시 이 스크립트를 먼저 실행해야 합니다.

### 1-2. ESP32-S3 툴체인 추가 설치

`setup_env.sh`는 ESP32-C6(RISC-V) 툴체인만 설치합니다. WT32-SC01 Plus의 ESP32-S3는 Xtensa 아키텍처이므로 별도 설치가 필요합니다.

```bash
cd clawd-tank                            # 프로젝트 루트에서 실행
export IDF_TOOLS_PATH="$PWD/.espressif"
source bsp/esp-idf/export.sh             # 환경 활성화
$IDF_PATH/install.sh esp32s3             # S3 툴체인 설치
```

### 1-3. 환경 활성화 (매 터미널 세션마다)

이후 모든 `idf.py` 명령은 ESP-IDF 환경을 먼저 활성화해야 합니다. **새 터미널을 열 때마다 아래 명령을 실행하세요.**

```bash
cd clawd-tank                            # 프로젝트 루트로 이동
export IDF_TOOLS_PATH="$PWD/.espressif"
source bsp/esp-idf/export.sh
```

활성화가 성공했는지 확인:

```bash
which idf.py                             # 경로가 출력되어야 함 (예: .../bsp/esp-idf/tools/idf.py)
```

> **direnv 사용 시**: `direnv allow`를 한 번 실행하면 이후 터미널에서 `firmware/` 디렉토리에 들어갈 때 자동으로 환경이 활성화됩니다. 단, direnv 훅이 쉘에 설정되어 있어야 합니다 (`eval "$(direnv hook zsh)"` in `~/.zshrc`).

### 1-4. 타깃 설정

```bash
cd firmware
idf.py set-target esp32s3
```

> 이 과정에서 `esp_lcd_st7796`(1.4.0), `lvgl`(9.5.0), `led_strip`(2.5.5) 컴포넌트가 자동으로 다운로드됩니다.

---

## 2. 펌웨어 빌드

```bash
cd firmware
idf.py build
```

빌드가 성공하면 다음과 같은 메시지가 표시됩니다.

```
clawd-tank.bin binary size 0x1f91e0 bytes. Smallest app partition is 0xff0000 bytes. 0xdf6e20 bytes (88%) free.

Project build complete. To flash, run:
 idf.py flash
```

### 빌드 에러 대응

| 에러 | 원인 | 해결 |
|------|------|------|
| `esp_lcd_st7796.h not found` | 컴포넌트 미다운로드 | `idf.py reconfigure` |
| `esp_lcd_new_i80_bus` 미정의 | ESP-IDF 버전 | ESP-IDF 5.3+ 확인 |
| PSRAM 관련 경고 | sdkconfig 충돌 | `idf.py fullclean && idf.py build` |

---

## 3. 보드 연결 및 플래시

### USB 포트 확인

```bash
ls /dev/cu.usb* /dev/tty.usb*
# 보통 /dev/cu.usbmodem* 또는 /dev/cu.usbserial-* 형태
```

### 플래시 + 모니터링

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

`monitor`를 붙이면 플래시 후 시리얼 모니터가 열려 부팅 로그를 확인할 수 있습니다.

### 정상 부팅 로그

```
I (xxx) clawd-tank: Clawd Tank starting...
I (xxx) display: Initializing display (WT32-SC01 Plus, ST7796 parallel)...
I (xxx) display: Display initialized: 480x320 landscape (parallel I80)
I (xxx) ble: BLE service initialized
I (xxx) clawd-tank: Clawd Tank running — free heap: xxx (internal: xxx, PSRAM: 8388608)
```

### 화면 표시 문제 대응

화면 관련 문제는 하드웨어 이식 단계에서 가장 자주 발생합니다.

| 증상 | 해결 |
|------|------|
| 화면이 하얗게 나옴 | `esp_lcd_panel_init` 후 ST7796 추가 초기화 명령 필요 가능성 |
| 화면이 뒤집힘 | `firmware/main/display.c`의 `swap_xy`/`mirror` 조합 조정 |
| 화면 노이즈 | `LCD_PCLK_HZ`를 16MHz에서 12MHz로 낮춤 |
| 백라이트 안 켜짐 | GPIO45 백라이트 PWM 핀 설정 확인 |

---

## 4. host daemon 실행 (BLE 연동)

### Python 의존성 설치

```bash
cd host
pip install -r requirements.txt
# bleak (BLE), rumps (메뉴바 앱) 설치
```

### daemon 실행

```bash
cd host
python -m clawd_tank_daemon
```

daemon이 자동으로 다음을 수행합니다.

1. **BLE 스캔** — `"Clawd Tank"` 장치 검색
2. **연결** — 서비스 UUID `aecbefd9-98a2-4773-9fed-bb2166daa49a`로 GATT 연결
3. **상태 동기화** — 시간, 세션 상태, 알림 전송

### 연결 확인

daemon 로그에 다음이 나타나면 연결 성공입니다.

```
INFO clawd-tank.ble: Scanning for Clawd Tank device...
INFO clawd-tank.ble: Found Clawd Tank: Clawd Tank (xx:xx:xx:xx)
INFO clawd-tank.ble: Connected to Clawd Tank (MTU: 256)
INFO clawd-tank.daemon: Transport 'ble' connected
```

이 시점에서 보드 화면에 Clawd 게가 idle 애니메이션으로 표시되어야 합니다.

---

## 5. Claude Code 연동

### hooks 설치

```bash
cd host
./install-hooks.sh
```

이 스크립트는 `~/.claude/settings.json`에 hooks를 추가합니다.

- `UserPromptSubmit` — 프롬프트 입력 시
- `Notification` — 권한 요청, idle 등
- `SessionEnd` — 세션 종료 시

### 동작 흐름

```
Claude Code hook → clawd-tank-notify → daemon → BLE → 보드 화면 애니메이션
```

코딩 중이면 Clawd가 `typing`/`building` 애니메이션을, 권한 요청이 오면 알림 카드와 LED 플래시를, 대기 중이면 `idle`, 자리 비움이면 `sleeping` 애니메이션을 표시합니다.

---

## 6. 메뉴바 앱 (선택)

daemon과 시뮬레이터를 통합 관리하는 macOS 메뉴바 앱을 사용할 수 있습니다.

```bash
cd host
python -m clawd_tank_menubar
```

메뉴바 아이콘에서 다음을 관리할 수 있습니다.

- **BLE 전송** — 하드웨어(보드) on/off
- **Simulator 전송** — 소프트웨어 창 on/off
- 밝기 및 슬립 타임아웃 설정

---

## 트러블슈팅

| 증상 | 확인할 것 |
|------|----------|
| `idf.py` 명령 없음 | `direnv allow` 또는 `source bsp/esp-idf/export.sh` |
| 빌드 에러 (C6 잔재) | `idf.py fullclean` 후 `idf.py set-target esp32-s3` 재실행 |
| 플래시 안 됨 | USB-C 케이블이 데이터 전송용인지 확인 |
| BLE 스캔 안 됨 | macOS BLE 권한, 보드가 광고 모드인지 시리얼 모니터 로그 확인 |
| BLE 연결은 되는데 화면 변화 없음 | daemon 로그에서 `write failed` 확인, MTU 설정 점검 |
| 화면 깨짐/노이즈 | `LCD_PCLK_HZ` 낮추기, `swap_xy`/`mirror` 조합 실험 |
