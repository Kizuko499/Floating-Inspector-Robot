# FIR Robot — Aplikasi Android Pendamping

Aplikasi pendamping robot FIR (ESP32-S3 CAM) untuk Android.
Paket: `id.fir.robot` • minSdk 26 • target/compileSdk 34 • Java murni
(tanpa AndroidX, tanpa dependensi eksternal).

## Transportasi

### WiFi (HTTP ke firmware, alamat bawaan AP robot `192.168.4.1`)
| Endpoint | Fungsi |
|---|---|
| `GET /telemetry` | JSON telemetri lengkap (sensor, WQI, alert, field `ai_*`) |
| `GET /capture` | Satu frame JPEG kamera |
| `GET /motor?cmd=F\|B\|L\|R\|S&speed=<0-100 persen>` | Kendali motor |
| `GET /analyze` | Analisis AI sekali; balasan `{"ok":true, ai_*...}` |
| `GET /ai-auto?on=1\|0` | Saklar mode otomatis AI |

### Bluetooth Low Energy (cadangan saat WiFi putus; butuh firmware v7.0)
- Nama perangkat: `FIR-Robot`
- Service: `12345678-1234-5678-1234-56789abcdef0`
- Telemetri `...abcdef1` (READ+NOTIFY, JSON sama seperti `/telemetry`;
  aplikasi meminta MTU 247 dan membaca ulang penuh bila string terpotong)
- Perintah `...abcdef2` (WRITE teks): `M:F`/`M:B`/`M:L`/`M:R`/`M:S`
  (opsional `:<0-255>`), `ANALYZE`, `AIAUTO:0`/`AIAUTO:1`
- Hasil AI `...abcdef3` (READ+NOTIFY):
  `<label>:<skor>:<KANDIDAT|AMAN|GAGAL>:<ms>` atau `BELUM ADA HASIL`

Kamera hanya lewat WiFi. Aplikasi mencoba WiFi dulu, turun ke BLE bila
WiFi putus, dan kembali ke WiFi secara otomatis saat WiFi pulih.
Indikator di bilah atas selalu menunjukkan transportasi aktif:
"Terhubung: WiFi — <ip>" / "Terhubung: Bluetooth" / "Tidak terhubung".

## Build

- Jalur normal: `./gradlew assembleDebug`
  (Gradle 8.5, AGP 8.2.2, JDK 17, Android SDK 34).
- Di VM pengembangan ini unduhan dependensi Java terblokir proksi egress,
  sehingga APK debug dibangun dengan rantai SDK langsung
  (aapt2 → javac → d8 → zipalign → apksigner, keystore debug standar)
  lewat `./build_manual.sh`. Hasilnya setara:
  `app/build/outputs/apk/debug/app-debug.apk`.
