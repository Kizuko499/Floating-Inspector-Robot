# FIR — Floating Inspector Robot

**Stasiun pemantau kualitas air perairan pelabuhan terapung berbiaya rendah** — satu platform, dua misi: memantau kualitas air (suhu, kekeruhan, TDS) dan menginspeksi infrastruktur dermaga di zona garis air.

Karya pribadi — dirancang, dirakit, dan diprogram sendiri dari nol, lalu dibawa ke **final KSTI 2026 kategori IoT** (ITS Surabaya, November 2026), subtema *Smart Home & Environment* — SDG 6 (air bersih), SDG 14 (ekosistem laut), SDG 9 (infrastruktur tangguh).

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-green)
![Android](https://img.shields.io/badge/app-Android%20(Java)-brightgreen)
![License](https://img.shields.io/badge/license-MIT-yellow)

---

## Dua Misi

1. **Pemantauan kualitas air** — suhu air (DS18B20), kekeruhan (turbidity SEN-0175), dan TDS dibaca berkala, dihitung menjadi indeks kualitas air (WQI), dan ditampilkan real-time di dashboard web robot. Ambang bahaya: suhu ≥ 35 °C atau ≤ 15 °C, TDS ≥ 500 ppm, kekeruhan di atas ambang "keruh" — alarm aktif setelah 3 bacaan berturut-turut (debounce) berupa banner + buzzer.
2. **Inspeksi visual zona garis air** — kamera OV5640 mengambil foto lambung/dinding dermaga; model AI Edge Impulse di dalam robot menandai **kandidat area korosi** (skor ≥ 0,70) yang **tetap perlu verifikasi operator** — robot tidak mengklaim "mendeteksi karat".

## Fitur Firmware (v7.10.2 — versi final KSTI)

- Access point WiFi sendiri (`FIR-Robot`) + dashboard web di `192.168.4.1`, tanpa internet
- Telemetri suhu, kekeruhan, TDS, level air, dan hujan (LM393) dengan klasifikasi jernih/sedang/keruh
- Kendali motor terapung 2 kanal (DRV8833) dari dashboard
- Foto kamera + riwayat foto tersimpan di kartu SD / LittleFS, log data CSV
- Halaman admin tersembunyi `/adminin` untuk override label klasifikasi kekeruhan
- Fail-safe: kegagalan mount SD tidak menyandera boot (maks. 3 percobaan + cooldown)

## Komponen Utama

| Komponen | Fungsi |
|---|---|
| ESP32-S3 CAM (N16R8) | Otak robot + kamera |
| Kamera OV5640 | Inspeksi visual |
| DS18B20 | Suhu air |
| Turbidity SEN-0175 | Kekeruhan |
| Sensor TDS (kompatibel Gravity) | Padatan terlarut |
| DRV8833 | Driver motor DC 2 kanal |
| MT3608 (step-up) + TP4056 | Daya: LiPo 1S 3,7 V → 5 V + pengisian |
| LiPo 1S 500 mAh | Baterai |
| Buzzer, sensor level air, LM393 hujan | Alarm & sensor pendukung |

Diagram lengkap ada di folder [`docs/`](docs/) — `Wiring_FIR_Terkini_10Nov2026.pdf` (wiring as-built) dan `Diagram_Wiring_Per_Komponen_FIR_Terkini.pdf` (per komponen).

## Struktur Repo

```
Floating-Inspector-Robot/
├── firmware/        # Proyek PlatformIO — firmware robot v7.10.2
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── lib/         # OneWire, DallasTemperature, model AI Edge Impulse
├── android-app/     # Source aplikasi pendamping Android (Java)
└── docs/            # Diagram wiring (PDF)
```

## Build Firmware

Butuh [PlatformIO](https://platformio.org/):

```bash
cd firmware
pio run              # kompilasi (env: fir-esp32s3cam)
pio run -t upload    # flash ke board via USB (saklar baterai OFF)
```

**Catatan kredensial:** salinan publik ini mengganti kredensial WiFi/AP di `firmware/src/main.cpp` (sekitar baris 1372–1379) dengan placeholder — isi `WIFI_SSID`, `WIFI_PASS`, dan `AP_PASS` dengan milikmu sebelum flash.

## Build Aplikasi Android

Buka folder `android-app/` di Android Studio, atau dari terminal:

```bash
cd android-app
./gradlew assembleDebug
```

Aplikasi terhubung ke robot lewat WiFi (mode AP: `192.168.4.1`).

## Lisensi

MIT — lihat [LICENSE](LICENSE). Library pihak ketiga di `firmware/lib/` (OneWire, DallasTemperature, Edge Impulse SDK) mengikuti lisensi masing-masing.
