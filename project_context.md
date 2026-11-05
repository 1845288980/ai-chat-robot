# Project Context

## Current user goal

Build the project step by step toward a local voice assistant: ESP32-S3 records audio, sends it to a computer on the same LAN, the computer performs speech recognition/LLM/TTS processing, and audio is returned to the ESP32 speaker. The current milestone is only reliable LAN communication.

## Hardware and network

- Target: ESP32-S3.
- OLED: 128x64 SSD1306 over hardware I2C; default SDA GPIO4, SCL GPIO5, address 0x3C.
- ESP32 Wi-Fi: fixed test SSID `Gang`, password is stored in the current source for this temporary experiment.
- PC IPv4: currently `192.168.1.142` (DHCP address; may change again).
- ESP32 connects to 2.4 GHz; PC may connect to 5 GHz. They must remain on the same LAN, without guest-network or AP isolation.
- TCP test port: `5000`.

## Project structure and implemented features

- `components/bsp/iic`: reusable SSD1306 driver and framebuffer text rendering.
- `components/bsp/wifi_scan`: Wi-Fi STA initialization/connection and the original nearby-AP scan/display code.
- `components/bsp/tcp_client`: ESP32 TCP client task for the current LAN test.
- `main/main.c`: intentionally small startup orchestration.
- `tools/tcp_server.py`: PC-side Python TCP ACK server.

## Current TCP protocol

- ESP32 currently connects to `192.168.1.142:5000`; this is a compile-time test setting and must be updated/reflashed if the PC DHCP address changes.
- ESP32 sends the UTF-8 line `hello from ESP32\n` every five seconds.
- PC replies with `ACK from PC\n`.
- Newline is the message boundary so multiple messages can share one TCP stream.
- ESP32 logs connection/send/receive states and shows the state on the OLED.
- ESP32 reconnects after connection failure or missing response.

## Change log

- 2026-11-01: Added fixed Wi-Fi STA connection using SSID `Gang`, limited retry handling, IP acquisition, and OLED connection/failure messages. Build passed with ESP-IDF v5.4.
- 2026-11-01: Added periodic Wi-Fi scan task and BSSID/channel diagnostics. Build passed.
- 2026-11-01: Added TCP client component, Python ACK server, and this context file. ESP-IDF build passed; `build/main.bin` generated with about 61% application partition free. Python `py_compile` passed and a local loopback test confirmed `hello from ESP32\n` receives `ACK from PC\n`.

## Verification and troubleshooting

1. Start the PC server from the project root: `python tools\\tcp_server.py`.
2. Allow Python through Windows Firewall on the Private network if prompted.
3. Flash/reset ESP32 and watch serial output plus OLED.
4. Expected PC output includes `TCP server listening on 0.0.0.0:5000`, `received: hello from ESP32`, and `sent: ACK from PC`.
5. Expected OLED state after the exchange: `TCP connected` / `ACK received`.
6. If connection fails, verify PC IP, server startup, port availability, firewall, same LAN, no guest network, and no AP isolation.

### 2026-11-01 TCP failure investigation

- User started `python tools\\tcp_server.py`; server printed `TCP server listening on 0.0.0.0:5000`.
- PC checks: WLAN IPv4 is still `192.168.1.19`; `netstat` shows `0.0.0.0:5000 LISTENING`; `Test-NetConnection 192.168.1.19 -Port 5000` succeeded locally.
- ESP32 OLED remains at TCP failure/retry state. This proves only the PC-side listener is healthy; it does not yet prove ESP32-to-PC reachability.
- Next evidence required: ESP32 serial `connect() failed: errno=...`, ESP32 OLED-assigned IP, whether PC server prints `client connected`, and a PC `ping <ESP32-IP>` result.
- Follow-up evidence: ESP32 got IP `192.168.1.149`; PC pinged it successfully with 0% loss and ARP resolved its MAC. PC WLAN IPv4 remains `192.168.1.19`, and TCP `0.0.0.0:5000` is listening. Windows WLAN network category is `Public`, with firewall enabled; no explicit port-5000 allow rule was found. `errno=113` is therefore most likely caused by Windows Public-profile inbound filtering (or a similar host firewall rule), not Wi-Fi association.
- 2026-11-02 follow-up: PC WLAN changed to IPv4 `192.168.1.142` and is now a Private network. TCP `0.0.0.0:5000` is listening, and `Test-NetConnection 192.168.1.142 -Port 5000` succeeds locally. The firmware still targeted stale `192.168.1.19`, explaining the ESP32 failure. Updated the TCP client target to `192.168.1.142`; the ESP-IDF build then passed and generated a new `build/main.bin` with about 61% app partition free. The board still needs this new image flashed.
- 2026-11-02 latest report: User flashed the updated image; OLED now shows `TCP connecting` for longer, then `TCP connect fail / retry in 5 seconds`. Current source target is confirmed as `192.168.1.142:5000`; PC WLAN profile is Private, and netstat shows TCP `0.0.0.0:5000 LISTENING`. The longer connecting screen is consistent with the blocking TCP `connect()` waiting for a timeout. Need fresh UART `connect() failed: errno=...` and whether Python logs `client connected` to distinguish firewall/drop from post-connect receive failure. An explicit Private-profile inbound allow rule for TCP/5000 may still be needed.

- 2026-11-02: After the Private-profile TCP/5000 firewall rule was added, the ESP32 connected and exchanged several messages successfully. When the ESP32 was reset while the Python server remained running, it did not reconnect until the Python server was restarted. The original server handled one client inside `serve_client()` before calling `accept()` again, so a stale connection could temporarily prevent it from accepting the ESP32's new connection. This was a server limitation, not expected TCP behavior and not a need to restart the server.
- 2026-11-02: Updated `tools/tcp_server.py` to handle each client in a separate daemon thread, increased the listen backlog, and added a 30-second idle timeout plus clean handling of reset/broken-pipe connections. The ESP32 already retries every five seconds, so with this server it should reconnect automatically after a reset as long as Wi-Fi is back and the PC address/firewall rule are still valid. Verified the Python file with `python -m py_compile tools\\tcp_server.py`.
- 2026-11-02: Local simulation verification passed on temporary port 5017: one client remained connected while a second client connected and received `ACK from PC`, proving the new server no longer waits for the first client before accepting another. Actual ESP32 reset/reconnect still needs to be verified on the hardware.
- 2026-11-02: Hardware verification passed after keeping the updated Python server running and resetting the ESP32 three times. The server reported three different client source ports (`55780`, `51096`, `55364`), and each new connection received `hello from ESP32` and `ACK from PC` without restarting the server. Later `client idle timeout` messages were expected cleanup of older reset connections: the old TCP sockets had stopped sending, so the server removed them after 30 seconds while the newest connection continued normally.
- 2026-11-02: Conceptual networking note for future conversations: the PC and ESP32 each receive a LAN/private IP from the router. In the current design, the ESP32 is the TCP client and actively connects to the PC server at the hardcoded `192.168.1.142:5000`; therefore the PC IP must remain stable (or the firmware target must be changed and reflashed), while the ESP32 IP may change because the ESP32 initiates the connection and the PC replies through that established TCP connection. The server listens on `0.0.0.0:5000`, meaning all PC network interfaces on port 5000. TCP first performs a connection handshake, then carries an ordered byte stream. The ESP32 sends `hello from ESP32\\n`, the newline marks a complete test message, and the PC sends `ACK from PC\\n` back on the same connection. The ESP32 IP is needed if the PC must initiate a new connection to the ESP32, or for ping/debugging. The server log source address such as `192.168.1.19:55780` is the ESP32's address and temporary client port, not the PC's address.
- 2026-11-02: Project-log maintenance rule: for future work on this project, record important code changes, troubleshooting evidence, user-reported problems, diagnoses, solutions, verification results, and current next steps in this file. This rule itself is recorded here so a future conversation can read this file and continue the project with its history.
- 2026-11-02: Audio hardware has not yet been purchased. Recommended value/simplicity-first hardware for the ESP32-S3-N16R8 minimum system board: one `INMP441` I2S digital microphone module; one `MAX98357A` I2S mono Class-D amplifier module; and one 4-ohm, 3-watt small speaker (approximately 40 mm is a practical size). This uses digital I2S for both capture and playback, has abundant ESP-IDF examples/community references, needs no external ADC/DAC, and is well suited to the planned PC-based ASR/LLM/TTS voice-assistant workflow. Avoid an analog microphone module or a bare speaker as the initial choice: the ESP32-S3 has no built-in DAC and those choices add analog/ADC/DAC or amplifier complexity. The MAX98357A should be powered from the board's 5 V USB/VIN pin for useful volume (while its I2S logic signals are 3.3 V); use short Dupont wires and do not connect the speaker directly to an ESP32 GPIO.
- 2026-11-02: I2S explanation: I2S and I2C are both digital chip-to-chip interfaces, but I2S is designed for continuous digital audio and I2C is a slower addressed control/configuration bus. In this project the audio path is `INMP441 microphone -> I2S -> ESP32 RAM`; then ESP32 packages PCM samples and sends them over Wi-Fi/TCP to the PC. The microphone does not send I2S directly to the PC. The PC receives PCM bytes, adds a WAV header (sample rate, bit depth, channel count, data length), and saves or streams a `.wav` file for ASR. For playback the reverse path is `PC TTS audio -> Wi-Fi/TCP -> ESP32 RAM -> I2S -> MAX98357A -> speaker`. Initial audio should be mono PCM with a fixed sample rate (for example 16 kHz, 16-bit) to keep framing and code simple.
- 2026-11-02: Interaction design decision: use Push-to-Talk for the first voice version. Press and hold a physical button to begin recording; keep holding while I2S captures PCM; release to stop recording and trigger upload/ASR. This is preferred over keyword wake-up for the first implementation because it avoids continuous microphone processing and wake-word model tuning. Required implementation concepts are GPIO edge handling, button debounce, a recording state machine (`IDLE -> RECORDING -> UPLOADING -> WAITING_REPLY -> PLAYING -> IDLE`), a maximum recording duration, audio buffering/backpressure, and a binary TCP audio framing protocol. The existing GPIO2 EXTI sample can provide the starting point for button events, but GPIO2 is currently only sample code and `exti_init()` is not called by `main.c`; the final button GPIO must be checked against the actual board pinout before wiring.
- 2026-11-02: Keyword-trigger alternative note: a wake-word design requires the microphone and I2S capture to run continuously, a ring buffer, voice activity detection (VAD), a wake-word detector/model (for example ESP-SR where supported), model memory/flash considerations, false-trigger handling, and usually an end-of-speech/VAD rule after wake-up. It is a later phase after Push-to-Talk recording and network audio transfer are reliable.
- 2026-11-02: Desired future feature: GPIO buttons control final speaker volume. Recommended design is two momentary buttons (`volume down` and `volume up`) with internal pull-ups and active-low wiring. Button events change a stored software volume level (for example 0%, 10%, ... 100%); during playback the ESP32 scales the received signed PCM samples by that level before writing them through I2S to the MAX98357A. This gives fine-grained control without extra analog parts. The MAX98357A `GAIN` pin only selects a fixed hardware gain and is not suitable for normal user volume adjustment. Add debounce, clamp the range to prevent overflow/clipping, show the level on the OLED, and later save the selected level in NVS so it survives reset. Confirm final available GPIO pins from the actual board pinout before deciding button wiring.

## Next work

- Start/restart `python tools\tcp_server.py`, flash the new firmware, and verify GPIO17 Push-to-Talk plus WAV saving on hardware.
- Confirm TCP server address is current before flashing; current working-tree firmware target is `192.168.1.112:5000`.
- Check recording duration and sound quality in generated WAVs, including the 10-second cap and failure feedback with the PC server unavailable.
- After reliable WAV transfer is confirmed, add ASR, then LLM/TTS and speaker playback.

### 2026-11-04 INMP441 microphone test

- User purchased and wired an INMP441 module to the ESP32-S3: VDD to 3V3, GND to GND, SCK/BCLK to GPIO6, WS to GPIO7, SD to GPIO15, and L/R to GND (left channel). OLED remains on GPIO4/5.
- Added the `components/bsp/inmp441` component using the ESP-IDF 5.4 standard I2S RX driver. It captures 16 kHz, 32-bit stereo slots and periodically logs average absolute level and peak from the left channel after shifting the 24-bit microphone samples into a convenient range.
- `main.c` starts this microphone diagnostic task immediately after OLED initialization and before Wi-Fi, so mic testing does not depend on LAN availability. Verify by monitoring UART logs for `inmp441: audio level`; speak or tap near the microphone and confirm avg_abs/peak rise over quiet readings. This is a signal-capture diagnostic only; audio upload and push-to-talk are not implemented yet.
- Hardware/UART verification reported by user: baseline quiet readings were around `avg_abs=53,000-57,000`, `peak=69,000-135,000`; during louder activity readings rose substantially (for example `avg_abs=365,301`, `peak=1,181,248`) and later fell back toward baseline. This confirms the mic signal responds to sound. These are raw PCM amplitude statistics, not calibrated dB values. `avg_abs` is the mean absolute left-channel sample over the approximately 2-second reporting window; `peak` is the maximum absolute sample in that window. Sample rate is 16 kHz (16,000 samples/second), distinct from the 2-second log period.
- In the same run the ESP32 connected to the TCP server at `192.168.1.22:5000`, sent `hello from ESP32`, and received `ACK from PC`, so the text TCP path was working at that time.
- Current hotspot migration instructions: connect both PC and ESP32 to the same phone hotspot. Update `WIFI_TEST_SSID` and `WIFI_TEST_PASSWORD` in `components/bsp/wifi_scan/wifi_scan.c`; find the PC's new hotspot-side IPv4 with `ipconfig` and update `TCP_SERVER_IP` in `components/bsp/tcp_client/tcp_client.c`. Rebuild and reflash after firmware changes. Keep TCP port 5000 and Python server `HOST = "0.0.0.0"` / `PORT = 5000` in `tools/tcp_server.py` unless there is a specific conflict. Ensure phone hotspot client isolation is disabled/supported and Windows Firewall allows TCP/5000 on the active profile. ESP32's own DHCP address is not hardcoded.

### 2026-11-05 Push-to-Talk PCM/WAV implementation

- Added active-low Push-to-Talk on GPIO17 with internal pull-up and 30 ms debounce. Hold to capture INMP441 left-channel audio at 16 kHz, convert to mono signed 16-bit PCM, and upload on release. Recording is capped at 10 seconds (320,000 PCM bytes) in PSRAM; reaching the cap auto-submits.
- OLED TCP feedback remains on rows 1-2. Audio state uses row 4 only: `FREE`, `RECORDING`, `REC DONE`, `UPLOADING`, `SAVED`/`UPLOAD FAIL`, then returns to `FREE`. Shared row updates preserve the other display rows.
- TCP retains the existing newline text ACK heartbeat and supports a `PCM1` 16-byte network-order header (sample rate, channels, bits/sample, payload length) followed by little-endian PCM bytes. The PC server validates the frame, writes `recordings/recording_*.wav`, and replies `SAVED <filename>`.
- ESP-IDF 5.4 build passed after implementation; `python -m py_compile tools\tcp_server.py tools\test_tcp_server.py` passed. The local loopback regression test verified text ACK followed by a PCM upload on the same connection and checked the resulting 0.5-second WAV format. Hardware flash, button wiring, WAV playback/duration, and disconnected-server behavior remain to be verified.
- 2026-11-05 follow-up: User tested GPIO16 by shorting it to GND, but OLED stayed at `FREE`. Changed the Push-to-Talk input to GPIO17 for the next hardware test; firmware must be rebuilt and reflashed.
- 2026-11-05 audio-transfer verification: GPIO17 Push-to-Talk and WAV saving worked, and WAV duration matched button hold time, proving capture control, TCP framing, and WAV persistence. However, analysis of a recorded WAV found `avg_abs=0` and `peak=0`: all samples were zero. Root cause was the recording conversion using `raw_samples[i] >> 16`, which discarded the INMP441's valid 24-bit data on this board. Changed it to the previously validated `raw_samples[i] >> 8` alignment with signed 16-bit saturation, and added per-recording `avg_abs`/`peak` UART logging. ESP-IDF build passed; the corrected hardware recording still needs to be flashed and played back.
- 2026-11-05 follow-up: After flashing the `>> 8` conversion, hardware recordings still reported `avg_abs=0`, `peak=0`, and WAV analysis confirmed all-zero PCM. Added a diagnostic/selection pass that measures both I2S stereo slots during each recording, selects the slot with greater absolute signal for WAV output, and logs raw left/right averages. This determines whether L/R slot selection or lack of microphone I2S data is the remaining cause. ESP-IDF build passed; await the new `raw L/R avg` hardware log.
- 2026-11-05 audio quality follow-up: User reported harsh clipping. UART from the latest capture showed `output avg=31879`, `peak=32768`, `raw L/R avg=108221/0`; inspection of its WAV confirmed 94.5% of samples were at signed-16-bit full scale. The active microphone is the left slot, while selecting the largest slot on every sample is unnecessary. Changed capture to use the left I2S slot consistently and shift its left-aligned 24-bit sample by 16 bits into signed 16-bit PCM, avoiding the severe clipping caused by `>> 8`. ESP-IDF 5.4 build passed and generated `build/main.bin`; flash and hardware re-test are pending.
