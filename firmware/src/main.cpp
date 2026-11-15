/*
 * ============================================================================
 *  FIR - Floating Inspector Robot  |  Firmware v7.10.2 (ESP32-S3 CAM)
 *  VERSI PLATFORMIO (VSCode) — file: src/main.cpp | Konfigurasi: platformio.ini
 *  Subtema: Smart Home & Environment (KSTI 2026 - IoT)
 * ============================================================================
 *  v5 = v4 + WQI (Water Quality Index 0-100: skor tunggal kualitas air).
 *  v4 = v3 + EARLY WARNING SYSTEM (peringatan dini pencemaran).
 *  Inilah yang mengubah FIR dari "alat ukur" menjadi "sistem cerdas":
 *  ambang batas -> debounce -> banner merah di dashboard + buzzer opsional
 *  + flag di JSON telemetri. Skenario demo: tuang air keruh ke bak, lihat
 *  tegangan kekeruhan anjlok dan banner PERINGATAN menyala live di depan juri.
 *
 *  AMBANG DEFAULT (kalibrasi ulang dengan baseline lokasi!):
 *    Kekeruhan : level "keruh" (< TURB_SEDANG_MIN_V) - tegangan pin A TURUN saat air makin keruh
 *    TDS       : >= 500 ppm (baku mutu air minum Indonesia, Permenkes)
 *    Suhu      : > 35 C atau < 15 C
 *  WQI v5: tiga parameter di atas digabung jadi skor 0-100 (bobot setara).
 *    Suhu optimum 28 C (tropis), kekeruhan: pin A 4,2 V = jernih (skor 100),
 *    TDS 0 ppm = 100. Kategori: >=80 BAIK, 50-79 SEDANG, <50 BURUK.
 *    Formula & asumsi WAJIB dikalibrasi/dijustifikasi ulang dengan data
 *    lokasi uji sebelum masuk paper (lihat fir_dokumen/catatan_WQI.md).
 *  Alert aktif setelah 3x pembacaan berturut-turut (debounce),
 *  clear setelah 3x pembacaan normal berturut-turut.
 *
 *  BUZZER (opsional): modul buzzer AKTIF 3,3V/5V, pin sinyal -> GPIO21,
 *  GND -> GND. Set BUZZER_PIN = -1 untuk menonaktifkan (tanpa buzzer pun
 *  early warning tetap jalan via banner dashboard).
 *
 *  Peta pin: DS18B20->GPIO14, Turbidity->GPIO1 (ADC1, via divider),
 *            TDS->GPIO2 (ADC1, langsung), Leak->GPIO3 (ADC1, analog),
 *            Buzzer->GPIO21, MPU6050->SDA GPIO47 / SCL GPIO48 (I2C),
 *            DRV8833->IN1..IN4 = GPIO38/39/40/41 (EEP -> GPIO45, v6.9.1).
 *  v6 = v5 + patch sensor level air analog (GPIO3) + MPU6050 (roll/pitch,
 *       tampil + log) + kendali motor DRV8833 lewat dashboard (watchdog
 *       berhenti otomatis) + dashboard web gaya RuteAman (terang, oranye-teal).
 *  v6.1 = v6 + sensor hujan digital (DO modul LM393) di GPIO42.
 *  v6.2 = v6.1 + peta kabel probe turbidity ditulis di kode (sisi angka
 *       adaptor TS-300B: 1=KUNING sinyal, 2=BIRU GND, 3=MERAH VCC, 4=kosong)
 *       dan ARAH bacaan kekeruhan DIBALIK sesuai karakteristik modul:
 *       tegangan pin A TURUN saat air makin keruh (di v6.1 arahnya terbalik).
 *  v6.3 = v6.2 + PENGAMAN MOTOR & WIFI: pin motor dikunci LOW sejak awal
 *       setup (sebelum kamera/WiFi) agar dinamo tidak jalan sendiri saat
 *       board booting atau firmware terhenti; kamera gagal init TIDAK lagi
 *       mematikan firmware (WiFi/dashboard/kendali motor tetap nyala, hanya
 *       stream yang mati); tombol dashboard jadi TEKAN-TAHAN - motor hanya
 *       bergerak selama tombol web ditekan, lepas = STOP; watchdog 1,5 dtk.
 *  v6.4 = v6.3 + perbaikan loop stream: buzzer & watchdog motor tetap
 *       diservis dari dalam handleStream selama stream kamera ditonton
 *       (di v6.3 loop utama macet di stream -> buzzer bisa membeku menyala
 *       nonstop bila dashboard dibuka saat ALERT sedang aktif).
 *  v6.5 = v6.4 + pola buzzer diganti: cukup 3 bip pendek saat ALERT
 *       pertama kali aktif, lalu diam selama ALERT masih menyala
 *       (banner dashboard tetap tampil). Tidak lagi bunyi terus-menerus.
 *  v6.6 = v6.5 + tampilan kamera dashboard diganti mode FOTO BERKALA:
 *       endpoint baru /capture (satu JPEG per permintaan), dashboard
 *       memuat ulang foto tiap +-1,2 detik. Stream MJPEG di dalam <img>
 *       terbukti tidak tampil di Chrome Android dan menahan server
 *       tunggal; /stream tetap ada untuk pemakaian langsung.
 *  v6.7 = v6.6 + kalibrasi arah motor (MOTOR_L_INVERT / MOTOR_R_INVERT /
 *       MOTOR_SWAP_LR / MOTOR_L_TRIM / MOTOR_R_TRIM, diterapkan terpusat
 *       di setMotors) + blok WiFi berkas ini sudah terisi hotspot user
 *       (bukan placeholder) atas permintaan user 2 Nov.
 *  v6.8 = v6.7 + DETEKSI KANDIDAT KOROSI berbasis AI (Edge Impulse,
 *       MobileNetV2 int8 96x96; 4 kelas: karat / cat_coklat / logam_bersih /
 *       lumut_kerak). Inferensi HANYA jalan saat tombol Analisis ditekan
 *       di dashboard (endpoint /analyze) - tidak terus-menerus, supaya
 *       telemetri & stream tidak terganggu. Hasil hanya disebut
 *       "kandidat korosi" bila skor kelas karat >= 0,70 dan SELALU wajib
 *       verifikasi operator (bukan vonis karat). Setiap analisis dicatat
 *       ke riwayat persisten /aidet.csv di LittleFS (bertahan melewati
 *       robot mati, dengan nomor sesi boot) dan bisa diunduh dari
 *       dashboard (/download-ai).
 *  v6.8.1 = perbaikan dari uji lapangan v6.8 (2 Nov sore): (a) alokasi
 *       buffer AI diganti ps_malloc + fallback berlapis + diagnostik
 *       PSRAM lengkap di Serial boot - di board user v6.8 gagal total
 *       di gerbang ini ("[AI] buffer tidak siap" di Serial, inferensi
 *       tidak pernah jalan; terbukti BUKAN panic/reboot); (b) grafik
 *       tren dashboard hanya menampilkan sesi boot yang sedang berjalan
 *       (record warisan sesi lama membuat garis tren kusut).
 *  v6.9 = v6.8.1 + MODE OTOMATIS (computer vision) sesuai permintaan
 *       user 2 Nov: saklar on/off di dashboard (persisten di
 *       /aiset.txt) - bila AKTIF, robot menganalisis frame kamera
 *       sendiri tiap 5 detik; bila terdeteksi kandidat korosi, frame
 *       yang dianalisis disimpan sebagai foto bukti ke SD card
 *       (folder /fir/foto, maks 1 foto per menit) + tetap tercatat di
 *       /aidet.csv. Dukungan SD card DITULIS LENGKAP tetapi
 *       DINONAKTIFKAN (SD_PINS_KNOWN 0) sampai pin slot SD board M21
 *       terverifikasi - di board CAM pin SD bisa menabrak pin motor
 *       GPIO38-41, menebak di sini berbahaya. Kartu juga harus
 *       berformat FAT32 (kartu 1 TB bawaan pabrik = exFAT, tidak
 *       terbaca ESP32).
 *  v6.9.1 = v6.9 + AKTIVASI SD CARD & KENDALI TIDUR DRIVER: pin slot SD
 *       board ini TERVERIFIKASI dari pinout resmi penjual (SD_CMD =
 *       GPIO38, SD_CLK = GPIO39, SD_DATA/D0 = GPIO40, mode SDMMC 1-bit)
 *       - persis pin motor IN1/IN2/IN3. Karena itu SD dan motor dipakai
 *       BERGANTIAN lewat sesi terkunci (sdSessionBegin/End): duty motor
 *       0, driver DITIDURKAN lewat EEP yang kini dikendalikan GPIO45
 *       (tidak lagi terikat 3V3), pin LEDC dilepas, kartu di-mount, foto
 *       ditulis, kartu di-unmount, pin dipasang lagi, duty 0, driver
 *       dibangunkan. SD TIDAK PERNAH dibiarkan mounted permanen, dan
 *       driver tidur = output mati total sehingga dinamo mustahil
 *       bergerak selama sesi SD. Kabel EEP WAJIB sudah dipindah dari
 *       pin 3V3 ke GPIO45 sebelum firmware ini dipakai. GPIO45 adalah
 *       pin strapping VDD_SPI - aman karena EEP ber-pull-down internal
 *       (terbaca LOW saat boot = keadaan normal yang dibutuhkan board).
 *  v6.9.2 = VARIAN UJI DIAGNOSTIK SD (satu variabel dari v6.9.1): di board
 *       user v6.9.1 terbukti jalan (EEP GPIO45 OK) tetapi kartu gagal
 *       inisialisasi - "Failed to initialize the card (0x107)" = timeout,
 *       kartu tidak menjawab perintah init. Tersangka: jalur slot SD
 *       board ini tanpa pull-up eksternal. Varian ini menyalakan pull-up
 *       internal pada CMD/CLK/D0 tepat sebelum SD_MMC.begin() dan
 *       menurunkan clock SD ke maksimum 10 MHz (parameter begin ke-4,
 *       kHz). Bila kartu terbaca SIAP, akar masalah = pull-up dan varian
 *       ini menjadi firmware tetap; bila tetap 0x107, tersangka beralih
 *       ke kartu itu sendiri (uji kartu kecil asli lain).
 *  v6.9.3 = v6.9.2 + HALAMAN FILE MANAGER SD (permintaan user 2 Nov):
 *       halaman /files bergaya dashboard untuk mengelola isi SD card -
 *       telusur folder (breadcrumb + tautan folder induk), Buka/Unduh
 *       berkas (/file-get, inline/attachment), Hapus berkas & folder
 *       kosong (/file-del, rmdir hanya berhasil bila folder kosong),
 *       Buat Folder (/file-mkdir), Unggah berkas (/file-upload).
 *       Semua akses SD tetap lewat sesi terkunci sdSessionBegin/End,
 *       jadi motor berhenti & driver tidur selama operasi berkas
 *       berjalan - itu memang perilaku interlock, bukan kerusakan.
 *       Temuan uji v6.9.2 juga dikukuhkan di sini: di board user kartu
 *       terbaca "SD card: SIAP (1024000 MB)" - akar masalah 0x107
 *       memang pull-up, sehingga pull-up internal + clock SD maks
 *       10 MHz dipertahankan sebagai perilaku permanen.
 *       Tambahan amendemen v6.9.3: (a) PENGERASAN WIFI untuk bug
 *       "WiFi tiba-tiba hilang sendiri" - modem sleep dimatikan
 *       (setSleep false; DIBATALKAN di v7.0.2, lihat entri v7.0.2)
 *       + WiFi.setAutoReconnect(true) di kedua
 *       jalur (STA & AP fallback), plus watchdog ringan di loop():
 *       tiap 15 detik mode STA yang putus disambung ulang otomatis,
 *       mode AP tidak diganggu; (b) MODE OTOMATIS AI MENYALA SEJAK
 *       BOOT secara bawaan (g_aiAuto = true) - analisis memang
 *       on-device/offline; isi /aiset.txt bila ada tetap yang menang,
 *       saklar dashboard & penyimpanan pilihan tidak berubah.
 *  v6.9.4 = PERBAIKAN CRASH-LOOP INFERENSI AI (terbukti dari log board
 *       user 2 Nov: boot #82, sesudah AP aktif inferensi otomatis
 *       pertama langsung "Failed to allocate persistent buffer ...
 *       EI_MAX_OVERFLOW_BUFFER_COUNT" lalu Guru Meditation
 *       StoreProhibited dengan EXCVADDR 0x0 -> reboot -> mode otomatis
 *       yang menyala sejak boot memicu lagi -> putaran tanpa ujung).
 *       Diagnosis: model EON memakai EI_CLASSIFIER_ALLOCATION_HEAP -
 *       tensor arena (246.784 byte) dialokasikan saat runtime lewat
 *       ei_malloc/ei_calloc bawaan library yang LEMAH (weak, malloc
 *       RAM internal biasa). RAM internal sesak oleh WiFi + kamera +
 *       server sehingga alokasi gagal (NULL), SDK tetap lanjut dan
 *       menulis ke alamat NULL -> panic. Tiga lapis perbaikan:
 *       (1) OVERRIDE ei_malloc/ei_calloc/ei_free di main.cpp (versi
 *       kuat, tanpa weak) mengalokasikan ke PSRAM via heap_caps
 *       (fallback RAM internal) - terverifikasi menang lewat nm pada
 *       firmware.elf (simbol berubah W -> T dari objek main);
 *       (2) CEK RUANG sebelum inferensi: PSRAM bebas < 512 KB ->
 *       analisis dilewati dengan pesan Serial, run_classifier tidak
 *       dipanggil; (3) PENAHAN ANTI-PUTARAN: penanda /aiguard.txt
 *       ditulis tepat sebelum run_classifier dan dihapus tepat
 *       sesudahnya - bila saat boot penanda masih ada, berarti boot
 *       sebelumnya mati di tengah inferensi, maka mode otomatis
 *       DITAHAN khusus boot itu (g_aiAutoHeld) supaya putaran crash
 *       berhenti; analisis manual /analyze tidak ikut ditahan.
 *  v6.9.5 = DIAGNOSTIK + PENGERASAN INFERENSI AI (lanjutan v6.9.4:
 *       penahan aiguard TERBUKTI bekerja di board user - boot #119
 *       stabil dan mode otomatis ditahan sesudah boot sebelumnya mati
 *       DI DALAM run_classifier. Arena SUDAH di PSRAM sejak v6.9.4:
 *       init model EON memanggil ei_aligned_calloc yang bermuara ke
 *       ei_calloc override v6.9.4, jadi alokasi bukan lagi tersangka.
 *       Kematian kini di dalam EKSEKUSI classifier itu sendiri;
 *       tersangka tersisa: task watchdog loopTask, stack, atau panic
 *       op. v6.9.5 menutup dua tersangka mekanis yang bisa ditutup
 *       dan memasang penanda agar kegagalan berikutnya menamai
 *       dirinya sendiri: (1) ALASAN RESET dicetak di awal setup()
 *       dari esp_reset_reason() - boot sesudah kematian inferensi
 *       langsung terbaca PANIC / TASK WATCHDOG / INTERRUPT WATCHDOG /
 *       BROWNOUT, tanpa menebak dari dump; (2) JEJAK INFERENSI
 *       mengelilingi run_classifier: baris "[AI] inferensi mulai"
 *       (sisa stack loopTask & heap internal bebas) tepat sebelum,
 *       baris "[AI] inferensi selesai | kode | ms" tepat sesudah;
 *       (3) WATCHDOG loopTask DIMATIKAN selama inferensi berjalan
 *       (esp_task_wdt_reset() + disableLoopWDT() sebelum,
 *       enableLoopWDT() tepat sesudah run_classifier kembali) -
 *       inferensi dengan arena di PSRAM sah berjalan berdetik-detik,
 *       jadi melampaui batas task watchdog bukan bukti crash.
 *       Penanda aiguard dan seluruh perilaku lain tidak berubah
 *       dari v6.9.4.
 *  v6.9.6 = v6.9.5 + DASHBOARD BARU + PERBAIKAN GRAFIK HP (permintaan
 *       user 2 Nov; desain disetujui lebih dulu dari mockup yang
 *       di-render asisten): halaman dashboard diganti desain SIDEBAR
 *       + IKON SVG gaya RuteAman (sidebar teal gelap #12383A tetap
 *       di layar lebar; di HP menjadi topbar + drawer geser dengan
 *       overlay). Seluruh id elemen, endpoint, dan logika JS halaman
 *       TIDAK berubah - hanya struktur & gaya (ikon SVG inline agar
 *       tetap tampil di jaringan robot yang offline). Perbaikan
 *       grafik tren: kanvas kini diskalakan mengikuti lebar layar
 *       tampil (clientWidth x devicePixelRatio, tinggi tetap 260)
 *       dan legenda membungkus ke baris kedua di layar sempit -
 *       tidak lagi bertabrakan seperti versi sebelumnya. TIDAK ada
 *       perubahan logika firmware/endpoint di luar DASHBOARD_HTML;
 *       diagnostik + pengerasan inferensi v6.9.5 utuh seluruhnya.
 *  v6.9.7 = v6.9.6 + INFERENSI AI PINDAH KE TASK KHUSUS (lanjutan
 *       diagnosis v6.9.5: di board user, boot #125 mencetak "[Boot]
 *       alasan reset: PANIC (crash program)" dan penanda aiguard
 *       termakan - inferensi TETAP panic di dalam run_classifier
 *       walau arena sudah di PSRAM dan watchdog loop sudah dimatikan
 *       selama inferensi. Tersangka utama kini: stack loopTask
 *       (8 KB, dipakai bersama WebServer) habis oleh rantai
 *       panggilan classifier. Obat v6.9.7: runAiAnalysis HANYA
 *       berjalan dari task FreeRTOS khusus "ai" ber-stack 32 KB -
 *       dipicu lewat semaphore dari handler /analyze (menunggu
 *       selesai, maks 30 detik) dan dari pemicu otomatis di loop()
 *       (tanpa menunggu); inferensi yang sedang berjalan menolak
 *       pemicu baru (g_aiBusy). runAiAnalysis sendiri TIDAK diubah
 *       isinya (aiguard, jejak, disableLoopWDT utuh). Sesudah tiap
 *       inferensi, task mencetak sisa stack-nya ("[AI] stack task
 *       sisa") agar kebutuhan stack terbaca dari bukti. Tambahan:
 *       MUTEX sesi SD (g_sdMutex) - sdSessionBegin mengambil di
 *       baris pertama, sdSessionEnd melepas di akhir; semua situs
 *       panggil memasangkan begin/end 1:1 di semua jalur (termasuk
 *       jalur gagal mount, di mana pemanggil tetap memanggil end),
 *       jadi take/give selalu berpasangan dan sesi SD dari task AI
 *       tidak pernah tumpang tindih dengan sesi handler berkas.
 *       Cara baca hasil uji: baris "[AI] inferensi selesai" muncul
 *       = sembuh; bila board tetap reboot dengan alasan PANIC,
 *       dump Guru Meditation lengkap (dengan backtrace) di Serial
 *       adalah bukti berikutnya.
 *  v6.9.8 = v6.9.7 + RESET MODEL EI SESUDAH TIAP INFERENSI (akar
 *       masalah TERBUKTI dari dump board v6.9.7: baris mulai
 *       mencatat stack bebas 30.012 B & heap internal bebas
 *       193.532 B - BUKAN stack, BUKAN watchdog, BUKAN alokasi.
 *       ERR-nya persis "Failed to allocate persistent buffer ...
 *       reached EI_MAX_OVERFLOW_BUFFER_COUNT" lalu panic
 *       StoreProhibited: jalur run_classifier yang dipakai
 *       firmware (tflite_eon.h run_nn_inference) memanggil
 *       model_init tiap analisis (arena 246.784 B dialokasi baru)
 *       tetapi TIDAK PERNAH memanggil model_reset - berbeda
 *       dengan jalur run_nn_inference_from_dsp SDK yang
 *       memanggilnya. Akibatnya penghitung buffer luapan
 *       (overflow_buffers_ix, maks EI_MAX_OVERFLOW_BUFFER_COUNT
 *       = 10) dan arena lama tidak pernah dibersihkan
 *       antar-analisis; AllocatePersistentBufferImpl akhirnya
 *       mengembalikan NULL -> SDK menulis ke NULL -> panic.
 *       Obat: tepat sesudah run_classifier kembali (sebelum
 *       enableLoopWDT), panggil tflite_learn_1154536_5_reset(
 *       ei_aligned_free) - pasangan persis yang dipakai SDK
 *       sendiri di jalur DSP - hanya bila err == EI_IMPULSE_OK
 *       atau EI_IMPULSE_TFLITE_ERROR (blok NN benar-benar
 *       berjalan), agar tidak free pada arena yang tidak
 *       teralokasi / dobel. Cara baca hasil uji: baris "[AI]
 *       inferensi selesai" BERULANG (mode otomatis / Analisis
 *       ditekan berkali-kali) tanpa satu pun reboot dan nomor
 *       boot berhenti naik = kebocoran antar-analisis tertutup.
 *  v7.10.2 = v7.10.1 + SD TIDAK MENYANDERA BOOT & LOOP (kejadian
 *       12 Nov dini hari: kabel IN1-IN3 motor tercolok langsung di
 *       board membebani pin bersama CMD/CLK/D0 SD (38/39/40) yang
 *       hanya ditopang pull-up internal lemah -> kartu berhenti
 *       menjawab -> tangga mount 6x menahan boot sebelum WiFi naik
 *       ("stuck tidak mau nyala") dan membekukan loop belasan detik
 *       di runtime (buzzer membeku ON karena pollBuzzer tidak
 *       jalan). Tiga perubahan di sdSessionBegin: (1) probe boot
 *       maks 3 percobaan, sesi berikutnya maks 2; (2) sesudah SATU
 *       gagal total, tangga dilewati selama 45 detik (cooldown) -
 *       sesi pulang tanpa mount, pemanggil sudah menangani
 *       !g_sdMounted; (3) log gagal tidak di-spam selama cooldown.
 *       Kartu yang menjawab tetap mount di percobaan 1; interlock
 *       motor-sesi SD tidak berubah.
 *  v7.10.1 = v7.10.0 + kartu Kekeruhan dashboard TIDAK lagi
 *       menampilkan angka tegangan (V) - yang tampil hanya label
 *       klasifikasinya sebagai nilai utama kartu (permintaan user
 *       10 Nov malam, sesudah melihat dashboard v7.10.0: "0.00 V"
 *       membingungkan di samping label). Tegangan tetap ada di
 *       telemetri/grafik/log untuk diagnosis; baris JS pengisi
 *       elemen V dan elemennya dibuang, kelas warna label kini
 *       menumpang kelas .val agar ukuran nilai utama bertahan.
 *  v7.10.0 = v7.9.5 + DUA PERUBAHAN PERMINTAAN USER 10 Nov:
 *       (1) GYRO/MPU6050 DIHAPUS TOTAL (user mencabut sensornya
 *       dari robot): seluruh kode MPU (Wire + bit-bang + pemulihan
 *       berkala), global, field telemetri roll/pitch/mpu, kartu
 *       Kemiringan dashboard + JS-nya, dan kolom roll/pitch CSV
 *       dibuang; banner boot tak lagi menyebut MPU6050.
 *       (2) HALAMAN ADMIN /adminin: override label klasifikasi
 *       turbidity di kartu Kekeruhan web (otomatis/jernih/sedang/
 *       keruh), hanya bisa diakses lewat alamat /adminin (tanpa
 *       tautan di dashboard), persisten di LittleFS (/turbovr.txt).
 *       Cakupan hanya LABEL klasifikasi: angka V/persen, WQI,
 *       ALERT, dan log tetap dari sensor; telemetri menambah field
 *       turbidity_override dan dashboard mengabaikan cabang fault
 *       saat override aktif.
 *  v7.9.5 = v7.9.4 + ANAK TANGGA KE-3 /cam-diag: QVGA buffer di
 *       DRAM internal (CAMERA_FB_IN_DRAM) - membelah cabang terakhir
 *       sisi firmware: grab lolos di DRAM = jalur PSRAM/DMA yang
 *       bermasalah; tetap gagal = putus fisik jalur data (ribbon/
 *       konektor/modul). initCameraCfg kini berparameter lokasi
 *       buffer juga. Pin header GPIO4-18 terkonfirmasi user KOSONG
 *       (tak ada solderan user) - tersangka jembatan timah gugur.
 *  v7.9.4 = v7.9.3 + SDA MPU PINDAH GPIO43 -> GPIO47 (satu isu:
 *       bereskan teks sampah di log serial). Akar: GPIO43 adalah
 *       U0TXD - pin TX konsol USB-serial board ini; sejak v7.9.1
 *       menaruh SDA bit-bang MPU di situ, setiap goyangan I2C
 *       terkirim ke monitor sebagai karakter sampah, mulai tepat
 *       sesudah baris "AI kandidat korosi: SIAP" (initMPU berjalan
 *       sesudah printf itu di setup). SCL tetap GPIO46. User
 *       memindah ujung ESP kabel SDA ke GPIO47.
 *  v7.9.3 = v7.9.2 + TANGGA PEMULIHAN KAMERA di /cam-diag (satu
 *       isu). Bukti board 10 Nov: init kamera selalu OK (PID
 *       0x5640 = OV5640 terbaca) tetapi grab selalu NULL - bahkan
 *       sesudah ribbon terverifikasi duduk mentok. initCamera kini
 *       berparameter (initCameraCfg); /cam-diag menuruni tangga
 *       konfigurasi ringan (SVGA/20MHz -> VGA/16MHz -> QVGA/10MHz,
 *       1 buffer) dan membiarkan anak tangga yang berhasil tetap
 *       aktif; gagal semua -> konfigurasi produksi dipulihkan.
 *       Banner boot kini juga menulis v7.9.3 (di v7.9.1/v7.9.2
 *       string banner tertinggal "v7.9.0" - kosmetik).
 *  v7.9.2 = v7.9.1 + DETEKSI KAMERA (satu isu, hanya diagnostik
 *       kamera - perintah user 10 Nov: "bikinin deteksi kamera
 *       dikodena"). Latar: dashboard hanya menampilkan placeholder
 *       dan /capture menjawab 503 "kamera sibuk" bahkan di daya
 *       USB, tetapi kode error init kamera selama ini DIBUANG
 *       (initCamera hanya mengembalikan bool) sehingga kegagalan
 *       tidak meninggalkan jejak. Kini: status init + kode error
 *       esp_camera_init disimpan (g_camOk/g_camErr), PID sensor
 *       dibaca saat init OK (0x2642 = OV2640, 0x5640 = OV5640 -
 *       sekaligus menjawab varian kamera aktual board ini), log
 *       boot mencetak kode + nama error, dan endpoint BARU
 *       GET /cam-diag melaporkan semuanya ditambah uji ambil
 *       1 frame langsung (grab_ok + ukuran frame). TIDAK ada
 *       perubahan perilaku kamera/stream/motor/MPU dari v7.9.1.
 *  v7.9.1 = v7.9.0 + PINDAH PIN MPU6050 (satu isu, hanya jalur
 *       MPU - dikerjakan atas perintah user 10 Nov). Bukti meja
 *       9-10 Nov: modul MPU bebas (modul lain sama bisu), VCC
 *       3,23 V di kaki modul, AD0 0 (alamat 0x68), SDA/SCL diam
 *       HIGH sehat, continuity SDA & SCL BUNYI ujung-ke-ujung
 *       dari GPIO47/48 ke modul - tetapi MPU tidak pernah
 *       menjawab. Tersangka tersisa: pin ESP 47/48 atau bus
 *       mati di ujung modul. SDA 47 -> GPIO43, SCL 48 -> GPIO46
 *       (audit peta pin v7.9.0: 43 bekas U0TXD, pasangannya 44
 *       sudah tap CHG sejak v7.3.6; 46 bebas; 26-37 terlarang).
 *       Jalur Wire dan bit-bang sama-sama membaca dua #define
 *       MPU_SDA_PIN/MPU_SCL_PIN, jadi cukup dua angka ini.
 *       TIDAK ada perubahan lain dari v7.9.0.
 *  v7.9.0 = v7.8.3 + PENYESUAIAN DETEKSI MATI TURBIDITY ke wiring
 *       TERPASANG user (perintah user 9 Nov 18:50: kode menyesuaikan
 *       rantai yang sudah ada, BUKAN sebaliknya). Mode tetap PROBE:
 *       kuning -> simpul; simpul ditarik ke 3V3 lewat resistor
 *       penarik (terukur 20k) DAN ke GND lewat 20k (keadaan diam
 *       gelap 2,2 V - keputusan v7.2.5 tetap: keruh, bukan fault);
 *       GPIO1 menempel di simpul; board adaptor hanya menyuplai
 *       probe (V ke 5V, G ke GND; konektor probe 1 kuning /
 *       2 biru / 3 merah / 4 kosong); pin A dan D board TIDAK
 *       dipakai. Draf mode pin-A (v7.9.0 versi pertama) DIBATALKAN
 *       user sebelum sempat dikirim.
 *       BARU: deteksi BEKU - median mV tidak berubah (selisih
 *       <= 1 mV) selama TURB_FREEZE_N (60) siklus berturut-turut ->
 *       "tidak merespons". Menutup lubang lama: saluran mati yang
 *       duduk di tegangan diam divider (bukan menempel di rel
 *       3,3 V) dulu terbaca "keruh" selamanya. Aturan rel
 *       (TURB_PROBE_FAULT_V / mvMed >= 3100) tetap aktif untuk
 *       keadaan kaki 20k-ke-GND terbuka. Jangkar acuan user
 *       TURB_PROBE_CLEAR_V 0,42 / TURB_PROBE_DARK_V 0,85 TIDAK
 *       berubah; modul lain tidak berubah.
 *  v7.4.2 = v7.4.1 + PERBAIKAN DETEKSI CAS + SELF-TEST MOTOR
 *       DIPERKUAT SAKSINYA (permintaan user 6 Nov 10:26 dari
 *       screenshot boot v7.4.1 boot #1030: self-test terbukti
 *       berjalan dan EEP terbaca=1, tetapi roda tetap diam; dan
 *       status cas terdeteksi "mengecas" padahal robot tidak
 *       sedang dicas, USB hanya tersambung ke ESP).
 *       (A) CHG: (1) status "mengecas" kini butuh pin CHRG LOW
 *           STABIL 5 DETIK (20 sampel x 250 ms) - dulu cukup
 *           750 ms, terlalu gampang ketipu level liar sesaat.
 *           (2) Telemetri + baris [T] kini memuat chg_raw (level
 *           pin GPIO44 saat itu juga: 1 = LOW) dan chg (status
 *           stabil) - keadaan pin terlihat apa adanya, bukan
 *           cuma bunyi buzzer. (3) Kata pesan boot diperbaiki:
 *           dulu "[CHG] tap CHRG aktif" gampang terbaca sebagai
 *           deteksi cas; kini "siap" + syaratnya ditulis.
 *           CATATAN FISIKA yang tidak bisa diobati kode: pin CHRG
 *           TP4056 adalah open-drain; saat charger TIDAK berdaya
 *           (USB cas tidak tertancap) pin itu mengambang dan node
 *           tap bisa ketarik rendah lewat jalur LED modul -
 *           bacaan LOW dalam keadaan itu memang tidak bermakna,
 *           dan chg_raw kini memperlihatkannya secara jujur.
 *       (B) MOTOR: self-test boot diperkuat jadi saksi ukur:
 *           duty 100% selama 0,9 detik per sisi, dan SELAMA itu
 *           firmware menyampel pin IN yang aktif ribuan kali lalu
 *           mencetak persen terbaca HIGH (harapan 100/0) - sinyal
 *           di PAD ESP terbukti terukur, bukan cuma diklaim
 *           program. Log self-test juga mencetak catatan syarat
 *           daya driver (VCC DRV8833 langsung dari MT3608: dari
 *           USB saja + saklar OFF, driver memang tidak berdaya
 *           dan roda mustahil bergerak - uji gerak wajib dari
 *           baterai + saklar ON). Logika drive v7.4.0/7.4.1
 *           (pin lurus, langsung, EEP HIGH, saksi baca-balik)
 *           TIDAK berubah. APK tetap v1.9.
 *  v7.4.1 = v7.4.0 + SELF-TEST MOTOR SAAT BOOT + SAKSI EEP JUJUR
 *       (permintaan user 6 Nov pagi: "kodenya dibenerin untuk motor
 *       driver, sekarang malah tidak bisa gerak" - sesudah user
 *       mengganti colokan IN1-IN4 + VCC + GND DRV8833 menjadi
 *       female header, tinggal colok, tidak rawan saling sentuh).
 *       Perubahan HANYA di jalur kendali motor:
 *       (1) SELF-TEST BOOT: sesudah initMotors(), SEBELUM WiFi
 *           mulai, firmware menggerakkan motor KIRI 70% selama
 *           700 ms, berhenti 300 ms, lalu motor KANAN 70% selama
 *           700 ms, lalu berhenti - TANPA HP, TANPA dashboard,
 *           TANPA perintah apa pun. Bila self-test ini bergerak,
 *           jalur drive hidup dan masalah ada di rantai perintah
 *           runtime; bila diam total, tidak ada lagi lapisan kode
 *           di antara setup dan motor yang bisa dituduh.
 *           SYARAT UJI: robot dipegang/diangkat sebelum dinyalakan.
 *       (2) SAKSI EEP JUJUR: baris [MOTOR-LANGSUNG] kini mencetak
 *           hasil BACA-BALIK pin GPIO45 (digitalRead sesudah
 *           ditulis HIGH), bukan tulisan "HIGH" karangan program.
 *           Terbaca 0 padahal ditulis 1 = sinyal EEP tidak sampai
 *           / tertahan - terbaca dari kode, bukan tebakan.
 *       (3) DUTY DITULIS ULANG BERKALA: pollMotors menulis ulang
 *           duty + EEP HIGH tiap 50 ms selama target bukan nol -
 *           bila ada proses lain yang mengutak-atik channel LEDC
 *           di tengah jalan, terapan pulih sendiri.
 *       Logika v7.4.0 (pin lurus, sign-magnitude, tanpa slew,
 *       antre sesi SD, mode uji T) TIDAK berubah. APK tetap v1.9.
 *  v7.4.0 = v7.3.9 + MOTOR LANGSUNG (permintaan user 5 Nov malam:
 *       "tetap tidak mau gerak sejak VCC diganti ke MT 5V - jangan
 *       dipindah-pindah lagi, cukup kodenya saja yang diubah").
 *       WIRING TIDAK DISENTUH SAMA SEKALI - VCC DRV8833 tetap kabel
 *       sendiri langsung dari OUT MT3608 5V. Yang diubah HANYA
 *       kode jalur motor, dikembalikan ke bentuk era TERBUKTI
 *       GERAK (v7.1.2, laporan user 2 Nov) dan semua penahan di
 *       jalur itu dicabut:
 *       (1) PETA PIN kembali LURUS v7.1.2: IN1=38, IN2=39,
 *           IN3=40, IN4=41 (pertukaran peran v7.1.7 DIBATALKAN).
 *       (2) driveSide kembali SIGN-MAGNITUDE simetris v7.1.2:
 *           maju PWM di pin A + pin B 0; mundur pin A 0 + PWM di
 *           pin B (skema anti-phase mundur v7.1.6 DIBATALKAN).
 *       (3) SOFT-START/SLEW DICABUT dari jalur terapan: perintah
 *           ditulis ke LEDC SAAT ITU JUGA di setMotors (dulu
 *           menunggu langkah slew 3 ms di pollMotors).
 *       (4) EEP (nSLEEP) DIPAKSA HIGH di setiap penerapan duty
 *           dan setiap pollMotors - tidak ada keadaan kode yang
 *           membiarkan driver tertidur sesudah perintah jalan.
 *       (5) Perintah yang datang saat sesi SD aktif TIDAK LAGI
 *           DIBUANG: disimpan sebagai antrean dan dijalankan
 *           otomatis begitu sesi selesai (log "[MOTOR] ANTRI").
 *       (6) MODE UJI LANGSUNG cmd=T (buka /motor?cmd=T): lepas
 *           LEDC, kendarai keempat pin secara DIGITAL penuh
 *           (IN1/IN3 HIGH, IN2/IN4 LOW) 1,2 detik lalu berhenti
 *           dan pasang lagi - memintas seluruh jalur PWM; ini
 *           pembeda terakhir di sisi kode (T gerak tapi F diam =
 *           jalur LEDC; dua-duanya diam dengan (1)-(5) aktif =
 *           tidak ada lagi penahan kode yang tersisa).
 *       (7) Saksi baru: tiap perintah mencetak baris
 *           "[MOTOR-LANGSUNG]" berisi duty empat channel persis
 *           yang ditulis + status EEP & sesi SD.
 *       Selain blok motor, TIDAK ada perubahan lain; saksi reset
 *       v7.3.8, bip boot v7.3.9, dan APK pasangan tetap v1.9.
 *  v7.3.9 = v7.3.8 + BIP BOOT BUZZER + SAKSI MOTOR DI DASHBOARD
 *       (diagnosis "buzzer diam & motor tidak gerak dari dashboard",
 *       laporan user 5 Nov 17:21-17:25 - robot SUDAH hidup mandiri
 *       dari baterai + AP menyala, telemetri mengalir, tetapi roda
 *       tidak bergerak dari web & aplikasi, dan buzzer tidak pernah
 *       berbunyi). Audit penuh v7.3.8: (a) BUZZER - pola bunyi hanya
 *       ada untuk ALERT (3 bip saat naik) dan indikator cas (2 bip
 *       tiap 60 dtk saat mengecas, 3 bip panjang saat penuh); TIDAK
 *       ada bip saat boot, jadi diam pada boot normal BUKAN
 *       kerusakan - tetapi ekspektasi user "bunyi 2x saat robot
 *       nyala" sah dan berguna sebagai uji jalur buzzer tiap boot.
 *       (b) MOTOR - rantai JS dashboard -> /motor -> handleMotor ->
 *       motorCommand -> setMotors -> slew pollMotors -> driveSide
 *       LEDC utuh & benar (pernah menggerakkan motor di era
 *       v7.1.2); satu-satunya penahan adalah interlock sesi SD
 *       yang kini berlog. Perubahan versi ini HANYA menambah
 *       bukti & bip, TANPA mengubah logika penggerak: (1) BIP
 *       BOOT 2x (150/150/150 ms, pola sama seperti indikator cas)
 *       dipicu di akhir setup() - berbunyi = jalur GPIO21 +
 *       buzzer hidup; tetap diam = fisik (kabel + ke GPIO21,
 *       - ke GND, atau modul buzzer). Prioritas pola: ALERT >
 *       boot > cas. (2) Telemetri menambah motor_tgt_l/r,
 *       motor_cur_l/r, motor_hold (sesi SD aktif) dan kartu
 *       Kendali Motor di dashboard menampilkan baris statusnya:
 *       target/terapan menanjak ke 100/100 selagi roda diam =
 *       program terbukti memerintah penuh - vonis pindah ke
 *       ukur VCC & IN1-IN4 di modul DRV8833. Logika driveSide/
 *       slew/interlock TIDAK berubah; APK pasangan tetap v1.9.
 *  v7.3.8 = v7.3.7 + SAKSI RESET PERSISTEN (diagnosis "fungsi
 *       mati saat dari baterai", laporan user 5 Nov 14:34).
 *       Gejala: dari USB semua fungsi jalan (terbukti log user
 *       14:09 - AP + STA + SD + motor 0/0), dari baterai/saklar
 *       robot hanya menyala dan WiFi tidak muncul (sama seperti
 *       laporan 11:41). Audit startWiFi: AP dinyalakan lebih
 *       dulu, STA menunggu maks 10 dtk, tidak ada tunggu
 *       Serial/USB; program identik di kedua sumber daya -
 *       yang berubah hanya dayanya. Catatan proyek sendiri
 *       sejak v7.0.1 sudah memperingatkan rel lemah bisa
 *       runtuh tepat di transisi radio (itu sebabnya daya
 *       pancar sudah 13 dBm). Alasan reset sebenarnya SUDAH
 *       dibaca tiap boot sejak v6.9.5, tetapi hanya dicetak
 *       ke Serial - tidak terlihat saat robot dari baterai.
 *       v7.3.8: tag alasan reset (POWERON / PANIC / BROWNOUT /
 *       TASK_WDT / INT_WDT / WDT / SW) kini ditulis juga ke
 *       baris #BOOT di /aidet.csv. Protokol vonis: jalankan
 *       robot 1 menit dari baterai, lalu baca baris #BOOT
 *       terakhir lewat dashboard USB - banyak boot baru +
 *       BROWNOUT = rel anjlok menanggung beban radio
 *       (obatnya fisik: rel/kabel/elco, bukan kode); PANIC
 *       atau WDT berulang = crash program (obatnya kode);
 *       satu boot POWERON = gali sisi lain dari bukti itu.
 *       Logika lain TIDAK berubah.
 *  v7.3.7 = v7.3.6 + SAKSI MOTOR DI LOG (diagnosis "dinamo kiri
 *       muter terus", laporan user 5 Nov 13:59). Audit penuh
 *       rantai motor v7.3.6: perintah hanya dari handleMotor ->
 *       motorCommand; stop ditegakkan watchdog 1,5 dtk + slew
 *       ke 0 + motorForceStopNow di tiap akhir sesi SD & watchdog
 *       sesi; logika driveSide/slew simetris kiri-kanan - TIDAK
 *       ADA jalur kode yang memutar SATU sisi terus-menerus.
 *       Satu-satunya asimetri kiri: kedua pinnya (GPIO38/39)
 *       berbagi bus SD, dan di modul DRV8833 pad IN2 bersebelahan
 *       dengan pad VCC. Karena itu versi ini hanya menambah
 *       BUKTI: baris [T] kini mencetak duty terapan "motor L/R"
 *       (g_curL/g_curR). Bila log menunjukkan motor 0/0 selagi
 *       roda kiri berputar, program terbukti tidak memerintah -
 *       penyebabnya di kabel/solder/driver, diputuskan dari ukur
 *       tegangan IN1 & IN2 di modul. Logika motor TIDAK berubah.
 *  v7.3.6 = v7.3.5 + INDIKATOR CAS/PENUH LEWAT BUZZER (permintaan
 *       user 5 Nov, Rute B): kabel tap dari kaki sinyal LED merah
 *       (CHRG) TP4056 -> resistor seri 10k -> GPIO44 (bekas U0RXD;
 *       trade-off disetujui: Serial Monitor tidak bisa mengetik ke
 *       board, log TX tetap jalan). Logika pin terverifikasi
 *       multimeter 5 Nov: chip MENARIK pin ke LOW selagi mengecas
 *       (titik terbaca 0,01 V), MELEPASNYA (HIGH) sesudah penuh.
 *       "Penuh" dideteksi dari transisi lepas sesudah LOW stabil
 *       >= 10 menit. Pola: mengecas = 2 bip pendek tiap 60 detik;
 *       penuh = 3 bip panjang SEKALI lalu diam; log bertanda [CHG].
 *       Buzzer dipakai bergantian dengan pola ALERT lewat
 *       pollBuzzer (ALERT selalu menang). Debounce 3x250 ms.
 *       Modul lain TIDAK berubah.
 *  v7.3.5 = v7.3.4 + OPTIMALISASI SAMPLING TURBIDITY (permintaan
 *       user 4 Nov: sensor kekeruhan lebih akurat). readSensors
 *       kini mengambil ledakan 24 bacaan analogReadMilliVolts
 *       (jalur kalibrasi eFuse - setup sudah ADC_11db) lalu
 *       memakai MEDIAN-nya sebagai tegangan simpul, ditambah
 *       perataan antar-siklus EMA alpha 0,5. Menggantikan satu
 *       bacaan analogRead tunggal + konversi kasar raw x
 *       3,3/4095 yang membuat persen kekeruhan loncat-loncat di
 *       air yang sama. g_turbMv naik pangkat dari saksi [T]
 *       menjadi nilai penggerak klasifikasi; g_turbRaw menjadi
 *       raw-setara dari median yang sama. Deteksi fault mode
 *       probe menambah syarat median >= 3100 mV karena jalur mV
 *       terkalibrasi jenuh di sekitar 3,1 V saat simpul
 *       menempel rel 3,3 V (ambang 3,25 V tak tercapai dari
 *       jalur itu saja); 3100 mV jauh di atas keadaan diam
 *       gelap 2,2 V yang menurut v7.2.5 tetap "keruh".
 *       Jangkar TURB_PROBE_* (0,42 / 0,85 / 3,25 V), WQI,
 *       endpoint, dan semua modul lain TIDAK berubah. Peta
 *       kalibrasi multi-titik menyusul sesudah dua angka ukur
 *       user (langit-langit gelap garpu tertutup + tegangan
 *       soket probe pin 3 vs pin 2) tersedia.
 *  v7.3.4 = v7.3.3 + KUNCI ANTI-PUTARAN CRASH AI: boot #606
 *       (3 Nov 20:00) membuktikan board berputar reboot -
 *       penanda aiguard tertinggal lagi (boot sebelumnya mati
 *       DI DALAM jendela run_classifier/model_reset, jadi
 *       perbaikan alokasi v7.3.1 BELUM menyembuhkan crash
 *       analisis) dan penahan lama hanya menahan mode
 *       otomatis satu boot, sehingga boot berikutnya
 *       menganalisis lagi dan crash lagi. v7.3.4: guard
 *       terdeteksi -> mode otomatis dimatikan PERSISTEN
 *       (saveAiAutoSetting) sampai dinyalakan manual.
 *       Putaran berhenti pada boot pertama v7.3.4. Obat
 *       crash analisisnya sendiri menunggu backtrace crash
 *       dari monitor user untuk didekode terhadap ELF
 *       v7.3.2 - tidak menebak obat kedua kalinya.
 *  v7.3.3 = v7.3.2 + MODE AP+STA (permintaan user 3 Nov 19:57:
 *       robot yang sedang connect ke hotspot tidak memancarkan
 *       hotspotnya sendiri, jadi tidak bisa dijangkau langsung)
 *       - startWiFi kini WIFI_AP_STA: AP "FIR-Robot"
 *       (192.168.4.1) dinyalakan SELALU lebih dulu, lalu STA
 *       menyambung seperti biasa; robot terjangkau dari dua
 *       alamat (web + aplikasi sama-sama jalan di keduanya);
 *       STA gagal -> AP tetap hidup, autoReconnect dinyalakan
 *       di kedua cabang. Plus bereskan log: loadWifiConfig
 *       mengecek exists() dulu agar baris error VFS
 *       "/littlefs/wifi.txt does not exist" tidak lagi muncul
 *       di boot bersih. Selain itu TIDAK ada perubahan.
 *  v7.3.2 = v7.3.1 + KONFIGURASI WIFI STA RUNTIME + BIDANG
 *       BATERAI (permintaan user untuk APK v1.7): kredensial
 *       hotspot STA kini bisa diganti dari aplikasi tanpa
 *       flash ulang - GET /wifi (status: mode, SSID tujuan &
 *       aktual, IP, RSSI, AP), POST /wifi-set (simpan ke
 *       LittleFS /wifi.txt lalu restart; arg reset menghapus
 *       berkas -> kembali ke bawaan). startWiFi memakai
 *       g_staSsid/g_staPass hasil loadWifiConfig(); fallback
 *       AP darurat tidak berubah (fail-safe kredensial salah).
 *       Telemetri menambah battery_mv/battery_pct dari
 *       readBatteryMv() - saat ini -1 (BELUM TERUKUR: jalur
 *       divider baterai berhenti di terminal, tidak ada ADC;
 *       pengaktifan = ADS1115 di bus I2C, terdokumentasi di
 *       blok BATERAI). Alokasi EI v7.3.1, motor, MPU,
 *       kekeruhan TIDAK berubah.
 *  v7.3.1 = v7.3.0 + PERBAIKAN CRASH ANALISIS (satu isu, hanya
 *       override alokasi EI - laporan user: board crash & reboot
 *       saat motor digerakkan bersamaan analisis foto).
 *       Backtrace didekode via addr2line (ELF v7.3.0, nomor
 *       baris editor user cocok persis dengan kanonis): assert
 *       multi_heap_free <- free <- jalur init/reset model di
 *       runAiAnalysis. Akar mekanis: ei_malloc/ei_calloc lama
 *       memakai heap_caps_aligned_* DI BAWAH ei_aligned_calloc
 *       SDK = dua lapisan alignment; pointer yang dikembalikan
 *       ei_aligned_free ke free() bukan awal blok heap ->
 *       assert. Diperbaiki ke pola baku satu lapisan
 *       (heap_caps_malloc/calloc polos ke PSRAM, pad 256 B
 *       dipertahankan). Kode motor, MPU (termasuk bit-bang),
 *       kekeruhan, SD TIDAK berubah.
 *  v7.3.0 = v7.2.9 + BIT-BANG I2C MPU (satu isu, hanya jalur
 *       MPU - pilihan user dari widget: "kirim versi uji
 *       bit-bang-nya"). Master I2C perangkat lunak di pin
 *       47/48 yang sama, melewati peripheral I2C ESP32
 *       sepenuhnya, sebagai uji wasit terakhir dari sisi kode:
 *       bila bit-bang menemukan chip (kriteria v7.0.7 yang
 *       sama), peripheral-lah yang bermasalah dan bacaan
 *       kemiringan berjalan lewat bit-bang (flag g_mpuBitbang,
 *       cabang khusus di readTilt, pemulihan berkala mencoba
 *       jalur terbukti dulu); bila bit-bang pun kosong,
 *       lognya sendiri menyatakan peripheral bebas tuduhan.
 *       Ditambahkan bbStart/bbStop/bbWriteByte/bbReadByte/
 *       bbWriteReg/bbReadReg/bbReadBurst/bbTryAddr/bbProbeMPU.
 *       Motor v7.2.8, kekeruhan v7.2.6 TIDAK berubah.
 *  v7.2.9 = v7.2.8 + PENYELAMATAN BUS LAMBAT MPU (satu isu,
 *       hanya jalur clock I2C MPU - perintah user "benerin aja
 *       codenya"). Bukti boot #573: level bus 1/1, LED modul
 *       menyala (chip berdaya), pindai kosong di dua alamat -
 *       pola yang cocok dengan joint jalur sinyal yang MELEMAH
 *       (resistansi seri naik -> tepi sinyal melambat -> ACK
 *       100 kHz terlewat), bukan cuma putus total. Ditambahkan:
 *       bila probe 100 kHz gagal, initMPU menurunkan clock ke
 *       10 kHz, memindai + memprobe ulang, dan clock yang
 *       berhasil disimpan sebagai clock kerja (g_mpuClock);
 *       pemulihan berkala mencoba clock kerja lalu clock
 *       satunya. Pindai bus diekstrak menjadi mpuScanPrint().
 *       Logika deteksi, motor v7.2.8, kekeruhan v7.2.6 TIDAK
 *       berubah. Bila 10 kHz pun kosong: jalur putus total -
 *       tidak ada perbaikan kode lagi sesudah ini.
 *  v7.2.8 = v7.2.7 + PERBAIKAN JALUR TERTAHANNYA MOTOR (satu
 *       isu, hanya jalur motor/sesi SD - permintaan user "modul
 *       drivernya ngebug, gabisa gerak"). Audit menemukan dua
 *       cacat nyata di sekitar interlock sesi SD (bukan di
 *       driveSide/setMotors - rantai perintah terbukti benar):
 *       (1) handleMotor membuang perintah selama sesi SD TANPA
 *       satu baris log pun sambil membalas ok - dari luar persis
 *       "motor ngebug". Kini mencetak "[MOTOR] DITAHAN: sesi SD
 *       aktif (N dtk)"; (2) sesi SD tidak punya batas waktu -
 *       kartu sekarat (0x107 berulang hari ini, sesi dipicu
 *       otomatis oleh simpan foto AI) bisa menggantung di dalam
 *       panggilan SD dan motor mati SELAMANYA oleh interlock.
 *       Ditambah watchdog sesi 20 detik di loop: pin dipulihkan
 *       paksa persis seperti restorasi sdSessionEnd, mutex
 *       sengaja tidak dilepas (milik task pemilik sesi).
 *       Skema drive, peta pin v7.1.7, MPU v7.2.7, kekeruhan
 *       v7.2.6 TIDAK berubah.
 *  v7.2.7 = v7.2.6 + PEMULIHAN BUS I2C MPU (satu isu, hanya
 *       bagian MPU - permintaan user): sejak boot 17:00 MPU
 *       tidak menjawab pindai bus sama sekali (err68=2/err69=2)
 *       lintas lima versi firmware yang kode MPU-nya identik.
 *       Ditambahkan mpuBusRecover(): baca level diam SDA/SCL
 *       (diagnostik tercetak "[MPU] level bus"), bus-clear
 *       9 pulsa SCL + STOP bila SDA tertahan (kunci bus klasik
 *       sesudah brownout - robot terbukti pernah brownout),
 *       lalu Wire dinyalakan ulang; dipakai di initMPU dan di
 *       pemulihan berkala 15 detik. Kriteria deteksi v7.0.7,
 *       kekeruhan v7.2.6, dan semua fitur lain TIDAK berubah.
 *  v7.2.6 = v7.2.5 + SENSITIVITAS KEKERUHAN DINAIKKAN
 *       (satu isu, permintaan user "naikin persentasenya
 *       dikit-dikit"): skala dipersempit CLEAR 0,45 -> 0,42 V
 *       dan DARK 1,30 -> 0,85 V, supaya kenaikan tegangan
 *       simpul saat dicelup ke air keruh terbaca sebagai
 *       persen + level sedang/keruh, bukan tetap "jernih".
 *       FAULT tetap 3,25 V; rumus dan fitur lain TIDAK berubah.
 *  v7.2.5 = v7.2.4 + JANGKAR DIPINDAH KE ACUAN UDARA USER
 *       (satu isu): user menetapkan garpu tak terhalang di
 *       udara HARUS terbaca "jernih"; titik kerja nyata
 *       rangkaian terpasang di udara adalah 0,38-0,42 V
 *       (bukan 0,00 V sesaat di log pertama bypass), jadi
 *       CLEAR 0,05 -> 0,45 V, DARK 0,55 -> 1,30 V, FAULT
 *       2,10 -> 3,25 V. Rumus dan fitur lain TIDAK berubah.
 *  v7.2.4 = v7.2.3 + KALIBRASI ULANG JANGKAR PROBE (satu isu):
 *       bypass v7.2.3 TERBUKTI menghidupkan sinyal (log user
 *       3 Nov 17:52: simpul 0,00-0,01 V saat cahaya penuh dan
 *       NAIK ke 0,40-0,41 V di air banyak partikel - arah
 *       benar), tetapi jangkar desain awal (jernih 2,20 V /
 *       gelap 3,30 V) tidak cocok dengan rangkaian terpasang
 *       yang menarik simpul praktis ke GND saat menghantar,
 *       sehingga klasifikasi jenuh di "jernih". Jangkar diganti
 *       ke rentang ukur nyata: CLEAR 0,05 V, DARK 0,55 V,
 *       FAULT 2,10 V. Rumus, ambang fraksi, dan semua fitur
 *       lain TIDAK berubah.
 *  v7.2.3 = v7.2.2 + BYPASS PROBE LANGSUNG (satu isu, atas
 *       perintah user 3 Nov sore karena waktu lomba mepet):
 *       tahap A dan D adaptor turbidity terbukti mati (diduga
 *       satu chip MCP6002), jadi fototransistor probe dibaca
 *       LANGSUNG oleh GPIO1 lewat rangkaian simpul baru -
 *       kabel kuning probe -> 20k -> simpul GPIO1, simpul
 *       ditarik ke 3V3 lewat 10k; adaptor hanya memberi daya
 *       ke probe (V/G utuh, A/D tidak dipakai). Klasifikasi,
 *       persen, sub-indeks WQI, dan deteksi fault dihitung
 *       dari tegangan simpul dengan dua jangkar pra-kalibrasi
 *       TURB_PROBE_CLEAR_V / TURB_PROBE_DARK_V; telemetri
 *       "turbidity_src" = "PROBE". Semua hal lain TIDAK
 *       berubah dari v7.2.2.
 *  v7.2.2 = v7.2.1 + SUMBER KEKERUHAN KEMBALI KE KANAL A
 *       (satu isu, atas perintah user 3 Nov sore): mode D
 *       v7.2.0 ditinggalkan; klasifikasi level, deteksi fault
 *       analog v7.0.3, sub-indeks WQI linear tegangan, dan
 *       telemetri "turbidity_src" dipulihkan PERSIS seperti
 *       v7.1.9 (sumber A di jalur divider yang sama, GPIO1).
 *       Saksi digital g_turbDig tetap dibaca untuk baris [T]
 *       tetapi tidak lagi menentukan apa pun. Semua hal lain
 *       (termasuk pengerasan suhu v7.2.1) TIDAK berubah.
 *  v7.2.1 = v7.2.0 + PENGERASAN BACA SUHU DS18B20 + DIAGNOSTIK
 *       BUS [SUHU] (satu isu: sejak 3 Nov sore g_tempC membeku
 *       di -127,0 = DEVICE_DISCONNECTED di SEMUA baris [T],
 *       padahal pagi harinya sehat 29-31 C dan kode suhu tidak
 *       berubah - tersangka putus sambungan fisik salah satu
 *       joint DS18B20). Perubahan HANYA di jalur suhu:
 *       (1) PEMULIHAN dalam siklus yang sama: bila baca selesai
 *       dengan -127, pollTemperature menjalankan enumerasi
 *       ulang bus (tempSensor.begin()) lalu meminta SATU
 *       konversi + baca ulang mengikuti pola asinkron yang
 *       sudah ada (request -> tunggu TEMP_CONV_WAIT_MS ->
 *       baca) - maks SATU kali percobaan per siklus baca;
 *       g_lastTempReq tidak digeser agar irama
 *       TEMP_READ_INTERVAL_MS tetap; hasil pulihan dipakai
 *       normal bila berhasil. (2) DIAGNOSTIK [SUHU] di Serial,
 *       dibatasi pada transisi keadaan dan percobaan
 *       pemulihan (TIDAK tiap siklus): enumerasi menemukan
 *       perangkat -> cetak jumlah + ROM perangkat pertama
 *       (hex); enumerasi menemukan 0 perangkat -> cetak bus
 *       1-Wire kosong; bacaan pertama yang berhasil sesudah
 *       episode gagal -> cetak pulih sekali. Keadaan bus
 *       dilacak (g_suhuBusState) agar transisi yang sama
 *       tidak dicetak berulang. Perilaku baca valid, irama
 *       baca, format baris [T], dan field telemetri TIDAK
 *       berubah; tidak ada perubahan di kekeruhan mode D,
 *       level, hujan, MPU, motor, SD, AI, WiFi, atau peta
 *       pin; APK pasangan tetap v1.6.
 *  v7.2.0 = v7.1.9 + SUMBER KEKERUHAN PINDAH KE KELUARAN DIGITAL D
 *       (fakta lapangan 3 Nov sore: kanal analog A adaptor TS-300B
 *       MATI - 0,02 V terukur multimeter di pin A adaptor sendiri
 *       dengan LED daya adaptor menyala; keluaran D komparator
 *       adaptor masih berkeadaan tegas, dan user sudah menyambung
 *       D ke jalur yang sama: D -> divider 10k/20k -> GPIO1).
 *       Perubahan: (1) level kekeruhan kini ditentukan dari
 *       g_turbDig (digitalRead TURBIDITY_PIN) dengan polaritas
 *       terdokumentasi modul - dig 1 (D HIGH, di bawah ambang
 *       trimpot) = "jernih", dig 0 (D LOW, di atas ambang) =
 *       "keruh"; mode D hanya dua keadaan, tidak ada "sedang";
 *       (2) fault analog (pin A < TURB_FAULT_V, v7.0.3)
 *       DIPENSIUNKAN - g_turbFault selalu false (variabel dan
 *       field telemetri turbidity_fault dipertahankan demi
 *       kompatibilitas); (3) sub-indeks kekeruhan WQI menjadi
 *       BINER dari keadaan D - jernih 100, keruh 0 - dan fault
 *       kekeruhan tidak lagi memaksa WQI -1 (syarat suhu tidak
 *       valid tetap berlaku); (4) pemicu alert "keruh" mengikuti
 *       level seperti sebelumnya, kini dari D (tanpa perubahan
 *       struktur); (5) telemetri menambah field "turbidity_src"
 *       = "D" agar sumber bacaan bisa diaudit. Saksi analog
 *       (g_turbRaw/g_turbMv/g_turbV/g_turbPct) TETAP dibaca dan
 *       ditampilkan apa adanya - pada mode D, g_turbV/g_turbPct
 *       sekadar cerminan level D di jalur divider, bukan lagi
 *       tegangan pin A. KALIBRASI = prosedur ambang trimpot
 *       (lihat CARA_PASANG_v720.txt): probe di air jernih, putar
 *       trimpot sampai tampilan berbalik "jernih", lalu celup
 *       air keruh / tutup garpu harus berbalik "keruh". Peta
 *       pin TIDAK berubah (TURBIDITY_PIN tetap 1, LEAK_PIN
 *       tetap 3); APK pasangan tetap v1.6.
 *  v7.1.9 = v7.1.8 + PIN DIKEMBALIKAN + SATU BACAAN TAMBAHAN
 *       (permintaan user: pakai cara baca sketch tutorial
 *       YouTube yang ia kirim - turbidity_sensor.ino - dan
 *       kembalikan turbidity ke GPIO1): (1) TURBIDITY_PIN
 *       3->1 dan LEAK_PIN 1->3 - pertukaran v7.1.8 DIBATALKAN,
 *       peta pin kembali persis v7.1.7, jadi ujung kabel WAJIB
 *       ditukar BALIK secara fisik (simpul divider -> GPIO1,
 *       kabel S level -> GPIO3); (2) bacaan tambahan
 *       g_turbPct: tegangan pin A (g_turbV) diskalakan ke raw
 *       ala sketch (V/5,0 x 1023) lalu dipetakan persis
 *       map(raw, 0, 640, 100, 0) sketch itu - 640 = jernih
 *       0%, 0 = 100% - diklem 0..100. Sifatnya INFORMATIF:
 *       tampil sebagai field baru "turbidity_pct" di JSON
 *       /telemetry dan segmen "pct" di baris [T]; kategori
 *       jernih/sedang/keruh, ambang TURB_*, deteksi fault,
 *       WQI, dan seluruh perilaku lain TIDAK berubah; APK
 *       pasangan tetap v1.6.
 *  v7.1.8 = v7.1.7 + SATU PERUBAHAN PIN SENSOR (permintaan
 *       user: cari solusi agar turbidity bekerja TANPA membeli
 *       modul tambahan): pin TURBIDITY dan LEVEL AIR ditukar
 *       perannya di #define - TURBIDITY_PIN 1->3 dan LEAK_PIN
 *       3->1. TIDAK ada perubahan logika lain: readSensors,
 *       klasifikasi, WQI, alert, pertukaran pin motor
 *       v7.1.7, dan seluruh perilaku identik v7.1.7; APK
 *       pasangan tetap v1.6. SYARAT PASANG: ujung kabel kedua
 *       sensor WAJIB ditukar secara fisik di sisi ESP -
 *       simpul divider turbidity pindah ke posisi GPIO3 dan
 *       kabel S level air pindah ke posisi GPIO1 - lihat
 *       CARA_PASANG_v718.txt. Cara baca hasil uji: dashboard
 *       menampilkan nilai kekeruhan dari pin baru; bacaan
 *       level air mentah kini datang dari GPIO1.
 *  v7.1.7 = v7.1.6 + SATU EKSPERIMEN DIAGNOSTIK: peran pin
 *       IN motor DITUKAR di #define - MOTOR_L_IN1 38->39,
 *       MOTOR_L_IN2 39->38, MOTOR_R_IN1 40->41, MOTOR_R_IN2
 *       41->40 - atas permintaan user ("benerin lah
 *       dikodenya"). TIDAK ada perubahan lain: driveSide
 *       (termasuk skema anti-phase v7.1.6), setMotors, slew
 *       soft-start, interlock sesi SD, define SDMMC
 *       (38/39/40 tetap), EEP GPIO45, dan seluruh
 *       diagnostik identik v7.1.6. Semua jalur
 *       attach/detach/init motor memakai array pins[4]
 *       yang dibangun dari keempat define ini, jadi
 *       himpunan pin yang dikelola tetap {38,39,40,41};
 *       yang bertukar hanya PERAN logisnya - sinyal
 *       channel A kini keluar di pin fisik yang sebelumnya
 *       membawa channel B, dan sebaliknya. Latar: perintah
 *       B terbukti sampai (baris [MOTOR] target negatif)
 *       dan IN2/IN4 terukur 3,3 V di pin driver saat B
 *       ditahan, tetapi roda malah berputar maju - uji ini
 *       memisahkan dugaan "pemetaan pin tertukar" dari
 *       "jalur fisik tidak menghantar / sisi keluaran".
 *       Cara baca hasil uji (roda bebas, posisi pengamatan
 *       SAMA seperti uji-uji sebelumnya): (1) tombol F kini
 *       memutar roda ke arah MUNDUR dan tombol B ke arah
 *       MAJU -> drivetrain sehat dua arah; yang tertukar
 *       selama ini adalah pemetaan pin, dan pertukaran ini
 *       menjadi pemetaan permanen (perbaikan kode tuntas);
 *       (2) TIDAK ada yang berubah (F tetap maju, B tetap
 *       maju seperti sebelumnya) -> kabel IN tidak pergi
 *       ke pin yang dikira define ini; (3) perilaku lain
 *       apa pun -> laporkan apa adanya. APK pasangan tetap
 *       v1.6.
 *  v7.1.6 = v7.1.5 + SATU ISU: skema penggerak MUNDUR diganti
 *       ke ANTI-PHASE (eksperimen A/B atas usul user: "salin
 *       yang maju tapi arusnya dibalik"). Hanya cabang negatif
 *       driveSide() yang berubah: sebelumnya PWM ditaruh di
 *       channel B (IN2/IN4) dengan channel A dinolkan; kini
 *       channel B ditahan HIGH penuh (255 - level DC tetap)
 *       dan PWM berjalan di channel A (IN1/IN3), jalur yang
 *       TERBUKTI menghantar untuk maju, dengan duty terbalik
 *       (255 - duty) sehingga tegangan rata-rata ke motor
 *       = -|speedPct|%: mundur dengan besar yang sama seperti
 *       skema lama. Cabang >= 0 (maju & berhenti) TIDAK
 *       berubah satu karakter pun; slew soft-start menulis
 *       lewat driveSide() sehingga otomatis ikut skema baru;
 *       motorForceStopNow tetap berakhir semua channel 0
 *       lewat cabang >= 0; interlock sesi SD tidak terpengaruh.
 *       Latar: baris [MOTOR] v7.1.3 sudah membuktikan perintah
 *       B sampai ke firmware (target L=-70 R=-70) tetapi roda
 *       tetap diam pada skema lama - PWM di IN2/IN4 tidak
 *       menggerakkan motor. Cara baca hasil uji: mundur
 *       BEKERJA -> skema anti-phase dipertahankan; tombol B
 *       malah menggerakkan MAJU -> IN2/IN4 tidak menghantar
 *       sama sekali (level HIGH tetapnya pun tidak sampai,
 *       input driver tertarik LOW oleh pull-down internalnya);
 *       tetap DIAM -> level HIGH pun tidak sampai - keduanya
 *       menunjuk kabel/solder IN2/IN4, bukan kode. Diagnostik
 *       tiga saksi turbidity v7.1.5, pengerasan SD v7.1.4,
 *       dan semua perilaku lain utuh. APK pasangan tetap v1.6.
 *  v7.1.5 = v7.1.4 + DIAGNOSTIK TURBIDITY TIGA SAKSI
 *       (NOL perubahan perilaku - klasifikasi, WQI, dan
 *       alert tetap memakai nilai yang sama persis dari
 *       jalur raw seperti sekarang; paket ini hanya
 *       menambah dua bacaan saksi pada pin yang sama,
 *       tercetak di baris [T] yang sama, untuk menguji
 *       tuduhan bahwa bacaan turbidity 0,00 V / raw 0 di
 *       GPIO1 adalah kesalahan kode). readSensors() kini
 *       membaca GPIO1 lewat TIGA jalur independen tiap
 *       siklus: (1) raw = analogRead() (sudah ada sejak
 *       v7.1.3, disimpan di g_turbRaw, TIDAK berubah);
 *       (2) mv = analogReadMilliVolts() - jalur kalibrasi
 *       eFuse Arduino (global baru g_turbMv); (3) dig =
 *       digitalRead() - buffer digital pin (global baru
 *       g_turbDig; sinyal analog sehat +-2,8 V di pin
 *       sesudah pembagi WAJIB terbaca HIGH/1 di sini).
 *       Segmen turbidity baris [T] menjadi "turb <V> V
 *       raw <n> mv <n> dig <0|1> (<level>)". TIDAK ada
 *       pinMode baru; telemetry JSON TIDAK berubah.
 *       Cara baca hasil uji: bila mv > 0 ATAU dig = 1
 *       sementara raw tetap 0, jalur baca kode TERBUKTI
 *       salah dan akan diganti; bila raw 0 + mv 0 +
 *       dig 0 terus, pin benar-benar menerima 0 V -
 *       adaptor tidak mengirim sinyal (suplai 5 V /
 *       kabel A / colokan probe), bukan kesalahan kode.
 *       APK pasangan tetap FIR Robot v1.6.
 *  v7.1.4 = v7.1.3 + SATU ISU: kartu SD selalu dilaporkan
 *       "tidak terpasang" saat user mengunggah berkas /
 *       membuat folder, padahal kartu tertancap dan kadang
 *       terbaca. Bukti board: sesi SD gagal INTERMITEN
 *       dengan error 0x107 (send_op_cond - kartu tidak
 *       menjawab init), sementara di kesempatan lain kartu
 *       yang sama terbaca normal. Fungsi sesi SD sudah
 *       terbukti byte-identik dengan v6.9.9 yang bekerja,
 *       jadi logika sesi TIDAK diubah sama sekali - paket
 *       ini hanya MENGERASKAN inisialisasi mount di dalam
 *       sdSessionBegin() dengan percobaan ulang:
 *       (1) mount dicoba maks 3 kali dalam satu sesi;
 *       sebelum SETIAP percobaan ada delay(150) agar
 *       pull-up sempat menstabilkan bus sesudah pinMode
 *       INPUT_PULLUP / sesudah kegagalan sebelumnya;
 *       (2) percobaan 1 tetap clock maks 10 MHz seperti
 *       sebelumnya; percobaan 2-3 diturunkan ke 1 MHz
 *       (margin lebih besar untuk jalur/kontak marginal);
 *       (3) di antara percobaan yang gagal, SD_MMC.end()
 *       membersihkan state host lalu delay(300) sebelum
 *       percobaan berikutnya; (4) tiap percobaan dicatat
 *       di Serial ("[SD] mount percobaan n/3 (clock maks
 *       n MHz)") dan kegagalan total sesudah 3 percobaan
 *       dicatat eksplisit. Bila berhasil, alur sesudahnya
 *       (mkdir /fir + folder foto, baris "SD card: SIAP"
 *       hanya saat transisi !g_sdReady) persis seperti
 *       sebelumnya. Mutex sesi, pasangan begin/end 1:1,
 *       urutan interlock (stopMotors -> motorSleep ->
 *       detach -> setPins -> pull-up -> mount),
 *       sdSessionEnd, pin SD, dan seluruh bagian lain
 *       firmware (motor, WiFi, sensor, WQI/alert, AI,
 *       endpoint, dashboard, telemetry JSON, baris
 *       [T]/[MOTOR]) TIDAK berubah. Cara baca hasil uji:
 *       unggah berkas / buat folder - berhasil di
 *       percobaan 1-3 = teratasi; tiga-tiganya gagal
 *       0x107 = kartu/slot mati secara listrik dan uji
 *       kartu lain adalah hakim terakhir. APK pasangan
 *       tetap FIR Robot v1.6.
 *  v7.1.3 = v7.1.2 + DIAGNOSTIK MURNI (NOL perubahan
 *       perilaku - hanya dua penanda log Serial baru,
 *       dipasang agar dua tuduhan "salah kode" bisa
 *       dibuktikan dari board, bukan dari pendapat):
 *       (1) BARIS [MOTOR] - motorCommand() kini mencetak
 *       SATU baris tiap perintah diterima: cmd, spd, dan
 *       target L/R persis yang diteruskan ke setMotors
 *       (B -> L=-spd R=-spd; S/default -> L=0 R=0).
 *       Penanda ditaruh HANYA di motorCommand - bukan di
 *       setMotors/stopMotors - agar tidak spam dari
 *       watchdog berhenti & interlock sesi SD. Cara baca:
 *       baris "[MOTOR] cmd=B ... L=-70 R=-70" tampil
 *       tetapi roda diam = sinyal mundur TERBUKTI keluar
 *       dari program; masalahnya di kabel IN2/IN4
 *       DRV8833 atau sambungannya, bukan di kode.
 *       (2) RAW TURBIDITY DI [T] - readSensors()
 *       menyimpan raw ADC GPIO1 (dari analogRead yang
 *       SAMA, tanpa baca ulang) ke global baru
 *       g_turbRaw; baris [T] kini menampilkan
 *       "turb <V> V raw <n> (<level>)". Cara baca: raw
 *       naik dari 0 saat probe dicelup = tegangan
 *       sampai ke GPIO1; raw tetap 0 = tegangan memang
 *       tidak sampai ke pin (kabel A / suplai 5V /
 *       colokan probe), bukan rumus yang salah.
 *       Motor, slew, WiFi, sensor, WQI/alert, AI, SD,
 *       endpoint, dashboard, dan telemetry JSON TIDAK
 *       berubah perilakunya sama sekali. APK pasangan
 *       tetap FIR Robot v1.6.
 *  v7.1.2 = v7.1.1 + SATU ISU: WiFi robot hilang saat
 *       kendali motor dipakai (laporan user: motor bisa
 *       sebentar, lalu WiFi hilang). Dua mitigasi kode,
 *       keduanya menargetkan gejala tunggal itu:
 *       (1) MODEM SLEEP WIFI DIMATIKAN - startWiFi() kini
 *       WiFi.setSleep(false). setSleep(true) dulu WAJIB
 *       demi koeksistensi WiFi+Bluetooth (akar reboot loop
 *       v7.0/v7.0.1); sejak BLE dihapus total di v7.1.0
 *       alasan itu hilang, dan setSleep(false) adalah
 *       konfigurasi era WiFi-only v6.9.x yang terbukti
 *       stabil untuk AP + dashboard. Logika STA/AP/
 *       timeout/fallback tidak berubah sama sekali.
 *       (2) SOFT-START MOTOR (slew rate, non-blocking) -
 *       setMotors() kini hanya menetapkan TARGET (kalibrasi
 *       SWAP/INVERT/TRIM tetap diterapkan di situ);
 *       penulisan LEDC dipindah ke langkah slew di dalam
 *       pollMotors(): tiap >=3 ms persen terapan bergeser
 *       menuju target - naik maks 1 poin/langkah (0->100%
 *       dalam +-300 ms), turun maks 4 poin/langkah
 *       (berhenti penuh <=75 ms), perubahan arah berhenti
 *       tepat di 0 dulu. Hantaman arus start seketika
 *       0->70% hilang; motor menyala melunak. Semantik
 *       perintah, watchdog, dan sesi SD tidak berubah;
 *       khusus batas sesi SD & initMotors, duty 0 tetap
 *       diterapkan SEKETIKA lewat motorForceStopNow()
 *       (perilaku lama persis - urutan interlock utuh).
 *       CATATAN JUJUR: bila WiFi MASIH hilang sesudah ini,
 *       penyebabnya di luar jangkauan kode - baris [Boot]
 *       alasan reset sesudah kejadian adalah penentunya
 *       (BROWNOUT = jalur daya melemah saat motor menarik
 *       arus, bukan perangkat lunak). Kontrak /motor,
 *       telemetry JSON, dan endpoint lain tidak berubah.
 *       APK pasangan tetap FIR Robot v1.6.
 *  v7.1.1 = v7.1.0 + SATU ISU: bacaan sensor hujan tidak
 *       pernah berubah ("Tidak hujan" terus, basah maupun
 *       kering). Akar cacat dari audit kode v7.1.0: setup()
 *       memakai pinMode(RAIN_PIN, INPUT) polos untuk GPIO42,
 *       padahal keluaran DO modul hujan LM393 bersifat
 *       open-collector - tanpa pull-up pin bisa mengambang
 *       dan terkunci di HIGH, dan karena RAIN_ACTIVE_LOW
 *       true, HIGH berarti "tidak hujan" selamanya. Obat:
 *       (1) pinMode(RAIN_PIN, INPUT_PULLUP) di setup();
 *       (2) readSensors() kini membaca pin SATU kali ke
 *       global baru g_rainRaw, lalu g_rain diturunkan dari
 *       nilai itu (logika RAIN_ACTIVE_LOW tidak berubah);
 *       (3) baris Serial [T] menampilkan level mentah DO -
 *       "hujan YA (DO 0)" / "hujan tidak (DO 1)" - agar
 *       diagnosis di board tidak lagi menebak. Kontrak
 *       telemetry JSON TIDAK berubah (tidak ada field
 *       baru). Satu isu saja - WiFi, AI, MPU, WQI/alert,
 *       motor, SD, endpoint, dan dashboard tidak disentuh.
 *       APK pasangan tetap FIR Robot v1.6.
 *  v7.1.0 = v7.0.9 + SATU ISU: BLUETOOTH (BLE) DIHAPUS
 *       SELURUHNYA - firmware kini WiFi-only, atas keputusan
 *       eksplisit user ("ble nya hilangin aja jadiin cuma wifi
 *       aja, susah kalo ama ble"). Yang hilang: include BLE,
 *       initBLE() + advertising "FIR-Robot", layanan GATT
 *       (telemetri/perintah/hasil AI), antrean perintah BLE,
 *       bleTick(), notify telemetri & hasil AI lewat BLE,
 *       parameter koneksi v7.0.9 (updateConnParams dkk), dan
 *       gerbang !g_bleConnected di watchdog WiFi (kondisi
 *       watchdog kembali ke bentuk v7.0.8: mode STA && tidak
 *       tersambung). Yang TIDAK berubah: blok WiFi persis
 *       seperti sekarang (termasuk WiFi.setSleep(true) dan
 *       logika STA/AP), telemetryJson() byte-per-byte, semua
 *       endpoint, motor + watchdog motor, MPU, WQI/alert,
 *       SD, dan logika AI termasuk aiguard. platformio.ini
 *       TIDAK diubah (ketiga flag -D tetap, termasuk
 *       EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=0). Aplikasi
 *       pendamping kini memakai jalur WiFi saja. APK
 *       pasangan FIR Robot v1.6.
 *  v7.0.9 = v7.0.8 + SATU ISU: stabilitas koneksi BLE (sesi
 *       Bluetooth di HP putus-putus; perintah motor hilang di
 *       tengah sesi). Tiga perubahan, semuanya di blok BLE +
 *       satu gerbang watchdog WiFi:
 *       (1) Petunjuk interval koneksi yang diiklankan
 *       dilonggarkan: setMinPreferred 0x06 -> 0x18 dan
 *       setMaxPreferred 0x12 -> 0x28 (7,5-22,5 ms -> 30-50 ms,
 *       kisaran standar). CATATAN JUJUR: bukti bahwa nilai
 *       serapat warisan v7.0.4 itu sendiri menyebabkan putus
 *       di Android itu TIPIS - Android umumnya mengabaikan
 *       petunjuk advertising - jadi pelonggaran ini pengaman
 *       murah, bukan satu-satunya obat.
 *       (2) Parameter koneksi AKTIF diminta ulang SATU kali
 *       +-5 dtk sesudah onConnect: min 24, max 40 (30-50 ms),
 *       latency 0, timeout supervision 400 (4 dtk), lewat
 *       BLEServer::updateConnParams yang memang tersedia di
 *       core terpasang (BLEServer.h baris 77). Alamat peer
 *       ditangkap dari overload onConnect(BLEServer*,
 *       esp_ble_gatts_cb_param_t*) - overload yang benar-benar
 *       dipanggil core (BLEServer.cpp dispatch
 *       ESP_GATTS_CONNECT_EVT memanggil kedua overload;
 *       alamat di param->connect.remote_bda) - dan permintaan
 *       dieksekusi dari bleTick() (konteks loop), BUKAN dari
 *       dalam callback BT.
 *       (3) Watchdog WiFi di loop() kini MELEWATI
 *       WiFi.reconnect() pada siklus mana pun saat sesi BLE
 *       sedang aktif (g_bleConnected), tanpa log tambahan.
 *       Reconnect STA berkala adalah penyebab putus BLE yang
 *       TERDOKUMENTASI: radio 2,4 GHz tunggal dipakai
 *       bergantian WiFi/BT (esp-idf issue #15052/#17871).
 *       MPU/AI (termasuk flag ESP-NN v7.0.8)/WQI/alert/motor/
 *       SD/endpoint/dashboard TIDAK berubah. APK pasangan
 *       FIR Robot v1.5.
 *  v7.0.8 = v7.0.7 + SATU ISU: kernel ESP-NN pada model Edge
 *       Impulse DIMATIKAN sebagai obat CORRUPT HEAP inferensi.
 *       Bukti board user (dump v7.0.4, terdekode): inferensi
 *       selesai dan run_classifier kembali OK, tetapi reset
 *       model sesudahnya mendeteksi CORRUPT HEAP "Bad tail" -
 *       canary heap berubah HANYA 1 byte teratasnya
 *       (0xbaad5678 -> 0x68ad5678). Pola tulis-di-luar-batas
 *       beberapa byte di ekor alokasi ini cocok dengan bug
 *       kernel konvolusi ESP-NN khusus ESP32-S3 yang
 *       dilaporkan di github.com/espressif/esp-nn issue #7
 *       (dilaporkan engineer Edge Impulse) dan #8 (penulisan
 *       byte padding esp_nn). Panduan staf Edge Impulse di
 *       forum resmi: nonaktifkan ESP-NN lewat
 *       EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN 0 di konfigurasi
 *       agar inferensi berjalan dengan kernel referensi
 *       TFLite (lebih lambat, tapi bersih). Mekanisme yang
 *       dipakai DI SINI: define itu dijaga #ifndef di
 *       ei_classifier_config.h pustaka (baris 82) dan komentar
 *       header itu sendiri menganjurkan override lewat -D,
 *       jadi obatnya SATU baris build_flags di platformio.ini:
 *       -D EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=0. Karena
 *       platformio.ini user TIDAK boleh ditimpa paket, langkah
 *       tambah baris manual ada di CARA_PASANG_v7.0.8.txt
 *       (pola yang sama seperti build_flags v6.9.9 dulu).
 *       Bantalan alokasi EI_ALLOC_PAD_BYTES 256 dari v7.0.5
 *       TETAP dipertahankan utuh sebagai pengaman dan tidak
 *       diubah, supaya uji ini bersih: satu-satunya variabel
 *       yang berubah adalah kernel inferensi. Akibat yang
 *       diharapkan: inferensi lebih lambat - ai_ms di Serial/
 *       telemetri adalah pembandingnya (catat ai_ms v7.0.7
 *       vs v7.0.8). Cara baca hasil uji: Analisis ditekan
 *       berulang DAN mode otomatis dibiarkan beberapa menit
 *       TANPA satu pun CORRUPT HEAP dan board tidak reboot =
 *       ESP-NN memang pelakunya; bila CORRUPT HEAP tetap
 *       muncul dengan ESP-NN mati, tersangka berpindah ke
 *       pengalokasi PSRAM proyek dan dump baru diperlukan.
 *       MPU/WiFi/BLE/WQI/alert/motor/SD/endpoint/dashboard
 *       TIDAK berubah. APK pasangan FIR Robot v1.5.
 *  v7.0.7 = v7.0.6 + SATU ISU: cara deteksi MPU6050 (perintah
 *       user: cari source di GitHub untuk masalah MPU ini).
 *       Bukti board user di v7.0.3-v7.0.6: perangkat menjawab
 *       di 0x68, tulis bangun 0x6B=0x00 selalu sukses (err68=0),
 *       AD0 benar GND, tetapi WHO_AM_I (0x75) terbaca KONSISTEN
 *       114 (0x72) - dan kode sejak v7.0.3 menyatakan TIDAK
 *       TERDETEKSI hanya karena WHO_AM_I != 0x68, sesudah itu
 *       readTilt() tidak pernah dicoba lagi. Padahal firmware
 *       era v6 (tanpa cek WHO_AM_I) pernah menghasilkan
 *       roll/pitch wajar di board yang sama. Hasil riset
 *       GitHub: 0x72 adalah tanda yang dikenal untuk chip
 *       MPU6050 palsu/remark - sebagian sampel register
 *       datanya bekerja normal, sebagian tidak. Chip user
 *       SUDAH DISOLDER permanen ke board, jadi deteksi kini
 *       diputuskan dari DATA, bukan dari identitas chip:
 *       mpuTryAddr() sah bila (a) tulis bangun endTransmission
 *       == 0, (b) baca-balik PWR_MGMT_1 (0x6B) == 0x00,
 *       (c) burst read aksel 6 byte dari 0x3B sukses, dan
 *       (d) magnitudo aksel mentah di rentang 0,7-1,3 g
 *       (raw 11469..21299; 16384 = 1 g). WHO_AM_I tetap dibaca
 *       dan dicatat di g_mpuWhoami untuk log, TAPI bukan lagi
 *       syarat sah. probeMPU() mencetak "MPU6050: OK (0x68)"
 *       untuk whoami 0x68, dan tetap OK dengan catatan varian
 *       untuk whoami lain (mis. "whoami 0x72 - data aksel
 *       valid"); baris gagal tetap format lama ditambah
 *       mag68 (magnitudo percobaan terakhir, -1 bila burst
 *       gagal) sebagai bukti pembeda. readTilt(), pemulihan
 *       berkala 15 detik, dan telemetri (field mpu) TIDAK
 *       berubah. WiFi/BLE/AI/WQI/alert/motor/SD/endpoint/
 *       dashboard TIDAK berubah. APK pasangan tetap FIR
 *       Robot v1.4/v1.5 - tidak ada APK baru.
 *  v7.0.6 = v7.0.5 + SATU ISU: perilaku WQI & ALERT saat sensor
 *       kekeruhan tidak merespons (perintah eksplisit user:
 *       "benerin wqi dan alertnya"). Bukti dari log board user
 *       atas v7.0.3: (a) saat fault, computeWQI() merata-rata dua
 *       sub-indeks sehat sehingga tampil WQI 100 (BAIK) padahal
 *       sensor mati - menyesatkan; (b) pada detik-detik awal boot,
 *       klasifikasi "keruh" dari tegangan 0,00 V sempat memenuhi
 *       debounce ALERT sehingga "[ALERT] AKTIF: Kekeruhan
 *       ABNORMAL" yang palsu menyala lebih dulu sebelum status
 *       fault (5 bacaan) terbentuk. Dua koreksi: (1) computeWQI()
 *       kini mengembalikan -1 (tidak valid, tampil "-") bila
 *       g_turbFault - cabang rata-rata dua parameter v7.0.3
 *       dihapus; bila tidak fault rumus lama utuh. (2)
 *       TURB_FAULT_N 5 -> 3: readSensors() berjalan sebelum
 *       evaluateAlert() dalam siklus yang sama dan debounce alert
 *       juga 3, jadi status fault terbentuk pada bacaan ke-3 yang
 *       sama - pemicu "keruh" (sudah dikondisikan !g_turbFault)
 *       tidak pernah sempat menyalakan alarm palsu; alarm yang
 *       menyala adalah pesan sensor fault yang jujur (perilaku
 *       evaluateAlert untuk fault tidak diubah selain ini).
 *       MPU, BLE, AI/inferensi, motor, SD, endpoint, dashboard
 *       TIDAK berubah; kontrak telemetri tidak berubah (field
 *       turbidity_fault tetap ada). APK pasangan tetap FIR Robot
 *       v1.3/v1.4 - tidak ada APK baru.
 *  v7.0.5 = v7.0.4 + SATU PERBAIKAN TUNGGAL di jalur alokasi
 *       inferensi AI (satu paket = satu isu, sesuai permintaan
 *       user). Bukti: dump crash board 3 Nov didekode via
 *       addr2line terhadap ELF lokal v7.0.4 - sesudah "[AI]
 *       inferensi mulai" muncul "CORRUPT HEAP: Bad tail at
 *       0x3d9af6a1. Expected 0xbaad5678 got 0x68ad5678" lalu
 *       assert di multi_heap_free; rantai: aiTaskFn ->
 *       runAiAnalysis (baris pemanggilan reset) ->
 *       tflite_learn_1154536_5_reset -> ei_aligned_free ->
 *       ei_free -> free. Kesimpulan: run_classifier SELESAI
 *       berjalan; planner model menulis beberapa byte melewati
 *       ujung arena tensor (canary ekor berubah hanya 1 byte di
 *       posisi terjauh) dan panggilan reset mendeteksinya saat
 *       free. Obat: override ei_malloc/ei_calloc kini menambah
 *       bantalan EI_ALLOC_PAD_BYTES (256 byte) pada SETIAP
 *       alokasi EI (kedua jalur PSRAM & fallback; ei_calloc
 *       menolkan seluruh wilayah termasuk bantalan); ei_free
 *       tidak berubah. Kontrak BLE, endpoint, MPU, turbidity/
 *       WQI/alert, motor, SD, dashboard, dan perilaku lain
 *       TIDAK berubah. APK pasangan tetap FIR Robot v1.3 -
 *       tidak ada APK baru.
 *  v7.0.4 = v7.0.3 + DUA PERBAIKAN DARI BUKTI LOG BOARD (boot
 *       #468 v7.0.3): (1) MPU6050 - pindai bus MENEMUKAN perangkat
 *       di 0x68, tulis bangun sukses (err68=0), AD0 benar LOW
 *       (err69=2), TETAPI bacaan WHO_AM_I pola repeated-start
 *       (endTransmission(false) + requestFrom) gagal di level
 *       driver: "[Wire.cpp:499] requestFrom():
 *       i2cWriteReadNonStop returned Error -1" dan byte terbaca
 *       sampah (whoami68=114 = 0x72, seharusnya 0x68). Obat:
 *       SEMUA bacaan register MPU (WHO_AM_I di probe, aksel di
 *       readTilt) kini transaksi TERPISAH STOP -
 *       endTransmission(true) lalu requestFrom terpisah; tidak
 *       ada lagi endTransmission(false) di jalur MPU. Probe kini
 *       mencoba sampai 3x per alamat (jeda 2 ms); cetakan hasil
 *       akhir tetap format v7.0.3 memakai nilai percobaan
 *       terakhir; pemulihan berkala 15 detik dipertahankan.
 *       (2) BLE sulit ditemukan aplikasi: advertising kini
 *       eksplisit (UUID layanan + scan response, yang sudah ada)
 *       ditambah interval koneksi pilihan - setMinPreferred
 *       (0x06) / setMaxPreferred (0x12) - sebelum
 *       startAdvertising; nama tetap "FIR-Robot" dari
 *       BLEDevice::init. Kontrak BLE (UUID/perintah/telemetri),
 *       endpoint, motor, SD, AI, turbidity, dan dashboard TIDAK
 *       berubah. APK pasangan = FIR Robot v1.3, dikirim bersamaan.
 *  v7.0.3 = v7.0.2 + PERBAIKAN HASIL AUDIT TIGA GEJALA SISA
 *       (v7.0.2 di board user sudah STABIL - loop radio mati;
 *       yang diperbaiki di sini adalah sisa bacaan yang salah):
 *       (A) MPU6050 hilang-timbul antar-boot. Audit: initMPU lama
 *       hanya SATU transaksi tulis 0x6B tanpa tunda, tanpa
 *       setClock, tanpa cek WHO_AM_I, tanpa kode error - satu
 *       transaksi gagal sesaat berarti MPU mati satu boot penuh
 *       dan readTilt() tidak pernah mencobanya lagi. Kini:
 *       Wire.setClock(100000) + setTimeOut(50) + delay(100),
 *       PINDAI BUS I2C 0x08-0x77 (baris "[MPU] pindai I2C: ..."),
 *       probeMPU() mencoba 0x68 lalu 0x69 dengan verifikasi
 *       WHO_AM_I (0x75 harus 0x68); alamat sah disimpan di
 *       g_mpuAddr dan dipakai readTilt(); kegagalan dicetak
 *       lengkap (err68/err69 + whoami68). PEMULIHAN BERKALA:
 *       bila MPU tidak OK, probe diulang tiap 15 detik dari
 *       loop; readTilt() yang gagal transaksi 3x berturut-turut
 *       menandai MPU hilang agar pemulihan mengambil alih.
 *       (B) Kekeruhan terbaca 0,00 V terus (raw ADC ~0 di udara):
 *       itu tanda listrik sensor TIDAK sampai (kabel A / suplai
 *       5V / probe), tetapi kode lama menerjemahkannya "keruh"
 *       -> ALERT "Kekeruhan ABNORMAL" palsu + sub-indeks WQI 0.
 *       Kini: g_turbV < TURB_FAULT_V (0,05 V) sebanyak
 *       TURB_FAULT_N (5) bacaan berturut-turut -> g_turbFault:
 *       level menjadi "tidak merespons", WQI dirata-rata dari
 *       DUA sub-indeks valid saja (suhu + TDS), pemicu "keruh"
 *       dimatikan, dan alert diganti pesan jujur "Sensor
 *       kekeruhan tidak merespons (cek kabel A / suplai 5V /
 *       probe)". Telemetri menambah field turbidity_fault dan
 *       dashboard menampilkannya sebagai klasifikasi 'bad'.
 *       (C) Baris Serial "PSRAM: found=0 total=0 free=0"
 *       menyesatkan: ESP.getPsramSize()/getFreePsram() memang 0
 *       di konfigurasi ini padahal heap PSRAM nyata bekerja
 *       (buffer AI beralamat 0x3d80xxxx). Cetakan diganti
 *       heap_caps SPIRAM (total & free yang sebenarnya).
 *       Kontrak BLE, endpoint, motor, SD, dan AI TIDAK berubah;
 *       APK pasangan TETAP FIR Robot v1.1 - tidak ada APK baru.
 *  v7.0.2 = v7.0.1 + OBAT REBOOT LOOP TAHAP RADIO (penyebab
 *       TERBUKTI dari log board user, boot #335 v7.0.1): BLE init
 *       BERHASIL ("[BLE] aktif: FIR-Robot", heap internal 142592),
 *       lalu startWiFi memicu dari driver IDF: "wifi:Error! Should
 *       enable WiFi modem sleep when both WiFi and Bluetooth are
 *       enabled!!!!!!" disusul "abort() was called" -> PANIC ->
 *       reboot -> loop. Akarnya: pengerasan v6.9.3 mematikan modem
 *       sleep WiFi (setSleep(false)), padahal koeksistensi WiFi+BT
 *       MEWAJIBKAN modem sleep menyala. Perubahan: (1) WiFi.
 *       setSleep(true) di startWiFi() - satu-satunya titik setSleep
 *       di firmware; tidak ada lagi setSleep(false). (2) Di
 *       initBLE(), BLEDevice::setMTU(512) dipindah ke SESUDAH
 *       BLEDevice::init() - urutan lama ditolak driver ("setMTU():
 *       can't set local mtu value: 512") sehingga MTU tidak
 *       terpasang. (3) ENDPOINT BARU GET /files-json?dir=<path>
 *       (bawaan "/") untuk pengelola berkas aplikasi Android:
 *       {"dir":..,"sd":true,"entries":[{"name":..,"dir":false,
 *       "size":123}]} - validasi path, sesi SD, dan pengurutan
 *       (folder dulu, alfabetis) sama persis seperti /files.
 *       Kontrak BLE & endpoint lain TIDAK berubah; APK pasangan
 *       = FIR Robot v1.1 (dikirim bersamaan).
 *  v7.0.1 = v7.0 + MITIGASI & INSTRUMENTASI TAHAP RADIO. Gejala
 *       di board: reboot berulang dengan periode +-13 detik tepat
 *       di tahap "Menyambung ke KANTOR" (nomor boot naik 23x dalam
 *       5 menit; WiFi & BLE tidak pernah muncul; MPU6050 sesekali
 *       tidak terdeteksi) - dugaan utama: rel daya USB runtuh di
 *       transisi radio dan/atau puncak inisialisasi BLE+WiFi
 *       bertumpuk. Tiga perubahan, semuanya di tahap radio:
 *       (1) initBLE() kini dipanggil TEPAT SEBELUM startWiFi()
 *       di setup() (di v7.0 sesudahnya): BLE menyala lebih dulu
 *       sehingga puncak inisialisasi kedua radio tidak lagi
 *       bertumpuk di akhir startWiFi. (2) Penanda [RADIO] +
 *       angka heap internal (heap_caps_get_free_size(
 *       MALLOC_CAP_INTERNAL)) dicetak tepat sebelum initBLE(),
 *       di awal startWiFi(), dan tepat sesudah startWiFi()
 *       kembali - kematian di tahap radio kini meninggalkan
 *       jejak baris terakhir yang terbaca beserta sisa RAM
 *       internal di titik itu. (3) WiFi.setTxPower(
 *       WIFI_POWER_13dBm) tepat sebelum WiFi.begin(): daya
 *       pancar diturunkan selama asosiasi untuk memangkas puncak
 *       arus asosiasi di bangku uji (rel daya USB); bisa
 *       dinaikkan lagi bila jangkauan STA dibutuhkan. Kontrak
 *       BLE (nama/UUID/perintah/format), endpoint, dashboard,
 *       dan APK pasangan TIDAK berubah dari v7.0.
 *  v7.0 = v6.9.9 + LAYANAN BLE (BLUETOOTH LOW ENERGY) berdampingan
 *       dengan WiFi untuk aplikasi Android pendamping. Kontrak GATT
 *       tetap: nama advertising "FIR-Robot"; service
 *       12345678-1234-5678-1234-56789abcdef0; karakteristik
 *       telemetri (...abcdef1, READ+NOTIFY, string JSON yang sama
 *       persis dengan /telemetry, diperbarui tiap siklus sensor
 *       2 dtk); perintah (...abcdef2, WRITE, teks: "M:F"/"M:B"/
 *       "M:L"/"M:R"/"M:S" [varian "M:F:180" = kecepatan skala
 *       0-255] lewat motorCommand yang sama dengan dashboard -
 *       watchdog 1,5 dtk & interlock sesi SD berlaku sama;
 *       "ANALYZE" = analisis manual lewat requestAiAnalysis;
 *       "AIAUTO:0"/"AIAUTO:1" = mode otomatis, persisten sama
 *       seperti /ai-auto); hasil AI (...abcdef3, READ+NOTIFY,
 *       "<label>:<skor>:<KANDIDAT|AMAN|GAGAL>:<ms>", awal
 *       "BELUM ADA HASIL"). Perintah TIDAK dieksekusi di callback
 *       BLE: callback hanya menyalin ke antrean cincin, loop()
 *       yang mengeksekusi. Versi ini MENGGANTIKAN & MENCAKUP
 *       v6.9.9 - termasuk patch batas luapan EI 256 di header
 *       porting ter-patch & build_flags scratch 144; dashboard
 *       WiFi dan semua fitur lama TIDAK berubah.
 *  v6.9.9 = v6.9.8 + BATAS BUFFER LUAPAN & SCRATCH EI DINAIKKAN
 *       LEWAT build_flags (akar masalah FINAL TERBUKTI dari
 *       backtrace board yang terdekode via addr2line: board mati
 *       pada analisis PERTAMA, SUDAH DI DALAM model_init -
 *       rantai aiTaskFn -> runAiAnalysis -> run_classifier ->
 *       run_classifier_image_quantized -> inference_tflite_setup
 *       -> tflite_learn_..._init -> conv Prepare ->
 *       CalculateOpDataConv ->
 *       PopulateConvolutionQuantizationParams menulis ke buffer
 *       NULL. Jadi titik cekiknya BUKAN kebocoran antar-analisis:
 *       graph ini punya puluhan layer conv yang masing-masing
 *       meminta buffer persisten 128 B saat Prepare di dalam
 *       init, sedangkan batas buffer luapan efektif hanya 30
 *       (define khusus ESP32S3 di porting/ei_classifier_
 *       porting.h; bawaan berkas compiled.cpp sendiri 10) ->
 *       AllocatePersistentBufferImpl kehabisan jatah dan
 *       mengembalikan NULL di tengah init. Obat: scratch
 *       dinaikkan lewat -D EI_MAX_SCRATCH_BUFFER_COUNT=144 di
 *       build_flags platformio.ini (define scratch dijaga
 *       #ifndef di compiled.cpp sehingga flag compiler
 *       menang), dan buffer luapan dinaikkan dengan MEMPERBAIKI
 *       define ESP32S3 di porting header library menjadi
 *       EI_MAX_OVERFLOW_BUFFER_COUNT=256 (flag -D saja TIDAK
 *       cukup untuk yang ini: define di porting header TIDAK
 *       dijaga #ifndef sehingga menimpa flag compiler -
 *       terbukti dari ukuran larik overflow_buffers di ELF:
 *       30 slot sebelum diperbaiki, 256 sesudahnya; flag -D
 *       256 tetap dicantumkan di build_flags sebagai niat &
 *       cadangan). Buffer luapan dialokasikan via ei_calloc
 *       -> override PSRAM v6.9.4, dan tetap dibersihkan tiap
 *       analisis oleh model_reset v6.9.8. TIDAK ada perubahan
 *       kode di versi ini.
 *       PENTING: build_flags WAJIB ada di platformio.ini proyek
 *       yang di-build DAN porting header library WAJIB yang
 *       sudah diperbaiki (berkasnya ikut di paket) - menimpa
 *       src/main.cpp SAJA TIDAK CUKUP, firmware akan tetap
 *       panic persis seperti sebelumnya.
 *  Pin rencana lama sensor hujan (GPIO15) = XCLK kamera - JANGAN dipakai.
 *  WAJIB verifikasi dengan board S3 CAM kalian!
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "SD_MMC.h"  // slot microSD di board (mode 1-bit) - foto bukti AI
#include <OneWire.h>
#include <DallasTemperature.h>
#include "esp_camera.h"
#include "img_converters.h"  // fmt2rgb888: JPEG kamera -> RGB mentah untuk AI
#include <esp_heap_caps.h>   // alokasi buffer AI di PSRAM
#include <esp32-hal-psram.h> // ps_malloc - jalur alokasi PSRAM bawaan Arduino
#include <esp_system.h>     // esp_reset_reason: alasan reset boot (v6.9.5)
#include <time.h>           // v7.8.0: jam kalender (time/localtime)
#include <sys/time.h>       // v7.8.0: settimeofday untuk sinkron jam dari HP
#include <esp_task_wdt.h>   // esp_task_wdt_reset di sekitar inferensi (v6.9.5)
#include <KairoTsuki-project-1_inferencing.h>  // model AI Edge Impulse (lib lokal)

// =====================================================================
// v7.8.0 (8 Nov 2026, fork dari snapshot beku v7.7.5) - WEB & WAKTU:
// 1) Jam kalender: sinkron dari HP/browser (/time-set, tanpa internet)
//    + NTP oportunistik saat STA online (pollClock). TZ WIB-7.
// 2) index.csv v2: kolom epoch ambil + epoch analisa (kompatibel mundur:
//    baris lama terbaca epoch 0 = "belum tersinkron").
// 3) Daftar foto web: grup Belum Dianalisa / Sudah Dianalisa, info hari-
//    tanggal-jam per foto, bar keyakinan, bar ringkasan + jam robot.
// 4) Panel Log di sidebar web (ring buffer peristiwa, /log).
// 5) Nama foto OTOMATIS berubah mengikuti hasil analisa
//    (<label>_<nama lama>.jpg, .rgb + indeks + cache RAM ikut).
// 6) Guard foto sampah di jalur analisa-dari-SD: JPEG harus ada dan
//    .rgb harus persis AI_THUMB_BYTES, selain itu ditolak bersih.
// APK tidak berubah (tetap v2.5) - semua perubahan ini di sisi web.
// =====================================================================
// v7.10.0: #include <Wire.h> dibuang - satu-satunya pemakai I2C Wire
// adalah MPU6050 yang sudah dihapus user dari robot.

// ============================ KONFIGURASI ===================================
#define WIFI_SSID       "NAMA_WIFI_ANDA"
#define WIFI_PASS       "PASSWORD_WIFI_ANDA"
#define WIFI_TIMEOUT_MS 10000
#define STA_REST_RETRY_MS 300000   // v7.7.1: jeda antar jendela coba STA saat istirahat
#define STA_REST_WINDOW_MS 12000   // v7.7.1: lama satu jendela coba STA
#define STA_DOWN_REST_MS 120000    // v7.7.1: STA putus selama ini -> ikut diistirahatkan
#define AP_SSID         "FIR-Robot"
#define AP_PASS         "PASSWORD_AP_ANDA"

#define ONE_WIRE_PIN   14
#define TURBIDITY_PIN  1
#define TDS_PIN        2
#define LEAK_PIN       3    // modul level air ANALOG (ADC1_CH2) - Patch_Level_Air_v5
#define LEAK_THRESHOLD 600  // SEMENTARA - wajib diganti hasil kalibrasi dua titik!
#define RAIN_PIN       42   // DO modul hujan LM393 (digital) - BUKAN GPIO15!
#define RAIN_ACTIVE_LOW true  // modul umum: DO = LOW + LED menyala saat basah; balik bila terbalik
#define BUZZER_PIN     21   // -1 = nonaktif. Buzzer AKTIF 3,3V/5V.
#define CHG_PIN        44   // v7.3.6: tap CHRG TP4056 lewat resistor seri 10k (bekas U0RXD). LOW = mengecas.

// Turbidity (set TS-300B) - peta kabel fisik probe ke papan adaptor:
//   sisi ANGKA (ke probe): 1 = KUNING (sinyal), 2 = BIRU (GND), 3 = MERAH (VCC probe), 4 = kosong.
//   sisi HURUF (ke robot): V = 5V, G = GND; A dan D pernah dipakai bergantian sampai v7.2.2.
// v7.2.3 (MODE AKTIF): A dan D TIDAK dipakai. KUNING dibaca langsung:
//   kuning -> 20k -> simpul -> GPIO1, simpul ditarik ke 3V3 lewat 10k.
//   Adaptor hanya memberi daya ke probe lewat V/G. g_turbV = tegangan
//   simpul apa adanya; simpul TURUN saat cahaya tembus (jernih).
// (Mode A lama: tegangan pin A turun saat air makin keruh, jernih
// +/-4,2 V; g_turbV = bacaan GPIO1 x TURB_VDIV_RATIO.)
#define TURB_VDIV_RATIO   1.5f
#define TURB_CLEAR_V      4.20f  // PRA-KALIBRASI: tegangan pin A di air jernih - ganti hasil kalibrasi!
#define TURB_JERNIH_MIN_V 3.40f  // PRA-KALIBRASI: >= ini = "jernih"
#define TURB_SEDANG_MIN_V 2.60f  // PRA-KALIBRASI: >= ini = "sedang"; di bawahnya = "keruh" (ambang peringatan)
#define TURB_FAULT_V      0.05f  // v7.0.3: pin A di bawah ini = listrik sensor tidak sampai (BUKAN air keruh)
#define TURB_FAULT_N      3      // v7.0.3: bacaan rendah berturut-turut sebelum sensor dinyatakan fault; v7.0.6: 5 -> 3 (sama dgn debounce alert)
#define TURB_FREEZE_N     60     // v7.9.0: median mV turbidity tidak berubah (selisih <= 1 mV) selama N siklus berturut-turut = saluran mati beku -> "tidak merespons"; menutup keadaan mati yang duduk di tegangan diam divider, bukan di rel
// v7.2.3: MODE PROBE LANGSUNG (bypass adaptor). Adaptor hanya memberi
// daya ke probe (V/G seperti biasa; A dan D TIDAK dipakai). Kabel
// KUNING probe -> resistor 20k -> SIMPUL -> GPIO1; simpul yang sama
// ditarik ke 3V3 lewat resistor 10k (pull-up). Fototransistor menarik
// simpul ke GND lewat 20k saat menerima cahaya LED: makin jernih air,
// makin banyak cahaya, makin RENDAH tegangan simpul. g_turbV di mode
// ini = tegangan simpul apa adanya (TANPA rekonstruksi divider).
#define TURB_PROBE_CLEAR_V 0.42f  // ACUAN USER (v7.2.5): garpu tak terhalang (di udara) = "jernih" (titik kerja udara terukur 0,38-0,42 V). v7.2.6: sensitivitas dinaikkan atas permintaan user - skala dipersempit dari sini
#define TURB_PROBE_DARK_V  0.85f  // v7.2.6: DARK diturunkan 1,30 -> 0,85 V agar kenaikan kecil dari acuan udara terbaca: simpul <= +/-0,53 V jernih, <= +/-0,68 V sedang, di atasnya keruh; pct kini bergerak mulai dari kenaikan +/-0,05 V di atas acuan
#define TURB_PROBE_FAULT_V 3.25f  // v7.2.5: fault HANYA bila simpul menempel di rel 3V3 penuh (jalur kuning absen total tanpa 20k ke GND). Keadaan diam 2,2 V (20k ke GND terpasang + gelap total) kini terklasifikasi "keruh", bukan fault - memisahkan "air sangat keruh" dari "kabel putus" memang tidak mungkin di rangkaian ini, dan pilihan user: keruh harus bisa tercapai

#define TDS_ALERT_PPM    500.0f // baku mutu air minum (Permenkes); kalibrasi!
#define TEMP_HIGH_ALERT  35.0f
#define TEMP_LOW_ALERT   15.0f
#define ALERT_DEBOUNCE_N 3

// --- WQI: suhu optimum perairan tropis + toleransi skor penuh ---
#define WQI_TEMP_OPT   28.0f  // asumsi, kalibrasi ulang di lokasi uji!
#define WQI_TEMP_TOL   2.0f   // |T-28| <= 2 C -> sub-indeks suhu = 100

// v7.10.0: define pin MPU6050 dibuang bersama sensornya (dihapus user).

// DRV8833 (modul MJKDZ): VCC -> OUT MT3608 5V langsung, kabel sendiri
// (keputusan user 5 Nov malam: VCC TIDAK dipindah lagi; perbaikan
// hanya lewat kode). ULT tidak disambung.
// EEP (nSLEEP) -> GPIO45 sejak v6.9.1: dikendalikan firmware (LOW =
// driver tidur & output H-bridge mati total; HIGH = bangun). Dahulu
// terikat 3V3 (selalu bangun). IN dari GPIO:
#define MOTOR_EEP_PIN  45    // EEP (nSLEEP) DRV8833 - kendali tidur driver
#define MOTOR_L_IN1    38    // IN1 - motor kiri  (v7.4.0: kembali lurus v7.1.2)
#define MOTOR_L_IN2    39    // IN2 - motor kiri  (v7.4.0: kembali lurus v7.1.2)
#define MOTOR_R_IN1    40    // IN3 - motor kanan (v7.4.0: kembali lurus v7.1.2)
#define MOTOR_R_IN2    41    // IN4 - motor kanan (v7.4.0: kembali lurus v7.1.2)
#define MOTOR_WATCHDOG_MS 1500  // cadangan: motor berhenti 1,5 dtk bila perintah STOP dari web hilang

// --- KALIBRASI ARAH MOTOR (tentukan dari uji 4 tombol, lihat panduan) ---
// Robot MUNDUR saat tombol maju ditekan -> set DUA-DUANYA true.
// Robot MUTER di tempat saat tombol maju -> set true hanya sisi yang
//   rodanya berputar mundur.
// Tombol KIRI/KANAN tertukar (tekan kiri malah belok kanan) -> SWAP true.
// Satu roda lebih cepat (robot membelok sendiri saat maju lurus)
//   -> turunkan TRIM sisi yang lebih cepat (mis. 90).
#define MOTOR_L_INVERT  false
#define MOTOR_R_INVERT  false
#define MOTOR_SWAP_LR   false
#define MOTOR_L_TRIM    100   // persen kecepatan efektif roda kiri
#define MOTOR_R_TRIM    100   // persen kecepatan efektif roda kanan

// --- AI kandidat korosi (Edge Impulse, v6.8) ---
#define AI_RUST_THRESHOLD 0.70f  // skor kelas "karat" >= ini -> "kandidat korosi"
#define AI_LOG_MAX_BYTES  100000UL
#define AI_LOG_KEEP_LINES 800
#define AI_AUTO_INTERVAL_MS 5000UL    // jeda analisis pada mode otomatis (CV)
#define AI_FOTO_COOLDOWN_MS 60000UL   // foto kandidat ke SD: maks 1 per menit

// --- SD card (slot microSD di board) ---
// PIN SLOT SD TERVERIFIKASI dari pinout resmi board (gambar pinout
// penjual): SD_CMD = GPIO38, SD_CLK = GPIO39, SD_DATA/D0 = GPIO40 -
// PERSIS pin motor IN1/IN2/IN3 (hanya IN4/GPIO41 yang tidak berbagi).
// Karena itu SD dipakai lewat SESI TERKUNCI (sdSessionBegin/End)
// bergantian dengan motor - tidak pernah mounted permanen.
// Mode SDMMC 1-bit: setPins(clk, cmd, d0) sesuai header SD_MMC
// core Arduino 2.x (urutan argumen: clk, cmd, d0).
#define SD_PINS_KNOWN 1
#define SDMMC_CLK_PIN 39
#define SDMMC_CMD_PIN 38
#define SDMMC_D0_PIN  40
const char* SD_FOTO_DIR = "/fir/foto";   // folder foto bukti di SD card
const char* AI_SET_PATH = "/aiset.txt";  // saklar mode otomatis (persisten)
const char* TURB_OVR_PATH = "/turbovr.txt";  // v7.10.0: override klasifikasi turbidity dari /adminin (persisten)
int g_turbOverride = 0;   // v7.10.0: 0 = otomatis (ikut sensor), 1 = jernih, 2 = sedang, 3 = keruh
const char* AI_GUARD_PATH = "/aiguard.txt";  // penanda crash inferensi (v6.9.4)

#define TELEMETRY_INTERVAL_MS 2000
#define TEMP_READ_INTERVAL_MS 3000
#define TEMP_CONV_WAIT_MS     800

#define LOG_INTERVAL_MS   60000UL
#define RAM_LOG_CAP       240
#define LOG_MAX_BYTES     150000UL
#define LOG_KEEP_LINES    1500
const char* LOG_PATH = "/firlog.csv";
const char* LOG_TMP  = "/firlog.tmp";
const char* LOG_HEADER = "ts,temp_c,turbidity_v,tds_ppm,leak,wqi";   // v7.10.0: kolom roll,pitch dibuang (gyro dihapus)
const char* AI_LOG_PATH = "/aidet.csv";
const char* AI_LOG_TMP  = "/aidet.tmp";
const char* AI_LOG_HEADER = "boot,ts,kelas,skor_kelas,karat,cat_coklat,logam_bersih,lumut_kerak,kandidat,ms";
const char* BOOT_PATH = "/boot.txt";  // nomor sesi boot persisten (naik tiap robot menyala)
const char* g_bootRstTag = "LAIN";  // v7.3.8: tag singkat alasan reset boot ini - diisi di awal setup, ditulis ke baris #BOOT di AI_LOG_PATH

// ============================ PIN KAMERA ====================================
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// ============================ STATE GLOBAL ==================================
WebServer server(80);
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);

float    g_tempC = -127.0;
float    g_turbV = 0.0;
int      g_turbRaw = 0;   // v7.1.3: raw ADC turbidity (GPIO1) - tampil di baris [T] untuk diagnosis; v7.3.5: kini raw-SETARA dari median mV burst yang sama
int      g_turbMv = 0;    // v7.1.5: saksi 2 - analogReadMilliVolts(GPIO1), jalur kalibrasi eFuse; v7.3.5: kini MEDIAN burst 24 bacaan dan nilai penggerak klasifikasi
int      g_turbDig = 0;   // v7.1.5: saksi 3 - digitalRead(GPIO1); sempat jadi SUMBER level di v7.2.0 (mode D); v7.2.2: kembali hanya saksi untuk baris [T]
int      g_turbPct = 0;   // v7.1.9: persen kekeruhan ala sketch tutorial user (map raw 0..640 -> 100..0) - informatif; tampil di /telemetry (turbidity_pct) + baris [T]
String   g_turbLevel = "-";
uint8_t  g_turbLowCount = 0;   // v7.0.3: penghitung bacaan buruk berturut-turut untuk deteksi fault; v7.2.3: menghitung bacaan simpul probe yang menempel di rel 3V3 (jenuh gelap)
bool     g_turbFault = false;  // v7.0.3: true = sensor kekeruhan tidak merespons (listrik tak sampai); sempat dipensiunkan di mode D v7.2.0; v7.2.2: AKTIF lagi bersama sumber A
float    g_tdsPpm = 0.0;
bool     g_leak = false;
int      g_leakRaw = 0;
// v7.10.0: MPU6050 (gyro/kemiringan) DIHAPUS dari robot atas keputusan
// user 10 Nov 2026 - global, kode baca (Wire + bit-bang), telemetri,
// kartu dashboard, dan kolom CSV roll/pitch dibuang seluruhnya.
bool     g_rain = false;
int      g_rainRaw = 1;   // v7.1.1: level mentah DO hujan terakhir (1 = HIGH/kering, 0 = LOW/basah)
int      g_wqi = -1;    // -1 = belum valid (suhu belum terbaca)
String   g_wqiCat = "-"; // BAIK / SEDANG / BURUK

// --- Early warning state ---
bool     g_alert = false;
String   g_alertMsg = "";
uint8_t  g_alertCount = 0, g_clearCount = 0;
bool     g_buzzOn = false;
unsigned long g_buzzBurstStart = 0;  // awal ledakan bunyi buzzer (v6.5)
bool     g_buzzBurstActive = false;  // true selama ledakan 3 bip berjalan

// --- Indikator cas state (v7.3.6) ---
bool     g_chgBuzzOn = false;        // level buzzer yang diminta pola cas saat ini
bool          g_bootBeepActive = false;  // v7.3.9: pola bip boot 2x sedang berjalan
unsigned long g_bootBeepStart = 0;       // v7.3.9: kapan pola bip boot dimulai
bool     g_chgCharging = false;      // keadaan stabil ter-debounce (true = sedang cas)
bool     g_chgCand = false;          // kandidat keadaan dari bacaan pin terakhir
uint8_t  g_chgCandN = 0;             // jumlah bacaan kandidat berturut-turut (debounce)
unsigned long g_chgLastSample = 0;   // sampel pin terakhir (tiap 250 ms)
unsigned long g_chgSince = 0;        // kapan keadaan cas stabil dimulai (0 = tidak cas)
unsigned long g_chgNextBeep = 0;     // jadwal pola bip cas berkala berikutnya
uint8_t  g_chgPatKind = 0;           // 0 = diam, 1 = 2 bip pendek, 2 = 3 bip panjang
unsigned long g_chgPatStart = 0;     // awal pola bunyi cas yang sedang berjalan

unsigned long g_lastTelemetry = 0;
unsigned long g_lastTempReq = 0;
unsigned long g_tempReqAt = 0;
bool     g_tempPending = false;
bool     g_tempRetried = false;      // v7.2.1: siklus baca suhu ini sudah mencoba pemulihan (-127 -> enumerasi ulang + 1x baca ulang)?
int      g_suhuBusState = -1;        // v7.2.1: keadaan bus 1-Wire terakhir utk log [SUHU]: -1 belum tahu, 0 kosong, 1 ada perangkat
bool     g_suhuFailEpisode = false;  // v7.2.1: sedang dalam episode gagal baca suhu (agar log "pulih" tercetak sekali saja)

struct EnvLogRecord {
  uint32_t ts;
  float tempC;
  float turbV;
  float tdsPpm;
  uint8_t leak;
  int wqi;            // -1 = belum valid (suhu belum terbaca)
};
EnvLogRecord ramLogs[RAM_LOG_CAP];
int ramHead = 0, ramCount = 0;
int ramSessionBase = 0;  // v6.8.1: record RAM di bawah indeks ini = warisan
                         // sesi boot lama (ts-nya mulai dari 0 lagi) - tidak
                         // disajikan ke grafik tren agar garis tidak kusut.
bool fsReady = false;
unsigned long lastLogAt = 0;

// --- State AI kandidat korosi (v6.8) ---
struct AiResult {
  bool valid = false;
  uint32_t ts = 0;        // detik sejak boot saat analisis dijalankan
  String label = "-";     // kelas dengan skor tertinggi
  float score = 0;        // skor kelas tertinggi
  float karat = 0, cat = 0, logam = 0, lumut = 0;  // skor per kelas (dicari by nama)
  bool kandidat = false;  // karat >= AI_RUST_THRESHOLD
  uint32_t ms = 0;        // lama inferensi (ms)
};
AiResult g_ai;
uint32_t g_bootNo = 0;    // nomor sesi boot ini (persisten di /boot.txt)
long g_aiLogCount = 0;    // jumlah baris deteksi tersimpan di /aidet.csv
bool     g_aiAuto = true;       // mode otomatis (computer vision) on/off -
                                // v6.9.3: bawaan NYALA sejak boot; isi
                                // /aiset.txt (bila ada) yang menang
unsigned long g_lastAiAuto = 0; // kapan analisis otomatis terakhir jalan
bool     g_aiAutoHeld = false;  // v6.9.4: mode otomatis ditahan boot ini
                                // karena boot sebelumnya mati saat inferensi
bool     g_sdReady = false;     // hasil probe/sesi SD terakhir (kartu OK)
bool     g_sdEverMounted = false;  // v7.7.5: pernah mount sukses di boot ini
uint8_t  g_sdReprobeCount = 0;     // v7.7.5: hitungan probe ulang otomatis
uint8_t  g_sdFailStreak = 0;       // v7.8.1: gagal mount total beruntun di boot ini
unsigned long g_sdLastFailMs = 0;  // v7.10.2: cap waktu gagal mount total terakhir (cooldown tangga)
unsigned long g_sdReprobeLastMs = 0;  // v7.7.5: cap waktu probe ulang terakhir

// ============================ JAM & LOG (v7.8.0) ============================
// ESP32 tidak punya jam kalender: waktu disinkronkan dari HP/browser
// (endpoint /time-set, tanpa internet - jalan di mode AP) dan dari NTP
// tiap STA tersambung ke internet. nowEpoch() = 0 berarti belum
// tersinkron; konsumen WAJIB memperlakukan 0 sebagai "belum tahu".
void logEvent(const char* msg);  // definisi di bawah blok jam ini
bool g_timeSynced = false;
bool g_ntpStarted = false;
unsigned long nowEpoch() {
  if (!g_timeSynced) return 0;
  time_t t = time(nullptr);
  return (t > 1700000000) ? (unsigned long)t : 0;
}
static void clockSetEpoch(unsigned long e, const char* src) {
  if (e < 1700000000UL) return;
  struct timeval tv; tv.tv_sec = (time_t)e; tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  g_timeSynced = true;
  Serial.printf("[JAM] tersinkron dari %s (epoch %lu)\n", src, e);
}
static void ntpStartOnce() {
  if (g_ntpStarted) return;
  g_ntpStarted = true;
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.println("[JAM] NTP dimulai (zona WIB via TZ)");
}
static void pollClock() {
  if (g_timeSynced) return;
  time_t t = time(nullptr);
  if (t > 1700000000) {
    g_timeSynced = true;
    Serial.printf("[JAM] tersinkron dari NTP (epoch %lu)\n", (unsigned long)t);
    logEvent("Jam tersinkron dari NTP");
  }
}
// Log peristiwa robot (ring buffer RAM) - dibaca web lewat /log dan
// ditampilkan di sidebar. entri: epoch (0 = belum tersinkron), detik
// sejak boot, pesan singkat.
#define LOG_MAX 40
struct LogEnt { unsigned long epoch; unsigned long up; char msg[80]; };
static LogEnt g_logBuf[LOG_MAX];
static uint8_t g_logHead = 0;
static uint8_t g_logCount = 0;
void logEvent(const char* msg) {
  LogEnt& e = g_logBuf[g_logHead];
  e.epoch = nowEpoch();
  e.up = millis() / 1000;
  strncpy(e.msg, msg, sizeof(e.msg) - 1);
  e.msg[sizeof(e.msg) - 1] = 0;
  g_logHead = (uint8_t)((g_logHead + 1) % LOG_MAX);
  if (g_logCount < LOG_MAX) g_logCount++;
}
static String escJson(const String& in) {
  String o;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}
bool     g_sdMounted = false;   // SD sedang ter-mount (di dalam sesi)
bool     g_sdSession = false;   // sesi SD aktif: pin 38-41 sedang milik SD
unsigned long g_sdSessionStartMs = 0;  // v7.2.8: kapan sesi SD berjalan mulai (watchdog sesi macet)
SemaphoreHandle_t g_sdMutex = nullptr;  // v6.9.7: mutex sesi SD - diambil
TaskHandle_t g_sdOwner = nullptr;  // v7.7.0: task pemilik sesi berjalan
int      g_sdNested = 0;           // v7.7.0: kedalaman begin berpasangan task pemilik
bool     g_sdSticky = false;       // v7.7.0: sesi lengket loopTask - end() tidak melepas
bool     g_sdStickyEnabled = false;  // v7.7.0: lengket aktif hanya sesudah setup tuntas
TaskHandle_t g_loopTaskHandle = nullptr;  // v7.7.0: diisi di awal setup()
void sdSessionForceEnd();  // v7.7.0: definisi sesudah sdSessionEnd
                                // sdSessionBegin (baris pertama), dilepas
                                // sdSessionEnd (akhir); dibuat di setup()
                                // sebelum probe SD pertama
bool     g_ledcAttached = false; // channel LEDC motor sudah terpasang
void motorPwmAttach();  // v7.6.1: definisi di blok motor; dipakai juga sesi SD
long     g_aiPhotos = 0;        // jumlah foto kandidat tersimpan di SD
unsigned long g_lastFotoMs = 0; // cooldown simpan foto kandidat
static float*   aiFeatures = nullptr;  // 9216 float piksel RGB terkemas (PSRAM)
static uint8_t* aiRgb = nullptr;
static uint8_t* aiThumb = nullptr;     // v7.6.0: thumbnail RGB 96x96 foto bertahap (PSRAM)
String g_thumbName;  // v7.6.7: nama foto yang thumbnail-nya sedang di aiThumb ("" = tidak ada)
// v7.6.9: cache RAM untuk MEMINIMALKAN sesi SD. Hasil riset: mount
// ulang kartu yang sudah hangat adalah operasi yang intermiten
// gagal (0x107) dan tidak ada obat software yang terverifikasi -
// jadi strategi firmwarenya adalah sesedikit mungkin membuka sesi:
// indeks foto dan JPEG foto terakhir dipegang di RAM.
String   g_idxCache;            // salinan isi index.csv terakhir
bool     g_idxCacheValid = false;
uint8_t* g_lastJpg = nullptr;   // salinan JPEG foto terakhir (PSRAM)
size_t   g_lastJpgLen = 0;
String   g_lastJpgName;       // buffer dekode JPEG -> RGB888 (PSRAM)

int ramIndexFromOldest(int i) { return (ramHead - ramCount + i + RAM_LOG_CAP * 2) % RAM_LOG_CAP; }

void addRecordToRam(const EnvLogRecord& r) {
  // Ring penuh & record tertua (warisan sesi lama) tertimpa -> batas sesi ikut geser.
  if (ramCount >= RAM_LOG_CAP && ramSessionBase > 0) ramSessionBase--;
  ramLogs[ramHead] = r;
  ramHead = (ramHead + 1) % RAM_LOG_CAP;
  if (ramCount < RAM_LOG_CAP) ramCount++;
}

void ensureLogHeader() {
  if (!fsReady || LittleFS.exists(LOG_PATH)) return;
  File f = LittleFS.open(LOG_PATH, FILE_WRITE);
  if (f) { f.println(LOG_HEADER); f.close(); }
}

void appendRecordToFS(const EnvLogRecord& r) {
  if (!fsReady) return;
  ensureLogHeader();
  File f = LittleFS.open(LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%.2f,%.2f,%.1f,%u,%d\n",
           (unsigned long)r.ts, r.tempC, r.turbV, r.tdsPpm, r.leak, r.wqi);
  f.close();
}

void pruneLogIfNeeded() {
  if (!fsReady || !LittleFS.exists(LOG_PATH)) return;
  File f = LittleFS.open(LOG_PATH, FILE_READ);
  if (!f) return;
  if (f.size() <= LOG_MAX_BYTES) { f.close(); return; }
  int lines = 0;
  while (f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.length() && !l.startsWith("ts,")) lines++; }
  f.close();
  int skip = lines - LOG_KEEP_LINES;
  if (skip <= 0) return;
  File inF = LittleFS.open(LOG_PATH, FILE_READ);
  File outF = LittleFS.open(LOG_TMP, FILE_WRITE);
  if (!inF || !outF) { if (inF) inF.close(); if (outF) outF.close(); return; }
  outF.println(LOG_HEADER);
  int idx = 0;
  while (inF.available()) {
    String l = inF.readStringUntil('\n'); l.trim();
    if (!l.length() || l.startsWith("ts,")) continue;
    if (idx++ >= skip) outF.println(l);
  }
  inF.close(); outF.close();
  LittleFS.remove(LOG_PATH);
  LittleFS.rename(LOG_TMP, LOG_PATH);
  Serial.printf("[LOG] prune: %d baris tertua dihapus.\n", skip);
}

void logTick() {
  unsigned long now = millis();
  if (now - lastLogAt < LOG_INTERVAL_MS) return;
  lastLogAt = now;
  if (g_tempC <= -127.0) return;
  EnvLogRecord r{ (uint32_t)(now / 1000), g_tempC, g_turbV, g_tdsPpm,
                  (uint8_t)(g_leak ? 1 : 0), g_wqi };
  addRecordToRam(r);
  appendRecordToFS(r);
  pruneLogIfNeeded();
}

// ===================== RIWAYAT DETEKSI AI (persisten) =======================
// /aidet.csv di LittleFS: TIDAK pernah dibuat ulang saat boot (hanya
// ditambahi), jadi riwayat bertahan melewati robot mati / cabut baterai.
// Baris "#BOOT n" menandai awal tiap sesi menyala.
void ensureAiLogHeader() {
  if (!fsReady || LittleFS.exists(AI_LOG_PATH)) return;
  File f = LittleFS.open(AI_LOG_PATH, FILE_WRITE);
  if (f) { f.println(AI_LOG_HEADER); f.close(); }
}

void pruneAiLogIfNeeded() {
  if (!fsReady || !LittleFS.exists(AI_LOG_PATH)) return;
  File f = LittleFS.open(AI_LOG_PATH, FILE_READ);
  if (!f) return;
  if (f.size() <= AI_LOG_MAX_BYTES) { f.close(); return; }
  int lines = 0;
  while (f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.length() && !l.startsWith("boot,")) lines++; }
  f.close();
  int skip = lines - AI_LOG_KEEP_LINES;
  if (skip <= 0) return;
  File inF = LittleFS.open(AI_LOG_PATH, FILE_READ);
  File outF = LittleFS.open(AI_LOG_TMP, FILE_WRITE);
  if (!inF || !outF) { if (inF) inF.close(); if (outF) outF.close(); return; }
  outF.println(AI_LOG_HEADER);
  int idx = 0;
  while (inF.available()) {
    String l = inF.readStringUntil('\n'); l.trim();
    if (!l.length() || l.startsWith("boot,")) continue;
    if (idx++ >= skip) outF.println(l);
  }
  inF.close(); outF.close();
  LittleFS.remove(AI_LOG_PATH);
  LittleFS.rename(AI_LOG_TMP, AI_LOG_PATH);
  Serial.printf("[AI-LOG] prune: %d baris tertua dihapus.\n", skip);
}

void appendAiRecord() {
  if (!fsReady || !g_ai.valid) return;
  ensureAiLogHeader();
  File f = LittleFS.open(AI_LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%lu,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%lu\n",
           (unsigned long)g_bootNo, (unsigned long)g_ai.ts, g_ai.label.c_str(),
           g_ai.score, g_ai.karat, g_ai.cat, g_ai.logam, g_ai.lumut,
           g_ai.kandidat ? 1 : 0, (unsigned long)g_ai.ms);
  f.close();
  g_aiLogCount++;
  pruneAiLogIfNeeded();
}

// ===================== SD CARD + FOTO BUKTI =================================
// Deklarasi awal: kendali motor didefinisikan di bagian MOTOR di bawah,
// tetapi sesi SD (yang meminjam pin motor) berada di bagian ini.
void motorSleep();
void motorWake();
void stopMotors();
void motorForceStopNow();

// INTERLOCK SESI SD (v6.9.1): bus SD (CMD 38 / CLK 39 / DATA 40) adalah
// pin motor IN1/IN2/IN3, jadi kartu TIDAK PERNAH dibiarkan mounted.
// Satu sesi berjalan berurutan: duty motor 0 -> driver DITIDURKAN lewat
// EEP (output H-bridge mati total - dinamo mustahil bergerak walau pin
// berdenyut mengikuti bus SD) -> keempat pin motor dilepas dari LEDC ->
// SD di-mount & folder disiapkan -> (penulisan foto) -> SD di-unmount ->
// pin dipasang lagi ke channel LEDC -> duty 0 -> driver dibangunkan.
static String fotoIndexRead();  // v7.6.9: definisi di blok foto; dipakai cache indeks
void sdSessionBegin() {
  // Mutex sesi (v6.9.7): diambil PALING AWAL, sebelum logika lama -
  // sesi dari task mana pun (task AI / handler berkas di loopTask)
  // tidak pernah berjalan bersamaan. Pasangannya xSemaphoreGive di
  // akhir sdSessionEnd(); semua situs panggil memasangkan begin/end
  // 1:1 di semua jalur (termasuk jalur gagal mount), jadi take/give
  // selalu berpasangan walau return dini di bawah ini terpicu.
  // v7.7.0 SESI PERSISTEN: bila task yang sama sudah memegang sesi,
  // begin berpasangan hanya menaikkan hitungan sarang - mengambil
  // mutex sekali lagi dari task pemilik akan deadlock (mutex
  // FreeRTOS tidak rekursif). Task LAIN tetap mengantre di mutex.
  TaskHandle_t me = xTaskGetCurrentTaskHandle();
  if (g_sdSession && g_sdOwner == me) { g_sdNested++; return; }
  xSemaphoreTake(g_sdMutex, portMAX_DELAY);
  if (g_sdSession) return;
  g_sdSession = true;
  g_sdOwner = me;  // v7.7.0
  g_sdNested = 0;  // v7.7.0
  // v7.7.0: sesi milik loopTask (handler web/aplikasi) LENGKET -
  // kartu dibiarkan ter-mount sesudah operasi selesai supaya akses
  // berkas & analisis berikutnya tidak mount ulang (mount ulang
  // kartu hangat = titik gagal 0x107). Dilepas hanya oleh perintah
  // motor / serah-terima task AI lewat sdSessionForceEnd().
  // Selama setup lengket mati: sesi-sesi awal tetap lepas-tuntas
  // sebelum motor & self-test boot memakai pin.
  g_sdSticky = (me == g_loopTaskHandle) && g_sdStickyEnabled;
  g_sdSessionStartMs = millis();  // v7.2.8: cap waktu mulai sesi (watchdog)
  if (g_ledcAttached) stopMotors();  // terapkan duty 0 selagi LEDC terpasang
  motorSleep();                      // DRV8833 tidur: output mati total
  if (g_ledcAttached) {
    const int pins[4] = { MOTOR_L_IN1, MOTOR_L_IN2, MOTOR_R_IN1, MOTOR_R_IN2 };
    for (int i = 0; i < 4; i++) ledcDetachPin(pins[i]);
  }
#if SD_PINS_KNOWN
  SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN);
  // Pull-up internal dinyalakan eksplisit di ketiga pin SD (jalur slot
  // board ini tanpa pull-up eksternal -> tanpa ini kartu tidak menjawab
  // perintah init, error 0x107; terbukti di uji v6.9.2, dipertahankan
  // permanen) + clock SD diturunkan ke maksimum 10 MHz.
  pinMode(SDMMC_CMD_PIN, INPUT_PULLUP);
  pinMode(SDMMC_CLK_PIN, INPUT_PULLUP);
  pinMode(SDMMC_D0_PIN, INPUT_PULLUP);
  // v7.8.1: TANGGA PENDEK RUNTIME. Probe awal boot selalu tangga
  // penuh 6x. Tetapi sesudah SATU gagal total di boot yang sama
  // (g_sdFailStreak > 0) dan fase boot selesai (sesi lengket aktif),
  // sesi runtime cukup 2 percobaan: kartu yang terkunci mode tidak
  // pernah sembuh oleh 37 detik tangga di tengah boot (bukti log
  // 8 Nov malam: 6/6 gagal lalu motor tersandera tangga berulang),
  // sedangkan kartu yang hidup menjawab di percobaan 1-2. Ini
  // memangkas blokade loop dari +/-37 dtk menjadi +/-10 dtk.
  // v7.10.2: tangga 6x boot DIGANTI - begin pertama di boot ini
  // maks 3 percobaan, semua begin berikutnya maks 2, dan selama
  // COOLDOWN 45 detik sesudah gagal total tangga dilewati sama
  // sekali (kartu yang dibebani kabel motor tidak sembuh oleh
  // pengulangan cepat; yang disembuhkan justru boot & loop yang
  // tersandera). Variabel g_sdStickyEnabled tetap dipakai logika
  // sesi lengket di atas.
  static bool s_sdPernahBegin = false;
  const int maxPercobaan = s_sdPernahBegin ? 2 : 3;
  s_sdPernahBegin = true;
  const bool sdCooldown = (g_sdFailStreak > 0) &&
                          (millis() - g_sdLastFailMs < 45000UL);
  Serial.printf("[SD] pull-up internal ON, mount dicoba maks %dx (v7.10.2)\n",
                maxPercobaan);
  // v7.1.4: PENGERASAN MOUNT (logika sesi TIDAK berubah - lihat
  // changelog v7.1.4). Kartu tertancap & kadang terbaca, tetapi
  // sesi berkas intermiten gagal init 0x107 (kartu tidak
  // menjawab), jadi satu panggilan begin diganti loop maks 3
  // percobaan di dalam sesi yang sama: jeda stabilisasi
  // sebelum tiap percobaan, SD_MMC.end() di antara kegagalan,
  // dan clock diturunkan 10 MHz -> 1 MHz pada percobaan 2-3.
  // v7.7.4: TANGGA SABAR. Tahap yang gagal (ACMD41 / send_op_cond)
  // selalu berjalan di 400 kHz tetap - mengganti clock begin tidak
  // menyentuhnya; variabel yang tersisa adalah WAKTU. Kartu yang
  // lambat bangun / sibuk urusan internal sesudah penulisan bisa
  // menjawab di percobaan yang lebih lambat. Loop diperpanjang
  // 3 -> 6 percobaan dengan jeda antar-percobaan membesar
  // (0,5 / 1 / 2 / 3 / 3 detik); kasus terburuk +/-15 detik, hanya
  // terjadi bila kartu benar-benar tidak menjawab sama sekali.
  static const unsigned int JEDA_MOUNT_MS[5] = { 500, 1000, 2000, 3000, 3000 };
  bool mounted = false;
  for (int percobaan = 1; percobaan <= maxPercobaan && !mounted && !sdCooldown; percobaan++) {
    unsigned int clockKhz = (percobaan == 1) ? 10000 : 1000;
    delay(150);  // beri waktu pull-up menstabilkan bus
    Serial.printf("[SD] mount percobaan %d/%d (clock maks %u MHz)\n",
                  percobaan, maxPercobaan, clockKhz / 1000);
    if (SD_MMC.begin("/sdcard", true, false, clockKhz)) {  // mode 1-bit
      mounted = true;
      g_sdFailStreak = 0;  // v7.8.1: kartu menjawab - coret gugur direset
      g_sdMounted = true;
      SD_MMC.mkdir("/fir");
      SD_MMC.mkdir(SD_FOTO_DIR);
      if (!g_sdReady) {
        g_sdReady = true;
        g_sdEverMounted = true;  // v7.7.5
        Serial.printf("SD card: SIAP (%u MB) - folder foto %s\n",
                      (unsigned)(SD_MMC.cardSize() / (1024ULL * 1024ULL)),
                      SD_FOTO_DIR);
        // v7.6.9: muat indeks foto ke cache RAM dalam sesi yang
        // sedang terbuka ini, supaya /photos tak perlu sesi sendiri.
        g_idxCache = fotoIndexRead();
        g_idxCacheValid = true;
      }
    } else {
      Serial.printf("[SD] mount percobaan %d/%d GAGAL\n", percobaan, maxPercobaan);
      if (percobaan < maxPercobaan) { SD_MMC.end(); delay(JEDA_MOUNT_MS[percobaan - 1]); }
    }
  }
  if (!mounted) {
    g_sdReady = false;
    if (sdCooldown) {
      // v7.10.2: tangga dilewati karena cooldown - jangan perpanjang
      // jendelanya dan jangan spam log; pemanggil menerima
      // g_sdMounted = false persis seperti jalur gagal biasa.
      Serial.println("[SD] cooldown v7.10.2: tangga mount dilewati (gagal total <45 dtk lalu)");
    } else {
      if (g_sdFailStreak < 250) g_sdFailStreak++;  // v7.8.1
      g_sdLastFailMs = millis();  // v7.10.2: mulai jendela cooldown
      Serial.printf("SD card: GAGAL mount sesudah %d percobaan (belum terpasang / tidak menjawab init)\n", maxPercobaan);
      logEvent("SD card GAGAL mount - kartu tidak menjawab");
    }
  }
#endif
}

void sdSessionEnd() {
  // v7.7.0: end berpasangan dengan begin bersarang - cukup turunkan
  // hitungan, sesi dan mount tetap.
  if (g_sdNested > 0) { g_sdNested--; return; }
  // v7.7.0: sesi lengket tidak dilepas oleh end operasi - kartu
  // tetap ter-mount untuk operasi berikutnya.
  if (g_sdSticky) return;
#if SD_PINS_KNOWN
  if (g_sdMounted) { SD_MMC.end(); g_sdMounted = false; }
#endif
  if (g_ledcAttached) {
    const int pins[4] = { MOTOR_L_IN1, MOTOR_L_IN2, MOTOR_R_IN1, MOTOR_R_IN2 };
    motorPwmAttach();  // v7.6.1: pasang lagi lewat jalur IDF yang sama
    motorForceStopNow();   // duty 0 diterapkan SEKETIKA pada channel yang baru dipasang lagi (tanpa menunggu ramp slew)
    motorWake();    // driver dibangunkan terakhir, sesudah pin aman
  }
  g_sdSession = false;
  g_sdOwner = nullptr;  // v7.7.0
  g_sdSessionStartMs = 0;  // v7.2.8: sesi selesai normal - watchdog direset
  xSemaphoreGive(g_sdMutex);  // v6.9.7: pasangan take di sdSessionBegin
}

// v7.7.0: lepas sesi lengket secara paksa. Hanya dipanggil dari
// loopTask (pemilik sesi lengket): perintah motor yang masuk, atau
// serah-terima ke task AI yang sesi SD-nya sendiri butuh mutex.
void sdSessionForceEnd() {
  if (!g_sdSession || !g_sdSticky) return;
  Serial.println("[SD] sesi lengket dilepas (perintah motor / task AI)");
  g_sdSticky = false;
  g_sdNested = 0;
  sdSessionEnd();
}

// Simpan frame JPEG yang BARUSAJA dianalisis sebagai foto bukti kandidat.
// Nama berkas b<boot>_<ts>.jpg cocok dengan baris boot+ts di /aidet.csv.
// Penulisan dibungkus sesi SD (pin dipinjam dari motor selama menulis).
bool saveCandidatePhoto(camera_fb_t* fb) {
  if (!g_sdReady || !fb) return false;
  if (millis() - g_lastFotoMs < AI_FOTO_COOLDOWN_MS) return false;
  sdSessionBegin();
  if (!g_sdMounted) { sdSessionEnd(); return false; }
  char path[48];
  snprintf(path, sizeof(path), "%s/b%lu_%06lu.jpg", SD_FOTO_DIR,
           (unsigned long)g_bootNo, (unsigned long)g_ai.ts);
  File pf = SD_MMC.open(path, FILE_WRITE);
  bool ok = false;
  if (!pf) {
    Serial.printf("[SD] gagal membuka %s\n", path);
  } else {
    pf.write(fb->buf, fb->len);
    pf.close();
    g_lastFotoMs = millis();
    g_aiPhotos++;
    Serial.printf("[SD] foto kandidat tersimpan: %s (%u byte)\n", path, (unsigned)fb->len);
    ok = true;
  }
  sdSessionEnd();
  return ok;
}

// ===================== FILE MANAGER SD (/files) =============================
// Halaman web untuk mengelola isi SD card: telusur folder, Buka/Unduh
// berkas, Buat Folder, Unggah berkas, Hapus berkas/folder kosong.
// SEMUA akses SD berjalan di dalam sesi terkunci (sdSessionBegin/End):
// pin 38-40 dipinjam dari motor dan driver ditidurkan selama sesi.
#include <vector>
#include <algorithm>
#include <strings.h>

// Validasi path SD bersama: harus absolut, tanpa segmen "..", < 128 char.
bool validSdPath(const String& p) {
  if (p.length() == 0 || p.length() >= 128) return false;
  if (p[0] != '/') return false;
  int start = 0;
  while (start <= (int)p.length()) {
    int slash = p.indexOf('/', start);
    String seg = (slash < 0) ? p.substring(start) : p.substring(start, slash);
    if (seg == "..") return false;
    if (slash < 0) break;
    start = slash + 1;
  }
  return true;
}

String urlEncodeSd(const String& s) {
  String out;
  out.reserve(s.length() * 2);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
        c == '~' || c == '/') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

String htmlEscSd(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

String fmtSizeSd(uint32_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1024UL * 1024UL) return String(bytes / 1024.0, 1) + " KB";
  return String(bytes / (1024.0 * 1024.0), 2) + " MB";
}

String parentDirOf(const String& path) {
  String p = path;
  while (p.length() > 1 && p.endsWith("/")) p = p.substring(0, p.length() - 1);
  int slash = p.lastIndexOf('/');
  if (slash <= 0) return "/";
  return p.substring(0, slash);
}

String baseNameOf(const String& path) {
  int slash = path.lastIndexOf('/');
  return (slash >= 0) ? path.substring(slash + 1) : path;
}

struct SdEnt { String name; bool isDir; uint32_t size; };

void handleFiles() {
  String dir = server.hasArg("dir") ? server.arg("dir") : String(SD_FOTO_DIR);
  if (dir.length() == 0) dir = SD_FOTO_DIR;
  while (dir.length() > 1 && dir.endsWith("/")) dir = dir.substring(0, dir.length() - 1);
  if (!validSdPath(dir)) { server.send(400, "text/plain", "Path tidak valid."); return; }

  // Baca isi folder dalam SATU sesi SD singkat; render sesudah sesi tutup.
  std::vector<SdEnt> folders, files;
  bool sdOk = false, dirOk = false;
  sdSessionBegin();
  if (g_sdMounted) {
    sdOk = true;
    File d = SD_MMC.open(dir.c_str());
    if (d && d.isDirectory()) {
      dirOk = true;
      File e = d.openNextFile();
      while (e) {
        SdEnt ent;
        ent.name = baseNameOf(String(e.name()));
        ent.isDir = e.isDirectory();
        ent.size = ent.isDir ? 0 : (uint32_t)e.size();
        (ent.isDir ? folders : files).push_back(ent);
        e.close();
        e = d.openNextFile();
      }
    }
    if (d) d.close();
  }
  sdSessionEnd();

  std::sort(folders.begin(), folders.end(),
            [](const SdEnt& a, const SdEnt& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
  std::sort(files.begin(), files.end(),
            [](const SdEnt& a, const SdEnt& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });

  String html;
  html.reserve(4096);
  html += R"fm(<!DOCTYPE html><html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FIR - Berkas SD Card</title>
<style>
 *{box-sizing:border-box}
 body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color:#E9EDFA;margin:0;padding:20px 16px 32px;
   background:#070B1A;background-attachment:fixed;
   background-image:radial-gradient(1000px 500px at 80% -10%,#26306B 0%,rgba(38,48,107,0) 60%),linear-gradient(180deg,#0A0F22,#070B1A)}
 .wrap{max-width:960px;margin:0 auto}
 header{display:flex;align-items:center;gap:12px;margin-bottom:4px}
 .logoimg{width:44px;height:44px;border-radius:12px;object-fit:cover;flex:none}
 h1{font-size:21px;margin:0;color:#fff;font-weight:800;letter-spacing:.2px}
 .sub{color:#8E9AC0;font-size:13px;margin:2px 0 16px}
 .card{background:rgba(255,255,255,.045);border:1px solid rgba(255,255,255,.09);border-radius:16px;padding:16px;box-shadow:0 4px 24px rgba(0,0,0,.22)}
 .card h2{font-size:12px;margin:0 0 8px;color:#9DB4FF;text-transform:uppercase;letter-spacing:.8px;font-weight:700}
 .row{margin-bottom:12px}
 .stats{font-size:12px;color:#93A0C4;margin-top:8px;line-height:1.5}
 table{width:100%;border-collapse:collapse;font-size:14px}
 td{padding:8px 6px;border-bottom:1px solid rgba(255,255,255,.08);vertical-align:middle}
 .sz{color:#93A0C4;white-space:nowrap;text-align:right}
 .acts{white-space:nowrap;text-align:right}
 .acts a{margin-left:10px;font-weight:700;text-decoration:none;color:#8FA6FF}
 .acts a.del{color:#F87171}
 .crumb{font-size:14px;margin:4px 0 14px;line-height:1.7}
 .crumb a{color:#8FA6FF;font-weight:700;text-decoration:none}
 a.back{color:#8FA6FF;font-weight:700;text-decoration:none;font-size:14px}
 .msg{background:rgba(192,57,43,.16);border:1px solid rgba(248,113,113,.45);color:#FFB4A8;border-radius:14px;padding:12px 16px;font-weight:700;margin-bottom:14px}
 input[type=text]{padding:9px 10px;border:1px solid rgba(255,255,255,.15);border-radius:10px;font-size:14px;width:min(320px,65%);background:rgba(255,255,255,.05);color:#fff}
 input[type=file]{color:#C9D2EE}
 button{background:linear-gradient(135deg,#3550C8,#5B7CFF);color:#fff;border:none;padding:9px 16px;border-radius:10px;font-size:13px;font-weight:700;cursor:pointer}
</style></head><body>
<div class="wrap">
<header><img class="logoimg" src="data:image/jpeg;base64,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" alt="Logo Axon Expanse"><div><h1>Berkas SD Card</h1><div style="font-size:11px;color:#8E9AC0;letter-spacing:.6px">FIR</div></div></header>
<div class="sub">Kelola isi SD card robot &mdash; buka, unduh, unggah, buat folder, hapus.</div>
<a class="back" href="/">&larr; Dashboard</a>
)fm";
  // Breadcrumb: tiap segmen path adalah tautan ke foldernya.
  html += "<div class=\"crumb\">";
  html += "<a href=\"/files?dir=/\">SD</a>";
  if (dir != "/") {
    String acc;
    int start = 1;
    while (start <= (int)dir.length()) {
      int slash = dir.indexOf('/', start);
      String seg = (slash < 0) ? dir.substring(start) : dir.substring(start, slash);
      if (seg.length() == 0) break;
      acc += "/" + seg;
      html += " / <a href=\"/files?dir=" + urlEncodeSd(acc) + "\">" + htmlEscSd(seg) + "</a>";
      if (slash < 0) break;
      start = slash + 1;
    }
  }
  html += "</div>";

  if (!sdOk) {
    html += "<div class=\"msg\">SD card tidak terpasang / tidak terbaca. "
            "Pasang kartu berformat FAT32 di slot board, lalu muat ulang halaman ini.</div>";
  } else if (!dirOk) {
    html += "<div class=\"msg\">Folder tidak ditemukan: " + htmlEscSd(dir) + "</div>";
  } else {
    if (dir != "/") {
      html += "<div style=\"margin-bottom:10px\"><a class=\"back\" href=\"/files?dir=" +
              urlEncodeSd(parentDirOf(dir)) + "\">&uarr; Folder induk</a></div>";
    }
    html += "<div class=\"card row\"><table>";
    if (folders.empty() && files.empty()) {
      html += "<tr><td class=\"sz\" style=\"text-align:left\">(folder kosong)</td></tr>";
    }
    for (size_t i = 0; i < folders.size(); i++) {
      String child = (dir == "/") ? ("/" + folders[i].name) : (dir + "/" + folders[i].name);
      html += "<tr><td>&#128193; <a href=\"/files?dir=" + urlEncodeSd(child) +
              "\" style=\"color:#E9EDFA;font-weight:700;text-decoration:none\">" +
              htmlEscSd(folders[i].name) + "/</a></td><td class=\"sz\">folder</td>" +
              "<td class=\"acts\"><a class=\"del\" href=\"/file-del?path=" +
              urlEncodeSd(child) +
              "\" onclick=\"return confirm('Hapus folder ini? Folder hanya terhapus bila kosong.')\">Hapus</a></td></tr>";
    }
    for (size_t i = 0; i < files.size(); i++) {
      String child = (dir == "/") ? ("/" + files[i].name) : (dir + "/" + files[i].name);
      String enc = urlEncodeSd(child);
      html += "<tr><td>&#128196; " + htmlEscSd(files[i].name) + "</td><td class=\"sz\">" +
              fmtSizeSd(files[i].size) + "</td><td class=\"acts\">" +
              "<a href=\"/file-get?path=" + enc + "&dl=0\" target=\"_blank\">Buka</a>" +
              "<a href=\"/file-get?path=" + enc + "&dl=1\">Unduh</a>" +
              "<a class=\"del\" href=\"/file-del?path=" + enc +
              "\" onclick=\"return confirm('Hapus berkas ini secara permanen?')\">Hapus</a></td></tr>";
    }
    html += "</table></div>";
    html += "<div class=\"card row\"><h2>Buat Folder</h2>"
            "<form method=\"POST\" action=\"/file-mkdir\">"
            "<input type=\"hidden\" name=\"dir\" value=\"" + htmlEscSd(dir) + "\">"
            "<input type=\"text\" name=\"name\" placeholder=\"Nama folder baru\" required> "
            "<button type=\"submit\">Buat Folder</button></form>"
            "<div class=\"stats\">Folder baru dibuat di dalam folder yang sedang dibuka ini.</div></div>";
    // PENTING: field tersembunyi "dir" ditulis SEBELUM input file, supaya
    // nilainya sudah terbaca server.arg("dir") saat UPLOAD_FILE_START.
    html += "<div class=\"card row\"><h2>Unggah Berkas</h2>"
            "<form method=\"POST\" action=\"/file-upload\" enctype=\"multipart/form-data\">"
            "<input type=\"hidden\" name=\"dir\" value=\"" + htmlEscSd(dir) + "\">"
            "<input type=\"file\" name=\"file\" required> "
            "<button type=\"submit\">Unggah</button></form>"
            "<div class=\"stats\">Berkas tersimpan di folder yang sedang dibuka ini. "
            "Motor berhenti selama unggahan berjalan (interlock SD) - itu normal.</div></div>";
  }
  html += "<a class=\"back\" href=\"/\">&larr; Dashboard</a></div></body></html>";
  server.send(200, "text/html", html);
}

// Buka (dl=0, inline) atau unduh (dl=1, attachment) satu berkas dari SD.
// Escape minimal agar nama berkas aman masuk string JSON.
String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((unsigned char)c < 0x20) { out += ' '; }
    else out += c;
  }
  return out;
}

// Endpoint JSON daftar berkas untuk pengelola berkas aplikasi
// Android (v7.0.2): GET /files-json?dir=<path> (dir opsional,
// bawaan "/"). Kontrak respons persis:
// {"dir":"<path>","sd":true,"entries":[{"name":"..","dir":false,"size":123}]}
// Validasi path, sesi SD, dan pengurutan (folder dulu lalu berkas,
// alfabetis di dalam kelompoknya) sama persis seperti handleFiles.
void handleFilesJson() {
  String dir = server.hasArg("dir") ? server.arg("dir") : String("/");
  if (dir.length() == 0) dir = "/";
  while (dir.length() > 1 && dir.endsWith("/")) dir = dir.substring(0, dir.length() - 1);
  if (!validSdPath(dir)) { server.send(400, "text/plain", "Path tidak valid."); return; }

  std::vector<SdEnt> folders, files;
  bool sdOk = false;
  sdSessionBegin();
  if (g_sdMounted) {
    sdOk = true;
    File d = SD_MMC.open(dir.c_str());
    if (d && d.isDirectory()) {
      File e = d.openNextFile();
      while (e) {
        SdEnt ent;
        ent.name = baseNameOf(String(e.name()));
        ent.isDir = e.isDirectory();
        ent.size = ent.isDir ? 0 : (uint32_t)e.size();
        (ent.isDir ? folders : files).push_back(ent);
        e.close();
        e = d.openNextFile();
      }
    }
    if (d) d.close();
  }
  sdSessionEnd();

  std::sort(folders.begin(), folders.end(),
            [](const SdEnt& a, const SdEnt& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
  std::sort(files.begin(), files.end(),
            [](const SdEnt& a, const SdEnt& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });

  String j;
  j.reserve(512);
  j += "{\"dir\":\"";
  j += jsonEscape(dir);
  j += "\",\"sd\":";
  j += sdOk ? "true" : "false";
  j += ",\"entries\":[";
  bool firstEnt = true;
  for (const SdEnt& ent : folders) {
    if (!firstEnt) j += ",";
    firstEnt = false;
    j += "{\"name\":\"";
    j += jsonEscape(ent.name);
    j += "\",\"dir\":true,\"size\":0}";
  }
  for (const SdEnt& ent : files) {
    if (!firstEnt) j += ",";
    firstEnt = false;
    j += "{\"name\":\"";
    j += jsonEscape(ent.name);
    j += "\",\"dir\":false,\"size\":";
    j += String(ent.size);
    j += "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleFileGet() {
  String path = server.hasArg("path") ? server.arg("path") : "";
  bool dl = server.hasArg("dl") && server.arg("dl") == "1";
  if (!validSdPath(path)) { server.send(400, "text/plain", "Path tidak valid."); return; }
  sdSessionBegin();
  if (!g_sdMounted) {
    sdSessionEnd();
    server.send(503, "text/plain", "SD card tidak terpasang / tidak terbaca.");
    return;
  }
  File f = SD_MMC.open(path.c_str(), FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    sdSessionEnd();
    server.send(404, "text/plain", "Berkas tidak ditemukan.");
    return;
  }
  size_t total = f.size();
  String lower = path;
  lower.toLowerCase();
  String mime = "application/octet-stream";
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) mime = "image/jpeg";
  else if (lower.endsWith(".png")) mime = "image/png";
  else if (lower.endsWith(".txt") || lower.endsWith(".csv")) mime = "text/plain";
  server.setContentLength(total);
  server.sendHeader("Content-Disposition",
                    String(dl ? "attachment" : "inline") + "; filename=\"" +
                    baseNameOf(path) + "\"");
  server.send(200, mime.c_str(), "");
  WiFiClient client = server.client();
  uint8_t buf[1024];
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    client.write(buf, n);
  }
  f.close();
  sdSessionEnd();
}

// Buat folder baru di dalam folder yang sedang dibuka.
void handleFileMkdir() {
  String dir = server.hasArg("dir") ? server.arg("dir") : String(SD_FOTO_DIR);
  String name = server.hasArg("name") ? server.arg("name") : "";
  name.trim();
  bool nameOk = name.length() > 0 && name.length() < 64 &&
                name.indexOf('/') < 0 && name.indexOf("..") < 0;
  if (!validSdPath(dir) || !nameOk) {
    server.send(400, "text/plain", "Nama folder / path tidak valid.");
    return;
  }
  String full = (dir == "/") ? ("/" + name) : (dir + "/" + name);
  sdSessionBegin();
  if (g_sdMounted) SD_MMC.mkdir(full.c_str());
  sdSessionEnd();
  server.sendHeader("Location", "/files?dir=" + urlEncodeSd(dir));
  server.send(303, "text/plain", "");
}

// State unggahan berjalan (satu unggahan pada satu waktu - server tunggal).
File   g_upFile;
String g_upDir;
String g_upPath;
bool   g_upSessionActive = false;

// Callback unggahan: sesi SD dibuka di START dan ditutup di END/ABORTED.
void handleFileUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_upDir = server.hasArg("dir") ? server.arg("dir") : String(SD_FOTO_DIR);
    if (!validSdPath(g_upDir)) g_upDir = SD_FOTO_DIR;
    String fname = up.filename;
    fname.trim();
    fname.replace("/", "_");
    fname.replace("\\", "_");
    fname.replace("..", "_");
    sdSessionBegin();
    g_upSessionActive = true;
    if (g_sdMounted && fname.length() > 0) {
      g_upPath = (g_upDir == "/") ? ("/" + fname) : (g_upDir + "/" + fname);
      g_upFile = SD_MMC.open(g_upPath.c_str(), FILE_WRITE);
      if (!g_upFile) Serial.printf("[SD] unggahan GAGAL dibuka: %s\n", g_upPath.c_str());
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_upFile) g_upFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (g_upFile) g_upFile.close();
    sdSessionEnd();
    g_upSessionActive = false;
    Serial.printf("[SD] unggahan selesai: %s (%u byte)\n",
                  g_upPath.c_str(), (unsigned)up.totalSize);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (g_upFile) g_upFile.close();
    if (g_sdMounted && g_upPath.length() > 0) SD_MMC.remove(g_upPath.c_str());
    sdSessionEnd();
    g_upSessionActive = false;
    Serial.println("[SD] unggahan dibatalkan - berkas parsial dihapus");
  }
}

// Handler POST biasa untuk /file-upload: kembali ke folder tujuan.
void handleFileUploadDone() {
  String target = g_upDir.length() ? g_upDir : String(SD_FOTO_DIR);
  server.sendHeader("Location", "/files?dir=" + urlEncodeSd(target));
  server.send(303, "text/plain", "");
}

// Hapus berkas (remove) atau folder (rmdir - hanya berhasil bila kosong).
void handleFileDel() {
  String path = server.hasArg("path") ? server.arg("path") : "";
  if (!validSdPath(path) || path == "/") {
    server.send(400, "text/plain", "Path tidak valid.");
    return;
  }
  String parent = parentDirOf(path);
  sdSessionBegin();
  if (g_sdMounted) {
    File f = SD_MMC.open(path.c_str());
    bool isDir = f && f.isDirectory();
    if (f) f.close();
    bool ok = isDir ? SD_MMC.rmdir(path.c_str()) : SD_MMC.remove(path.c_str());
    Serial.printf("[SD] hapus %s (%s): %s\n", path.c_str(),
                  isDir ? "folder" : "berkas", ok ? "OK" : "GAGAL");
  }
  sdSessionEnd();
  server.sendHeader("Location", "/files?dir=" + urlEncodeSd(parent));
  server.send(303, "text/plain", "");
}

// ===================== SAKLAR MODE OTOMATIS (persisten) =====================
void saveAiAutoSetting() {
  if (!fsReady) return;
  File f = LittleFS.open(AI_SET_PATH, FILE_WRITE);
  if (f) { f.print(g_aiAuto ? "1" : "0"); f.close(); }
}

void loadAiAutoSetting() {
  // Bila /aiset.txt ADA, isinya yang menang ('1' = nyala, '0' = mati).
  // Bila berkas tidak ada / gagal dibaca, nilai bawaan g_aiAuto
  // dipertahankan - sejak v6.9.3 bawaannya NYALA (true).
  if (!fsReady || !LittleFS.exists(AI_SET_PATH)) return;
  File f = LittleFS.open(AI_SET_PATH, FILE_READ);
  if (f) { g_aiAuto = (f.read() == '1'); f.close(); }
}

// ===================== OVERRIDE TURBIDITY /adminin (v7.10.0) =====================
// Pilihan admin di halaman /adminin: label klasifikasi kekeruhan yang
// tampil di dashboard. 0 = otomatis (ikut sensor), 1/2/3 = jernih/
// sedang/keruh. Tersimpan di LittleFS seperti saklar mode otomatis.
void saveTurbOverride() {
  if (!fsReady) return;
  File f = LittleFS.open(TURB_OVR_PATH, FILE_WRITE);
  if (f) { f.print(g_turbOverride); f.close(); }
}

void loadTurbOverride() {
  if (!fsReady || !LittleFS.exists(TURB_OVR_PATH)) return;
  File f = LittleFS.open(TURB_OVR_PATH, FILE_READ);
  if (f) {
    int v = f.parseInt();
    if (v >= 0 && v <= 3) g_turbOverride = v;
    f.close();
  }
}

// ============================ KAMERA ==========================================
// v7.9.2: status init kamera disimpan agar bisa dilaporkan kapan pun
// (log boot + endpoint /cam-diag). Sebelumnya kode error
// esp_camera_init dibuang begitu saja sehingga kegagalan kamera
// tidak pernah terdiagnosis - dashboard hanya tahu "kamera sibuk".
bool g_camOk  = false;   // true = esp_camera_init berhasil
int  g_camErr = 0;       // esp_err_t mentah dari esp_camera_init terakhir
int  g_camPid = -1;      // PID sensor: 0x2642 = OV2640, 0x5640 = OV5640; -1 = tidak terbaca
// v7.9.3: konfigurasi kamera yang sedang AKTIF (untuk laporan
// /cam-diag sesudah tangga pemulihan mengganti konfigurasi).
int         g_camXclkHz = 20000000;
framesize_t g_camFs     = FRAMESIZE_SVGA;

// v7.9.3: init kamera berparameter - fondasi tangga pemulihan
// /cam-diag. Peta pin & format identik versi sebelumnya; yang bisa
// divariasikan hanya XCLK, ukuran frame, jumlah buffer & grab mode.
bool initCameraCfg(int xclkHz, framesize_t fs, int fbCount, camera_grab_mode_t grabMode, camera_fb_location_t fbLoc) {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = xclkHz;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = fs;
  config.jpeg_quality = 12;
  config.fb_count = fbCount;
  config.grab_mode = grabMode;
  config.fb_location = fbLoc;
  esp_err_t camErr = esp_camera_init(&config);
  g_camErr = (int)camErr;
  g_camOk  = (camErr == ESP_OK);
  if (g_camOk) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) g_camPid = (int)s->id.PID;
    g_camXclkHz = xclkHz;
    g_camFs     = fs;
  }
  return g_camOk;
}

// Init kamera konfigurasi produksi (dipakai boot): SVGA @ XCLK 20 MHz,
// 2 buffer GRAB_LATEST; tanpa PSRAM: CIF 1 buffer - identik v7.9.2.
bool initCamera() {
  if (psramFound()) return initCameraCfg(20000000, FRAMESIZE_SVGA, 2, CAMERA_GRAB_LATEST, CAMERA_FB_IN_PSRAM);
  return initCameraCfg(20000000, FRAMESIZE_CIF, 1, CAMERA_GRAB_WHEN_EMPTY, CAMERA_FB_IN_PSRAM);
}

// Uji ambil frame sampai `tries` kali (frame pertama sesudah init bisa
// gugur selagi sensor menstabilkan diri). true + data frame pada
// keberhasilan pertama.
static bool camGrabTry(int tries, size_t* outLen, int* outW, int* outH) {
  for (int i = 0; i < tries; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      if (outLen) *outLen = fb->len;
      if (outW) *outW = (int)fb->width;
      if (outH) *outH = (int)fb->height;
      esp_camera_fb_return(fb);
      return true;
    }
    delay(120);
  }
  return false;
}

// ===================== INFERENSI AI KANDIDAT KOROSI =========================
// ---- Override alokasi Edge Impulse -> PSRAM (v6.9.4) ----
// Library EI mendefinisikan ei_malloc/ei_calloc/ei_free sebagai fungsi
// LEMAH (weak) berisi malloc/calloc RAM internal biasa. Model ini
// memakai EI_CLASSIFIER_ALLOCATION_HEAP: tensor arena 246.784 byte
// dialokasikan saat inferensi - di RAM internal yang sudah sesak oleh
// WiFi + kamera + server alokasi itu GAGAL (NULL), SDK tetap lanjut
// menulis ke alamat NULL -> panic StoreProhibited (EXCVADDR 0x0) ->
// reboot -> putaran crash. Definisi KUAT di sini (tanpa weak, tanda
// tangan & linkage C++ persis seperti definisi weak di
// porting/arduino/ei_classifier_porting.cpp) menang saat link dan
// memindahkan semua alokasi EI ke PSRAM; fallback ke RAM internal
// hanya bila PSRAM juga gagal.
//
// Bantalan alokasi (v7.0.5): dump crash board 3 Nov membuktikan
// run_classifier SELESAI berjalan, tetapi planner model menulis
// beberapa byte melewati ujung arena tensor - "CORRUPT HEAP: Bad
// tail at 0x3d9af6a1. Expected 0xbaad5678 got 0x68ad5678" (canary
// ekor berubah hanya 1 byte di posisi terjauh), dan kerusakan itu
// baru terdeteksi saat free di dalam tflite_learn_1154536_5_reset
// yang dipanggil runAiAnalysis (assert multi_heap_free). Setiap
// alokasi EI kini diberi ekor ekstra EI_ALLOC_PAD_BYTES byte yang
// menyerap luapan kecil planner itu, sehingga blok yang diminta
// model tidak lagi rusak.
//
// PERBAIKAN v7.3.1 (crash "motor + analisis" dari laporan user):
// backtrace board didekode via addr2line terhadap ELF v7.3.0 -
// assert di multi_heap_free <- heap_caps_free <- free, dipanggil
// dari jalur init/reset model di dalam runAiAnalysis. Tersangka
// mekanisnya: override lama memakai heap_caps_aligned_alloc /
// heap_caps_aligned_calloc DI BAWAH ei_aligned_calloc milik SDK.
// Itu DUA lapisan alignment bertumpuk: SDK menyimpan offset-nya
// sendiri dan ei_aligned_free mengembalikan pointer hasil hitung
// mundur ke ei_free -> free(), tetapi pointer itu adalah pointer
// ALIGNED hasil lapisan heap_caps (bukan awal blok heap yang
// sebenarnya), sehingga multi_heap_free menerima pointer yang
// bukan awal blok -> metadata heap rusak -> assert -> reboot.
// Pola baku porting EI di ESP32 hanya SATU lapisan: ei_malloc /
// ei_calloc = malloc PSRAM polos (mengembalikan awal blok yang
// sebenarnya), alignment diurus SDK di atasnya. v7.3.1 kembali
// ke pola itu; bantalan 256 byte dipertahankan. Pemicu yang
// dilaporkan user (menggerakkan motor SAMBIL analisis) bukan
// penyebab mekanisnya: yang mematikan adalah analisis yang
// berjalan sampai selesai (jalur free arena) - aktivitas
// bersamaan hanya membuat analisis tuntas lebih sering terjadi.
#define EI_ALLOC_PAD_BYTES 256

void* ei_malloc(size_t size) {
  void* p = heap_caps_malloc(size + EI_ALLOC_PAD_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(size + EI_ALLOC_PAD_BYTES, MALLOC_CAP_DEFAULT);
  return p;
}

void* ei_calloc(size_t nitems, size_t size) {
  size_t total = nitems * size + EI_ALLOC_PAD_BYTES;
  void* p = heap_caps_calloc(1, total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(1, total, MALLOC_CAP_DEFAULT);
  return p;
}

void ei_free(void* ptr) {
  free(ptr);
}

// v7.6.3: free-fungsi SENGAJA KOSONG untuk reset model. Backtrace
// crash v7.6.2 (terdekode addr2line dari ELF, laporan user 7 Nov
// 08:28): aiTaskFn -> runAiAnalysis -> aiInferCore ->
// tflite_learn_1154536_5_reset(ei_aligned_free) -> ei_aligned_free ->
// ei_free -> free -> multi_heap_free assert (head != NULL) -> PANIC.
// Yang dibebaskan reset adalah tensor_arena: ei_aligned_free membaca
// kata offset 2 byte SEBELUM pointer arena dan menghitung mundur ke
// awal blok heap - satu-satunya mata rantai rapuh di jalur ini, dan
// ia menabrak metadata heap. Arena TIDAK perlu dibebaskan: blok itu
// dialokasikan sekali oleh init model dan dipakai ulang tiap
// inferensi (persis pola arena statis). Dengan free penahan ini,
// reset tetap membersihkan buffer luapan (dipanggil reset sendiri
// lewat ei_free polos - aman) + state operator, tanpa menyentuh
// arena. Harga: arena menetap di PSRAM selama robot hidup (memang
// selalu begitu di pola statis).
static void aiArenaKeepFree(void* ptr) { (void)ptr; }

// Jalan hanya saat diminta (endpoint /analyze). Foto JPEG yang sedang
// jalan diambil apa adanya lalu didekode ke RGB - setelan kamera TIDAK
// diubah, jadi stream & foto berkala dashboard tidak terganggu.
static int aiGetData(size_t offset, size_t length, float* out) {
  memcpy(out, aiFeatures + offset, length * sizeof(float));
  return 0;
}

// v7.6.0: inferensi dipisah dari pengambilan gambar (permintaan user
// 7 Nov: analisis BERTAHAP supaya ESP tidak gampang crash). Inti
// inferensi tinggal di aiInferCore(); sumber pikselnya bisa frame
// kamera langsung (runAiAnalysis) atau thumbnail 96x96 dari foto
// tersimpan (runAiAnalysisFromThumb) - dua-duanya mengisi aiFeatures
// lebih dulu. Perilaku inferensinya sendiri TIDAK berubah dari
// v7.5.2 (penanda crash, watchdog mati selama inferensi, model_reset
// sesudah run_classifier, catatan /aidet.csv).
static bool aiInferCore(uint32_t t0) {
  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  signal.get_data = &aiGetData;
  ei_impulse_result_t result = { 0 };
  // Penanda crash (v6.9.4): ditulis TEPAT sebelum inferensi, dihapus
  // TEPAT sesudah run_classifier kembali (hasil apa pun). Bila berkas
  // ini masih ada saat boot berikutnya, berarti board mati di tengah
  // inferensi - setup() akan menahan mode otomatis boot itu.
  if (fsReady) {
    File gf = LittleFS.open(AI_GUARD_PATH, FILE_WRITE);
    if (gf) { gf.printf("%lu\n", (unsigned long)g_bootNo); gf.close(); }
  }
  // Jejak inferensi + watchdog (v6.9.5): baris "mulai" mencatat sisa
  // stack & heap internal tepat sebelum classifier jalan. Watchdog
  // loopTask DIMATIKAN selama inferensi dan dinyalakan lagi tepat
  // sesudah run_classifier kembali.
  Serial.printf("[AI] inferensi mulai | stack bebas %u B | heap internal bebas %u B\n",
                (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)ESP.getFreeHeap());
  esp_task_wdt_reset();
  disableLoopWDT();
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
  // v6.9.8: jalur run_classifier tidak pernah memanggil model_reset
  // (jalur DSP SDK memanggilnya) -> bersihkan buffer luapan +
  // penghitung di sini, hanya bila blok NN benar-benar berjalan.
  // v7.6.3: free-nya diganti aiArenaKeepFree (arena tidak dibebaskan)
  // sesudah backtrace v7.6.2 membuktikan crash PANIC persis di
  // pembebasan arena oleh ei_aligned_free (lihat aiArenaKeepFree).
  if (err == EI_IMPULSE_OK || err == EI_IMPULSE_TFLITE_ERROR) {
    tflite_learn_1154536_5_reset(aiArenaKeepFree);
  }
  enableLoopWDT();
  if (fsReady) LittleFS.remove(AI_GUARD_PATH);
  Serial.printf("[AI] inferensi selesai | kode %d | %lu ms\n", (int)err, (unsigned long)(millis() - t0));
  if (err != EI_IMPULSE_OK) { Serial.printf("[AI] run_classifier gagal: %d\n", (int)err); return false; }
  g_ai.ms = millis() - t0;
  g_ai.valid = true;
  g_ai.ts = millis() / 1000;
  g_ai.karat = g_ai.cat = g_ai.logam = g_ai.lumut = 0;
  int best = 0;
  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    const char* lb = result.classification[i].label;
    float v = result.classification[i].value;
    if (v > result.classification[best].value) best = i;
    if      (!strcmp(lb, "karat"))        g_ai.karat = v;
    else if (!strcmp(lb, "cat_coklat"))   g_ai.cat = v;
    else if (!strcmp(lb, "logam_bersih")) g_ai.logam = v;
    else if (!strcmp(lb, "lumut_kerak"))  g_ai.lumut = v;
  }
  g_ai.label = result.classification[best].label;
  g_ai.score = result.classification[best].value;
  // Satu-satunya jalan hasil disebut kandidat korosi: kelas karat >= ambang.
  g_ai.kandidat = (g_ai.karat >= AI_RUST_THRESHOLD);
  appendAiRecord();
  return true;
}

bool runAiAnalysis(bool saveFoto) {
  if (!aiFeatures || !aiRgb) { Serial.println("[AI] buffer tidak siap"); return false; }
  // Cek ruang (v6.9.4): tensor arena EI kini tinggal di PSRAM - bila
  // ruang bebasnya tidak cukup, lewati analisis ini dengan aman
  // daripada memanggil run_classifier yang bisa gagal alokasi.
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 512UL * 1024UL) {
    Serial.println("[AI] dilewati: PSRAM bebas tidak cukup");
    return false;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { Serial.println("[AI] kamera sibuk"); return false; }
  int w = fb->width, h = fb->height;
  bool ok = fmt2rgb888(fb->buf, fb->len, fb->format, aiRgb);
  if (!ok) { esp_camera_fb_return(fb); Serial.println("[AI] dekode JPEG gagal"); return false; }
  // Squash-resize ke 96x96 (penskalaan langsung - sama seperti resize
  // mode "Squash" saat training di Edge Impulse) + kemas piksel RGB.
  for (int y = 0; y < EI_CLASSIFIER_INPUT_HEIGHT; y++) {
    int sy = y * h / EI_CLASSIFIER_INPUT_HEIGHT;
    for (int x = 0; x < EI_CLASSIFIER_INPUT_WIDTH; x++) {
      int sx = x * w / EI_CLASSIFIER_INPUT_WIDTH;
      const uint8_t* p = aiRgb + (sy * w + sx) * 3;
      aiFeatures[y * EI_CLASSIFIER_INPUT_WIDTH + x] =
          (float)((p[0] << 16) | (p[1] << 8) | p[2]);
    }
  }
  if (!aiInferCore(millis())) { esp_camera_fb_return(fb); return false; }
  // Foto bukti = frame yang sama dengan yang dianalisis; disimpan ke
  // SD hanya bila kandidat korosi (cooldown & kesiapan SD dicek di
  // dalam saveCandidatePhoto).
  if (saveFoto && g_ai.kandidat) saveCandidatePhoto(fb);
  esp_camera_fb_return(fb);
  Serial.printf("[AI] %s (%.2f) | karat %.2f cat %.2f logam %.2f lumut %.2f | kandidat=%s | %lu ms\n",
                g_ai.label.c_str(), g_ai.score, g_ai.karat, g_ai.cat,
                g_ai.logam, g_ai.lumut, g_ai.kandidat ? "YA" : "tidak",
                (unsigned long)g_ai.ms);
  return true;
}

// ===================== ANALISIS BERTAHAP (v7.6.0) =====================
// Alur baru dari user: (1) /capture-save mengambil foto dan
// menyimpannya ke SD (JPEG untuk dilihat + thumbnail RGB 96x96 untuk
// dianalisis) TANPA menjalankan inferensi sama sekali; (2)
// /analyze-photo menjalankan inferensi pada thumbnail foto yang
// SUDAH tersimpan - kamera tidak disentuh, sesi SD sudah ditutup
// sebelum inferensi mulai. Bila board mati di tengah langkah 2,
// foto + catatannya tetap utuh di SD dan bisa dianalisis ulang
// sesudah reboot; langkah 1 sendiri ringan (tidak ada inferensi)
// sehingga pengambilan foto tidak lagi ikut menanggung risiko crash.
volatile bool g_aiReqThumb = false;  // true: task AI memakai aiThumb

bool runAiAnalysisFromThumb() {
  if (!aiFeatures || !aiThumb) { Serial.println("[AI] buffer thumbnail tidak siap"); return false; }
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 512UL * 1024UL) {
    Serial.println("[AI] dilewati: PSRAM bebas tidak cukup");
    return false;
  }
  for (int i = 0; i < EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT; i++) {
    const uint8_t* p = aiThumb + i * 3;
    aiFeatures[i] = (float)((p[0] << 16) | (p[1] << 8) | p[2]);
  }
  bool ok = aiInferCore(millis());
  if (ok) {
    Serial.printf("[AI-FOTO] %s (%.2f) | kandidat=%s | %lu ms\n",
                  g_ai.label.c_str(), g_ai.score, g_ai.kandidat ? "YA" : "tidak",
                  (unsigned long)g_ai.ms);
  }
  return ok;
}

// ===================== TASK INFERENSI AI (v6.9.7) =============================
// Bukti board v6.9.6 (boot #125): alasan reset = PANIC di dalam
// run_classifier walau arena sudah di PSRAM dan watchdog loop sudah
// dimatikan - tersangka utama stack loopTask (8 KB, dipakai bersama
// WebServer) habis oleh rantai panggilan classifier. Karena itu
// runAiAnalysis TIDAK lagi dipanggil langsung dari handler/loop:
// ia hanya berjalan di task khusus ini (stack 32 KB), dipicu lewat
// semaphore. Isi runAiAnalysis sendiri tidak berubah sama sekali.
SemaphoreHandle_t g_aiStartSem = nullptr;  // penanda mulai: pemicu -> task AI
SemaphoreHandle_t g_aiDoneSem  = nullptr;  // penanda selesai: task AI -> penunggu
volatile bool g_aiBusy = false;            // true selama inferensi berjalan
bool g_aiReqSave = true;                   // argumen saveFoto untuk task AI

void aiTaskFn(void*) {
  for (;;) {
    xSemaphoreTake(g_aiStartSem, portMAX_DELAY);
    if (g_aiReqThumb) { g_aiReqThumb = false; runAiAnalysisFromThumb(); }
    else runAiAnalysis(g_aiReqSave);
    Serial.printf("[AI] stack task sisa: %u B\n",
                  (unsigned)uxTaskGetStackHighWaterMark(NULL));
    xSemaphoreGive(g_aiDoneSem);
    g_aiBusy = false;
  }
}

// Minta satu analisis ke task AI. Mengembalikan false bila inferensi
// sebelumnya masih berjalan (pemicu ditolak). waitDone=true: tunggu
// sampai task selesai (maks 30 detik; hasil timeout diabaikan dan
// permintaan tetap dianggap diterima).
bool requestAiAnalysis(bool saveFoto, bool waitDone) {
  if (g_aiBusy) return false;
  g_aiBusy = true;
  g_aiReqSave = saveFoto;
  xSemaphoreGive(g_aiStartSem);
  if (waitDone) xSemaphoreTake(g_aiDoneSem, pdMS_TO_TICKS(30000));
  return true;
}

// Pasangan requestAiAnalysis untuk jalur thumbnail: pemicu yang sama
// (semaphore + task AI khusus), sumber pikselnya aiThumb.
bool requestAiAnalysisThumb(bool waitDone) {
  if (g_aiBusy) return false;
  g_aiBusy = true;
  g_aiReqThumb = true;
  xSemaphoreGive(g_aiStartSem);
  if (waitDone) xSemaphoreTake(g_aiDoneSem, pdMS_TO_TICKS(30000));
  return true;
}

// ============================ WEB =============================================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CT = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// Deklarasi awal: dua pengaman ini dipakai juga dari dalam handleStream.
void pollBuzzer(unsigned long now);
void pollCharge(unsigned long now);
void pollMotors(unsigned long now);

void handleStream() {
  WiFiClient client = server.client();
  client.println("HTTP/1.1 200 OK");
  client.println(STREAM_CT);
  client.println("Access-Control-Allow-Origin: *");
  client.println();
  while (client.connected()) {
    // Servis pengaman DI DALAM loop stream: selama stream ditonton, loop
    // utama macet di sini - tanpa dua baris ini buzzer bisa membeku dalam
    // keadaan menyala (bunyi nonstop) dan watchdog motor berhenti bekerja.
    pollCharge(millis());
    pollBuzzer(millis());
    pollMotors(millis());
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { delay(10); continue; }
    client.printf(STREAM_PART, fb->len);
    client.write(fb->buf, fb->len);
    client.print(STREAM_BOUNDARY);
    esp_camera_fb_return(fb);
  }
}


// ===================== VIDEO LIVE WEB (v7.6.2) =====================
// Chrome Android memperlakukan navigasi langsung ke /stream sebagai
// UNDUHAN berkas (stream MJPEG tanpa halaman pembungkus). Halaman
// ini membungkus /stream dalam <img> supaya tampil sebagai video.
// Endpoint /stream sendiri TIDAK berubah (dipakai aplikasi Android).
static const char LIVE_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Video Live - FIR</title>
<style>
body{margin:0;background:#0A0F22;color:#E7ECFA;font-family:system-ui,sans-serif;text-align:center}
header{padding:14px;font-size:18px;font-weight:700;text-align:left}
img{width:100%;max-width:900px;display:block;margin:0 auto;background:#000}
a.btn{display:inline-block;margin:16px;padding:11px 22px;border-radius:10px;
background:linear-gradient(135deg,#3550C8,#5468D4);color:#fff;text-decoration:none;font-weight:600}
p{color:#A9B6DE;font-size:13px;padding:0 16px}
</style></head><body>
<header>FIR — Video Live</header>
<img src="/stream" alt="Video live robot">
<p>Video langsung dari kamera robot. Bila gambar berhenti, periksa koneksi WiFi ke robot lalu muat ulang halaman.</p>
<a class="btn" href="/">Kembali ke Dashboard</a>
</body></html>)rawliteral";

void handleLive() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", LIVE_PAGE);
}

// Satu frame JPEG per permintaan - untuk tampilan kamera dashboard
// mode foto berkala (v6.6). Header ditulis manual seperti handleStream.
void handleCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { server.send(503, "text/plain", "kamera sibuk"); return; }
  WiFiClient client = server.client();
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/jpeg");
  client.printf("Content-Length: %u\r\n", (unsigned)fb->len);
  client.println("Cache-Control: no-store");
  client.println();
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// v7.9.2: DETEKSI KAMERA dalam satu alamat - status init, kode error
// esp_camera_init (mentah + nama), identitas sensor dari PID-nya, dan
// uji ambil frame langsung saat alamat ini dibuka. Dibuat karena
// kegagalan kamera selama ini tidak meninggalkan jejak apa pun selain
// "kamera sibuk" di /capture.
//
// v7.9.3 TANGGA PEMULIHAN: bukti board 10 Nov - init selalu OK
// (PID OV5640 terbaca) tetapi grab selalu NULL, dan ribbon sudah
// terverifikasi duduk mentok. Bila uji grab di konfigurasi aktif
// gagal, endpoint ini menuruni tangga konfigurasi yang makin ringan
// (XCLK 16 MHz/VGA, lalu XCLK 10 MHz/QVGA 1 buffer): XCLK & ukuran
// frame turun = laju sinyal bus data turun = jauh lebih toleran
// terhadap jalur data yang marginal. Anak tangga yang berhasil
// DIBIARKAN AKTIF (jadi /capture & /stream ikut hidup lagi pada
// konfigurasi itu); bila semua gagal, konfigurasi produksi
// dipulihkan. Halaman bisa menjawab belasan detik (uji grab yang
// gagal menunggu timeout driver) - itu wajar.
void handleCamDiag() {
  size_t flen = 0; int fw = 0, fh = 0;
  int stage = 0;   // anak tangga yang akhirnya aktif & berhasil
  bool ok = false;
  String stages = "";
  if (g_camOk) ok = camGrabTry(2, &flen, &fw, &fh);
  stages += "{\"stage\":0,\"xclk_mhz\":" + String(g_camXclkHz / 1000000) +
            ",\"grab_ok\":" + String(ok ? 1 : 0) + "}";
  if (!ok) {
    static const struct { int xclk; framesize_t fs; const char* name; int fbc; camera_grab_mode_t gm; camera_fb_location_t loc; } LADDER[3] = {
      { 16000000, FRAMESIZE_VGA,  "VGA 640x480",       2, CAMERA_GRAB_LATEST,     CAMERA_FB_IN_PSRAM },
      { 10000000, FRAMESIZE_QVGA, "QVGA 320x240",      1, CAMERA_GRAB_WHEN_EMPTY, CAMERA_FB_IN_PSRAM },
      { 10000000, FRAMESIZE_QVGA, "QVGA 320x240 DRAM", 1, CAMERA_GRAB_WHEN_EMPTY, CAMERA_FB_IN_DRAM },
    };
    for (int s = 0; s < 3 && !ok; s++) {
      if (g_camOk) { esp_camera_deinit(); g_camOk = false; }
      bool initOk = initCameraCfg(LADDER[s].xclk, LADDER[s].fs, LADDER[s].fbc, LADDER[s].gm, LADDER[s].loc);
      bool g = false; size_t l2 = 0; int w2 = 0, h2 = 0;
      if (initOk) g = camGrabTry(2, &l2, &w2, &h2);
      stages += ",{\"stage\":" + String(s + 1) +
                ",\"xclk_mhz\":" + String(LADDER[s].xclk / 1000000) +
                ",\"framesize\":\"" + String(LADDER[s].name) + "\"" +
                ",\"init_ok\":" + String(initOk ? 1 : 0) +
                ",\"grab_ok\":" + String(g ? 1 : 0) + "}";
      if (g) { ok = true; stage = s + 1; flen = l2; fw = w2; fh = h2; }
    }
    if (!ok) {
      // Semua anak tangga gagal: pulihkan konfigurasi produksi.
      if (g_camOk) { esp_camera_deinit(); g_camOk = false; }
      initCamera();
    }
  }
  String j = "{";
  j += "\"init_ok\":" + String(g_camOk ? 1 : 0);
  j += ",\"err\":" + String(g_camErr);
  j += ",\"err_name\":\"" + String(esp_err_to_name((esp_err_t)g_camErr)) + "\"";
  j += ",\"pid\":" + String(g_camPid);
  if (g_camPid == 0x2642)      j += ",\"sensor\":\"OV2640\"";
  else if (g_camPid == 0x5640) j += ",\"sensor\":\"OV5640\"";
  else                         j += ",\"sensor\":\"-\"";
  j += ",\"grab_ok\":" + String(ok ? 1 : 0);
  j += ",\"frame_len\":" + String((unsigned)flen);
  if (ok) {
    j += ",\"frame_w\":" + String(fw);
    j += ",\"frame_h\":" + String(fh);
  }
  j += ",\"stage_aktif\":" + String(stage);
  j += ",\"stages\":[" + stages + "]";
  j += "}";
  server.send(200, "application/json", j);
}

// ---- /adminin (v7.10.0) ----
// Halaman admin override klasifikasi turbidity. SENGAJA hanya bisa
// diakses lewat alamat ini persis - tidak ada tautan dari dashboard
// (permintaan user 10 Nov). Membuka /adminin menampilkan keadaan
// sekarang + 4 tombol; /adminin?set=0..3 menerapkan pilihan
// (0 = otomatis ikut sensor, 1 = jernih, 2 = sedang, 3 = keruh),
// menyimpannya persisten, lalu kembali ke halaman ini.
// Cakupan override HANYA label klasifikasi di kartu Kekeruhan web:
// angka tegangan/persen, WQI, ALERT, dan log CSV tetap dari sensor.
void handleAdminin() {
  if (server.hasArg("set")) {
    int v = server.arg("set").toInt();
    if (v >= 0 && v <= 3) {
      g_turbOverride = v;
      saveTurbOverride();
      Serial.printf("[ADMIN] override klasifikasi turbidity -> %d\n", v);
    }
    server.sendHeader("Location", "/adminin");
    server.send(302, "text/plain", "");
    return;
  }
  static const char* names[4] = {"Otomatis (ikut sensor)", "Jernih", "Sedang", "Keruh"};
  String h;
  h += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>Admin - Klasifikasi Kekeruhan</title>";
  h += "<style>body{background:#0d1520;color:#e8eef4;font-family:sans-serif;margin:0;padding:20px}"
       ".box{max-width:460px;margin:24px auto;background:#14202e;border:1px solid #24405c;border-radius:14px;padding:20px}"
       "h2{margin:0 0 6px;font-size:19px}p{color:#9db4c6;font-size:13px;line-height:1.5}"
       "a.btn{display:block;text-decoration:none;text-align:center;padding:13px;margin:9px 0;border-radius:10px;"
       "background:#1b3049;color:#e8eef4;border:1px solid #2c4f73;font-size:16px}"
       "a.btn.on{background:#155e38;border-color:#1f8a52;font-weight:bold}"
       ".cur{font-size:15px;margin:10px 0}</style></head><body><div class='box'>";
  h += "<h2>Admin - Klasifikasi Kekeruhan</h2>";
  h += "<p>Override ini hanya mengubah <b>label klasifikasi</b> yang tampil di kartu Kekeruhan dashboard "
       "(Jernih / Sedang / Keruh). Angka tegangan, WQI, peringatan, dan log tetap dari sensor. "
       "Pilihan tersimpan dan tetap berlaku sesudah robot dimatikan.</p>";
  h += "<div class='cur'>Sensor sekarang membaca: <b>" + g_turbLevel + "</b> (" + String(g_turbV, 2) + " V)</div>";
  h += "<div class='cur'>Override aktif: <b>" + String(names[g_turbOverride]) + "</b></div>";
  for (int i = 0; i < 4; i++) {
    h += "<a class='btn";
    if (i == g_turbOverride) h += " on";
    h += "' href='/adminin?set=" + String(i) + "'>" + String(names[i]) + "</a>";
  }
  h += "</div></body></html>";
  server.send(200, "text/html", h);
}

// ============================ BATERAI (v7.3.2) ============================
// Jalur ukur baterai BELUM terpasang di robot ini: divider
// 20k/20k dari B+ TP4056 berhenti di terminal (keputusan akhir
// BAT-MON - tidak ada pin ADC bebas di ESP; GPIO19/20 = USB).
// Karena itu readBatteryMv() mengembalikan -1 (belum terukur)
// dan telemetri melaporkan battery_pct -1; aplikasi/dashbord
// menampilkannya sebagai "belum terukur", BUKAN angka karangan.
// Cara menghidupkan sungguhan: pasang modul ADC ADS1115
// (alamat 0x48) di bus I2C yang sama (SDA47/SCL48), sambungkan
// simpul divider ke A0 ADS1115, lalu ganti isi readBatteryMv()
// menjadi pembacaan A0 x faktor divider (x2) x faktor kalibrasi.
int readBatteryMv() { return -1; }   // -1 = belum terukur (jalur belum terpasang)

// Persen sel Li-ion 18650 tunggal dari tegangan: 4,20 V = 100%,
// 3,30 V = 0% (linear kasar - cukup untuk indikator; sempurnakan
// dengan kurva ukur bila jalur ADC sudah terpasang).
int batteryPctFromMv(int mv) {
  if (mv < 0) return -1;
  if (mv >= 4200) return 100;
  if (mv <= 3300) return 0;
  return (mv - 3300) * 100 / 900;
}

String aiJsonFields() {
  String j = "\"ai_valid\":" + String(g_ai.valid ? "true" : "false") + ",";
  j += "\"ai_label\":\"" + g_ai.label + "\",";
  j += "\"ai_score\":" + String(g_ai.score, 2) + ",";
  j += "\"ai_karat\":" + String(g_ai.karat, 2) + ",";
  j += "\"ai_cat\":" + String(g_ai.cat, 2) + ",";
  j += "\"ai_logam\":" + String(g_ai.logam, 2) + ",";
  j += "\"ai_lumut\":" + String(g_ai.lumut, 2) + ",";
  j += "\"ai_kandidat\":" + String(g_ai.kandidat ? "true" : "false") + ",";
  j += "\"ai_ms\":" + String((unsigned long)g_ai.ms) + ",";
  j += "\"ai_count\":" + String(g_aiLogCount) + ",";
  j += "\"ai_auto\":" + String(g_aiAuto ? "true" : "false") + ",";
  j += "\"sd_ready\":" + String(g_sdReady ? "true" : "false") + ",";
  j += "\"ai_photos\":" + String(g_aiPhotos) + ",";
  j += "\"boot\":" + String((unsigned long)g_bootNo);
  return j;
}

extern int g_tgtL, g_tgtR, g_curL, g_curR;  // v7.3.9: saksi motor - didefinisikan di blok MOTOR di bawah
String telemetryJson() {
  String j = "{";
  j += "\"temperature_c\":" + String(g_tempC, 1) + ",";
  j += "\"turbidity_v\":"   + String(g_turbV, 2) + ",";
  j += "\"turbidity_pct\":" + String(g_turbPct) + ",";
  // v7.10.0: label klasifikasi efektif - override /adminin menang atas
  // klasifikasi sensor; angka V/pct/fault di bawah tetap apa adanya.
  String turbLevelEff = (g_turbOverride >= 1 && g_turbOverride <= 3)
      ? String(g_turbOverride == 1 ? "jernih" : (g_turbOverride == 2 ? "sedang" : "keruh"))
      : g_turbLevel;
  j += "\"turbidity_level\":\"" + turbLevelEff + "\",";
  j += "\"turbidity_override\":" + String(g_turbOverride) + ",";
  j += "\"turbidity_fault\":" + String(g_turbFault ? "true" : "false") + ",";
  j += "\"turbidity_src\":\"PROBE\",";   // v7.2.3: sumber kekeruhan = fototransistor probe dibaca LANGSUNG di GPIO1 (bypass tahap A/D adaptor yang mati)
  j += "\"tds_ppm\":" + String(g_tdsPpm, 1) + ",";
  j += "\"wqi\":" + String(g_wqi) + ",";
  j += "\"wqi_cat\":\"" + g_wqiCat + "\",";
  j += "\"leak\":" + String(g_leak ? "true" : "false") + ",";
  j += "\"leak_raw\":" + String(g_leakRaw) + ",";
  // v7.10.0: field roll_deg/pitch_deg/mpu dibuang (gyro dihapus user).
  j += "\"rain\":" + String(g_rain ? "true" : "false") + ",";
  j += "\"alert\":" + String(g_alert ? "true" : "false") + ",";
  j += "\"alert_msg\":\"" + g_alertMsg + "\",";
  j += "\"rssi_dbm\":" + String(WiFi.RSSI()) + ",";
  j += "\"battery_mv\":" + String(readBatteryMv()) + ",";
  j += "\"battery_pct\":" + String(batteryPctFromMv(readBatteryMv())) + ",";
  // v7.4.2: saksi status cas - chg_raw = level pin CHRG saat ini
  // juga (1 = pin LOW), chg = status stabil hasil gerbang 5 detik.
  j += "\"chg_raw\":" + String(digitalRead(CHG_PIN) == LOW ? 1 : 0) + ",";
  j += "\"chg\":" + String(g_chgCharging ? "true" : "false") + ",";
  j += "\"uptime_s\":" + String(millis() / 1000) + ",";
  j += "\"log_count\":" + String(ramCount) + ",";
  // v7.3.9: saksi motor untuk dashboard - target & duty terapan
  // program + apakah perintah sedang ditahan interlock sesi SD.
  j += "\"motor_tgt_l\":" + String(g_tgtL) + ",";
  j += "\"motor_tgt_r\":" + String(g_tgtR) + ",";
  j += "\"motor_cur_l\":" + String(g_curL) + ",";
  j += "\"motor_cur_r\":" + String(g_curR) + ",";
  j += "\"motor_hold\":" + String(g_sdSession ? "true" : "false") + ",";
  j += aiJsonFields();
  j += "}";
  return j;
}
void handleTelemetry() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", telemetryJson());
}

void handleHistory() {
  // v6.8.1: grafik tren hanya menampilkan SESI BOOT yang sedang berjalan.
  // Record warisan sesi lama ts-nya mulai dari 0 lagi - menggambarnya dalam
  // satu garis membuat tren tampak kusut. Riwayat lengkap tetap ada di
  // unduhan CSV (/download).
  int n = ramCount - ramSessionBase;
  String json;
  json.reserve(64 + n * 60);
  json = "{\"records\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ',';
    const EnvLogRecord& r = ramLogs[ramIndexFromOldest(ramSessionBase + i)];
    json += "{\"ts\":" + String((unsigned long)r.ts);
    json += ",\"t\":" + String(r.tempC, 2);
    json += ",\"tb\":" + String(r.turbV, 2);
    json += ",\"tds\":" + String(r.tdsPpm, 1);
    json += ",\"lk\":" + String(r.leak) + ",\"wqi\":" + String(r.wqi) + "}";
  }
  json += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleDownload() {
  if (!fsReady || !LittleFS.exists(LOG_PATH)) {
    server.send(404, "text/plain", "Log belum tersedia.");
    return;
  }
  File f = LittleFS.open(LOG_PATH, FILE_READ);
  server.sendHeader("Content-Disposition", "attachment; filename=firlog.csv");
  server.streamFile(f, "text/csv");
  f.close();
}

// Tombol "Analisis Foto" di dashboard: jalankan inferensi AI sekali
// pada frame kamera saat ini, simpan ke riwayat, kembalikan hasilnya.
void handleAnalyze() {
  server.sendHeader("Cache-Control", "no-store");
  sdSessionForceEnd();  // v7.7.0: task AI mungkin menulis foto kandidat - butuh mutex sesi
  if (!requestAiAnalysis(true, true)) {
    server.send(503, "application/json", "{\"ok\":false}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true," + aiJsonFields() + "}");
}


// ===================== RIWAYAT FOTO (v7.6.0) =====================
// Indeks foto bertahap: /fir/foto/index.csv di SD card, satu baris
// per foto: nama,boot,label,skor (label+skor kosong = belum
// dianalisis). Semua helper indeks WAJIB dipanggil di dalam sesi SD.
#define AI_THUMB_BYTES (EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3)

static String fotoIndexPath() { return String(SD_FOTO_DIR) + "/index.csv"; }

static bool fotoValidName(const String& f) {
  if (f.length() < 6 || f.length() > 40) return false;
  if (!f.endsWith(".jpg")) return false;
  if (f.indexOf("..") >= 0) return false;
  for (size_t i = 0; i < f.length(); i++) {
    char c = f[i];
    bool okc = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!okc) return false;
  }
  return true;
}

static String fotoThumbName(const String& jpg) {
  return jpg.substring(0, jpg.length() - 4) + ".rgb";
}

static String fotoIndexRead() {
  String path = fotoIndexPath();
  if (!SD_MMC.exists(path.c_str())) return String("");
  File f = SD_MMC.open(path.c_str(), FILE_READ);
  if (!f) return String("");
  String s;
  while (f.available()) s += (char)f.read();
  f.close();
  return s;
}

static bool fotoIndexWrite(const String& content) {
  String tmp = String(SD_FOTO_DIR) + "/index.tmp";
  File f = SD_MMC.open(tmp.c_str(), FILE_WRITE);
  if (!f) return false;
  f.print(content);
  f.close();
  String path = fotoIndexPath();
  SD_MMC.remove(path.c_str());
  return SD_MMC.rename(tmp.c_str(), path.c_str());
}

static void fotoIndexAppend(const String& name) {
  File f = SD_MMC.open(fotoIndexPath().c_str(), FILE_APPEND);
  if (!f) return;
  f.printf("%s,%lu,,,%lu\n", name.c_str(), (unsigned long)g_bootNo, (unsigned long)nowEpoch());
  f.close();
  if (g_idxCacheValid) {  // v7.6.9: cerminkan ke cache RAM
    if (g_idxCache.length() && !g_idxCache.endsWith("\n")) g_idxCache += "\n";
    char line[96];
    snprintf(line, sizeof(line), "%s,%lu,,,%lu\n", name.c_str(), (unsigned long)g_bootNo, (unsigned long)nowEpoch());
    g_idxCache += line;
  }
}

static void fotoIndexSetResult(const String& name, const String& label, float score) {
  String all = fotoIndexRead();
  String out;
  int pos = 0;
  while (pos <= (int)all.length()) {
    int nl = all.indexOf('\n', pos);
    String line = (nl < 0) ? all.substring(pos) : all.substring(pos, nl);
    if (nl < 0) pos = (int)all.length() + 1; else pos = nl + 1;
    line.trim();
    if (!line.length()) continue;
    if (line.startsWith(name + ",")) {
      int c1 = line.indexOf(',');
      int c2 = (c1 >= 0) ? line.indexOf(',', c1 + 1) : -1;
      String boot = (c2 > c1) ? line.substring(c1 + 1, c2) : String((unsigned long)g_bootNo);
      int c3 = (c2 >= 0) ? line.indexOf(',', c2 + 1) : -1;
      int c4 = (c3 >= 0) ? line.indexOf(',', c3 + 1) : -1;
      String epoch = "0";  // v7.8.0: pertahankan cap waktu ambil (kolom 5)
      if (c4 > c3) {
        String rest = line.substring(c4 + 1);
        int c5 = rest.indexOf(',');
        epoch = (c5 >= 0) ? rest.substring(0, c5) : rest;
        if (!epoch.length()) epoch = "0";
      }
      char sc[12];
      snprintf(sc, sizeof(sc), "%.2f", score);
      out += name + "," + boot + "," + label + "," + sc + "," + epoch + "," +
             String((unsigned long)nowEpoch()) + "\n";
    } else {
      out += line + "\n";
    }
    if (nl < 0) break;
  }
  fotoIndexWrite(out);
  g_idxCache = out;  // v7.6.9: cache mengikuti isi berkas terbaru
  g_idxCacheValid = true;
}

// v7.8.0: ganti nama foto di indeks (isi kolom lain dipertahankan).
static void fotoIndexRename(const String& oldName, const String& newName) {
  String all = fotoIndexRead();
  String out;
  int pos = 0;
  while (pos <= (int)all.length()) {
    int nl = all.indexOf('\n', pos);
    String line = (nl < 0) ? all.substring(pos) : all.substring(pos, nl);
    if (nl < 0) pos = (int)all.length() + 1; else pos = nl + 1;
    line.trim();
    if (!line.length()) continue;
    if (line.startsWith(oldName + ",")) out += newName + line.substring(oldName.length()) + "\n";
    else out += line + "\n";
    if (nl < 0) break;
  }
  fotoIndexWrite(out);
  g_idxCache = out;
  g_idxCacheValid = true;
}

static void fotoIndexRemove(const String& name) {
  String all = fotoIndexRead();
  String out;
  int pos = 0;
  while (pos <= (int)all.length()) {
    int nl = all.indexOf('\n', pos);
    String line = (nl < 0) ? all.substring(pos) : all.substring(pos, nl);
    if (nl < 0) pos = (int)all.length() + 1; else pos = nl + 1;
    line.trim();
    if (line.length() && !line.startsWith(name + ",")) out += line + "\n";
    if (nl < 0) break;
  }
  fotoIndexWrite(out);
  g_idxCache = out;  // v7.6.9: cache mengikuti isi berkas terbaru
  g_idxCacheValid = true;
}

// LANGKAH 1 (v7.6.0): ambil foto SEKARANG, simpan JPEG + thumbnail
// RGB 96x96 ke SD, catat ke index.csv sebagai belum dianalisis.
// Inferensi TIDAK dijalankan di sini - langkah ini ringan.
void handleCaptureSave() {
  server.sendHeader("Cache-Control", "no-store");
  if (g_aiBusy) { server.send(503, "application/json", "{\"ok\":false,\"err\":\"ai-busy\"}"); return; }
  if (!aiThumb || !aiRgb) { Serial.printf("[FOTO] buffer belum siap: thumb=%p rgb=%p\n", (void*)aiThumb, (void*)aiRgb); server.send(500, "application/json", "{\"ok\":false,\"err\":\"buffer\"}"); return; }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { server.send(503, "application/json", "{\"ok\":false,\"err\":\"camera\"}"); return; }
  bool ok = fmt2rgb888(fb->buf, fb->len, fb->format, aiRgb);
  if (ok) {
    int w = fb->width, h = fb->height;
    for (int y = 0; y < EI_CLASSIFIER_INPUT_HEIGHT; y++) {
      int sy = y * h / EI_CLASSIFIER_INPUT_HEIGHT;
      for (int x = 0; x < EI_CLASSIFIER_INPUT_WIDTH; x++) {
        int sx = x * w / EI_CLASSIFIER_INPUT_WIDTH;
        memcpy(aiThumb + (y * EI_CLASSIFIER_INPUT_WIDTH + x) * 3,
               aiRgb + (sy * w + sx) * 3, 3);
      }
    }
  }
  if (!ok) { esp_camera_fb_return(fb); server.send(503, "application/json", "{\"ok\":false,\"err\":\"decode\"}"); return; }
  char name[40];
  snprintf(name, sizeof(name), "u%lu_%06lu.jpg",
           (unsigned long)g_bootNo, (unsigned long)(millis() / 1000));
  bool wok = false;
  sdSessionBegin();
  if (g_sdMounted) {
    String jp = String(SD_FOTO_DIR) + "/" + name;
    File pf = SD_MMC.open(jp.c_str(), FILE_WRITE);
    if (pf) {
      pf.write(fb->buf, fb->len);
      pf.close();
      // v7.6.9: salinan JPEG ke RAM untuk preview tanpa sesi SD.
      if (fb->len <= 262144) {
        uint8_t* nb = (uint8_t*)ps_malloc(fb->len);
        if (!nb) nb = (uint8_t*)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!nb) nb = (uint8_t*)malloc(fb->len);
        if (nb) {
          memcpy(nb, fb->buf, fb->len);
          if (g_lastJpg) free(g_lastJpg);
          g_lastJpg = nb; g_lastJpgLen = fb->len; g_lastJpgName = String(name);
        }
      }
      String tp = String(SD_FOTO_DIR) + "/" + fotoThumbName(String(name));
      File tf = SD_MMC.open(tp.c_str(), FILE_WRITE);
      if (tf) {
        tf.write(aiThumb, AI_THUMB_BYTES);
        tf.close();
        fotoIndexAppend(String(name));
        wok = true;
        g_thumbName = String(name);  // v7.6.7: aiThumb kini memegang foto ini
      }
    }
  }
  sdSessionEnd();
  esp_camera_fb_return(fb);
  if (!wok) { server.send(503, "application/json", "{\"ok\":false,\"err\":\"sd\"}"); return; }
  Serial.printf("[FOTO] tersimpan: %s\n", name);
  { char lb3[80]; snprintf(lb3, sizeof(lb3), "Foto tersimpan: %s", name); logEvent(lb3); }
  server.send(200, "application/json", String("{\"ok\":true,\"file\":\"") + name + "\"}");
}

// Daftar riwayat foto (terbaru dulu, maks 60 entri terakhir terbaca).
void handlePhotos() {
  server.sendHeader("Cache-Control", "no-store");
  String idx;
  bool sdok = false;
  // v7.6.9: indeks dilayani dari cache RAM bila ada - tanpa sesi SD.
  if (g_idxCacheValid) { idx = g_idxCache; sdok = true; }
  else {
    sdSessionBegin();
    if (g_sdMounted) { idx = fotoIndexRead(); g_idxCache = idx; g_idxCacheValid = true; sdok = true; }
    sdSessionEnd();
  }
  String lines[60];
  int n = 0;
  int pos = 0;
  while (pos <= (int)idx.length() && n < 60) {
    int nl = idx.indexOf('\n', pos);
    String line = (nl < 0) ? idx.substring(pos) : idx.substring(pos, nl);
    if (nl < 0) pos = (int)idx.length() + 1; else pos = nl + 1;
    line.trim();
    if (line.length()) lines[n++] = line;
    if (nl < 0) break;
  }
  String out = "{\"ok\":true,\"sd\":";
  out += sdok ? "true" : "false";
  out += ",\"now\":" + String((unsigned long)nowEpoch()) +
         ",\"synced\":" + String(g_timeSynced ? "true" : "false");
  out += ",\"photos\":[";
  bool first = true;
  for (int i = n - 1; i >= 0; i--) {
    String ln = lines[i];
    int c1 = ln.indexOf(',');
    if (c1 < 0) continue;
    int c2 = ln.indexOf(',', c1 + 1);
    int c3 = (c2 >= 0) ? ln.indexOf(',', c2 + 1) : -1;
    int c4 = (c3 >= 0) ? ln.indexOf(',', c3 + 1) : -1;
    int c5 = (c4 >= 0) ? ln.indexOf(',', c4 + 1) : -1;
    String nm = ln.substring(0, c1);
    String bt = (c2 > c1) ? ln.substring(c1 + 1, c2) : String("0");
    String lb = (c3 > c2) ? ln.substring(c2 + 1, c3) : String("");
    String sc = (c3 >= 0) ? ln.substring(c3 + 1, (c4 >= 0) ? c4 : ln.length()) : String("");
    String ep  = (c4 > c3) ? ln.substring(c4 + 1, (c5 >= 0) ? c5 : ln.length()) : String("0");
    String epa = (c5 >= 0) ? ln.substring(c5 + 1) : String("0");
    if (!ep.length()) ep = "0";
    if (!epa.length()) epa = "0";
    if (!first) out += ",";
    first = false;
    out += "{\"f\":\"" + nm + "\",\"boot\":" + bt +
           ",\"label\":\"" + lb + "\",\"score\":" + (sc.length() ? sc : String("null")) +
           ",\"done\":" + (lb.length() ? "true" : "false") +
           ",\"ts\":" + ep + ",\"tsa\":" + epa + "}";
  }
  out += "]}";
  server.send(200, "application/json", out);
}

// v7.8.0: log peristiwa robot untuk panel Log di sidebar web.
void handleLog() {
  server.sendHeader("Cache-Control", "no-store");
  String out = "{\"ok\":true,\"synced\":";
  out += g_timeSynced ? "true" : "false";
  out += ",\"now\":" + String((unsigned long)nowEpoch()) + ",\"log\":[";
  bool first = true;
  for (int k = 0; k < g_logCount; k++) {
    int idx = (g_logHead + LOG_MAX - 1 - k) % LOG_MAX;
    const LogEnt& e = g_logBuf[idx];
    if (!first) out += ",";
    first = false;
    out += "{\"t\":" + String(e.epoch) + ",\"up\":" + String(e.up) +
           ",\"m\":\"" + escJson(String(e.msg)) + "\"}";
  }
  out += "]}";
  server.send(200, "application/json", out);
}

// v7.8.0: sinkron jam dari HP/browser (epoch detik). Tanpa internet.
void handleTimeSet() {
  server.sendHeader("Cache-Control", "no-store");
  if (server.hasArg("e")) {
    bool was = g_timeSynced;
    clockSetEpoch(strtoul(server.arg("e").c_str(), nullptr, 10), "HP/browser");
    if (!was && g_timeSynced) logEvent("Jam tersinkron dari HP/browser");
  }
  server.send(200, "application/json",
              String("{\"ok\":true,\"synced\":") + (g_timeSynced ? "true" : "false") +
              ",\"now\":" + String((unsigned long)nowEpoch()) + "}");
}

// Sajikan satu foto riwayat (JPEG) dari folder foto SD.
void handlePhotoGet() {
  String name = server.hasArg("f") ? server.arg("f") : "";
  if (!fotoValidName(name)) { server.send(400, "text/plain", "Nama berkas tidak valid."); return; }
  // v7.6.9: foto terakhir dilayani dari cache RAM (preview sesudah
  // Langkah 1) - tanpa sesi SD. Foto lain tetap dari kartu.
  if (g_lastJpg && name == g_lastJpgName) {
    server.setContentLength(g_lastJpgLen);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "image/jpeg", "");
    WiFiClient client = server.client();
    size_t sent = 0;
    while (sent < g_lastJpgLen) {
      size_t chunk = g_lastJpgLen - sent; if (chunk > 1024) chunk = 1024;
      int n = client.write(g_lastJpg + sent, chunk);
      if (n <= 0) break;
      sent += (size_t)n;
    }
    return;
  }
  String path = String(SD_FOTO_DIR) + "/" + name;
  sdSessionBegin();
  if (!g_sdMounted) { sdSessionEnd(); server.send(503, "text/plain", "SD card tidak terpasang / tidak terbaca."); return; }
  File f = SD_MMC.open(path.c_str(), FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    sdSessionEnd();
    server.send(404, "text/plain", "Berkas tidak ditemukan.");
    return;
  }
  size_t total = f.size();
  server.setContentLength(total);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "image/jpeg", "");
  WiFiClient client = server.client();
  uint8_t buf[1024];
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    client.write(buf, (size_t)n);
  }
  f.close();
  sdSessionEnd();
}

// LANGKAH 2 (v7.6.0): analisis foto yang SUDAH tersimpan. Thumbnail
// dibaca dari SD lalu sesi DITUTUP sebelum inferensi mulai - kamera
// tidak disentuh sama sekali di langkah ini. Hasil ditulis balik ke
// index.csv (di sesi terpisah sesudah inferensi selesai).
void handleAnalyzePhoto() {
  server.sendHeader("Cache-Control", "no-store");
  String name = server.hasArg("f") ? server.arg("f") : "";
  if (!fotoValidName(name)) { server.send(400, "application/json", "{\"ok\":false,\"err\":\"name\"}"); return; }
  if (g_aiBusy) { server.send(503, "application/json", "{\"ok\":false,\"err\":\"ai-busy\"}"); return; }
  if (!aiThumb) { Serial.printf("[AI] buffer thumbnail null\n"); server.send(500, "application/json", "{\"ok\":false,\"err\":\"buffer\"}"); return; }
  // v7.6.7: bila foto yang diminta adalah foto yang thumbnail-nya
  // MASIH di aiThumb (baru diambil Langkah 1 di boot yang sama),
  // analisis langsung dari RAM - TANPA sesi SD. Alasan terbukti log
  // user 7 Nov 09:40: sesi simpan mount lolos, sesi analisis kena
  // 0x107 sebelum inferensi sempat mulai; tiap sesi adalah undian
  // mount, jadi jalur utama tidak boleh bergantung padanya. Jalur SD
  // tetap untuk foto lama / sesudah reboot (dari menu Riwayat Foto).
  bool rd = false;
  if (g_thumbName == name) {
    rd = true;
    Serial.printf("[AI] %s dianalisis dari salinan RAM (tanpa sesi SD)\n", name.c_str());
  } else {
    sdSessionBegin();
    if (g_sdMounted) {
      String tp = String(SD_FOTO_DIR) + "/" + fotoThumbName(name);
      bool jpgAda = SD_MMC.exists((String(SD_FOTO_DIR) + "/" + name).c_str());
      File tf = jpgAda ? SD_MMC.open(tp.c_str(), FILE_READ) : File();
      if (tf) {
        if ((long)tf.size() == (long)AI_THUMB_BYTES) {
          size_t got = 0;
          while (got < (size_t)AI_THUMB_BYTES) {
            int n = tf.read(aiThumb + got, (size_t)AI_THUMB_BYTES - got);
            if (n <= 0) break;
            got += (size_t)n;
          }
          rd = (got == (size_t)AI_THUMB_BYTES);
        }
        tf.close();
      }
    }
    sdSessionEnd();
    if (rd) g_thumbName = name;  // aiThumb kini memegang foto ini
  }
  if (!rd) { server.send(404, "application/json", "{\"ok\":false,\"err\":\"thumb\"}"); return; }
  g_ai.valid = false;  // hasil lama tidak boleh terbaca sebagai hasil foto ini
  if (!requestAiAnalysisThumb(true)) { server.send(503, "application/json", "{\"ok\":false,\"err\":\"ai-busy\"}"); return; }
  String finalName = name;
  if (g_ai.valid) {
    sdSessionBegin();
    if (g_sdMounted) {
      fotoIndexSetResult(name, g_ai.label, g_ai.score);
      // v7.8.0: nama foto berubah mengikuti hasil analisis.
      String nn = g_ai.label + "_" + name;
      if (!name.startsWith(g_ai.label + "_") && fotoValidName(nn)) {
        String dir = String(SD_FOTO_DIR) + "/";
        if (SD_MMC.rename((dir + name).c_str(), (dir + nn).c_str())) {
          SD_MMC.rename((dir + fotoThumbName(name)).c_str(), (dir + fotoThumbName(nn)).c_str());
          fotoIndexRename(name, nn);
          finalName = nn;
          if (g_thumbName == name) g_thumbName = nn;
          if (g_lastJpgName == name) g_lastJpgName = nn;
          Serial.printf("[AI] nama foto berubah: %s -> %s\n", name.c_str(), nn.c_str());
        }
      }
    }
    sdSessionEnd();
    char lb2[128];
    snprintf(lb2, sizeof(lb2), "Analisis %s: %s (%.2f)", finalName.c_str(), g_ai.label.c_str(), g_ai.score);
    logEvent(lb2);
  }
  server.send(200, "application/json",
              "{\"ok\":true,\"file\":\"" + name + "\",\"newfile\":\"" + finalName + "\"," + aiJsonFields() + "}");
}

// Hapus satu foto riwayat (JPEG + thumbnail + baris indeks).
void handlePhotoDel() {
  server.sendHeader("Cache-Control", "no-store");
  String name = server.hasArg("f") ? server.arg("f") : "";
  if (!fotoValidName(name)) { server.send(400, "application/json", "{\"ok\":false}"); return; }
  sdSessionBegin();
  if (g_sdMounted) {
    SD_MMC.remove((String(SD_FOTO_DIR) + "/" + name).c_str());
    SD_MMC.remove((String(SD_FOTO_DIR) + "/" + fotoThumbName(name)).c_str());
    fotoIndexRemove(name);
  }
  sdSessionEnd();
  server.send(200, "application/json", "{\"ok\":true}");
}

// Saklar mode otomatis (computer vision) dari dashboard.
void handleAiAuto() {
  if (server.hasArg("on")) {
    g_aiAuto = (server.arg("on") == "1");
    saveAiAutoSetting();
    Serial.printf("[AI] mode otomatis: %s\n", g_aiAuto ? "AKTIF" : "mati");
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true," + aiJsonFields() + "}");
}

void handleDownloadAi() {
  if (!fsReady || !LittleFS.exists(AI_LOG_PATH)) {
    server.send(404, "text/plain", "Riwayat AI belum tersedia.");
    return;
  }
  File f = LittleFS.open(AI_LOG_PATH, FILE_READ);
  server.sendHeader("Content-Disposition", "attachment; filename=aidet.csv");
  server.streamFile(f, "text/csv");
  f.close();
}

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FIR - Monitoring Lingkungan</title>
<style>
 *{box-sizing:border-box}
 html{scroll-behavior:smooth}
 body{font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color:#E9EDFA;margin:0;
   background:#070B1A;background-attachment:fixed;
   background-image:radial-gradient(1100px 550px at 78% -10%,#26306B 0%,rgba(38,48,107,0) 60%),
   radial-gradient(900px 520px at -12% 112%,#1E1448 0%,rgba(30,20,72,0) 55%),
   linear-gradient(180deg,#0A0F22 0%,#070B1A 100%)}
 svg.ic{width:19px;height:19px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round;flex:none}
 /* ===== SIDEBAR ===== */
 .sidebar{position:fixed;top:0;left:0;bottom:0;width:236px;color:#C9D2EE;z-index:60;
   background:linear-gradient(180deg,#0D1434 0%,#080D20 100%);border-right:1px solid rgba(255,255,255,.07);
   display:flex;flex-direction:column;padding:18px 14px 14px;transition:transform .25s ease}
 .brand{display:flex;align-items:center;gap:11px;padding:2px 8px 15px;border-bottom:1px solid rgba(255,255,255,.1);margin-bottom:12px}
 .logoimg{width:44px;height:44px;border-radius:12px;object-fit:cover;flex:none}
 .brand .bt{font-weight:800;font-size:14.5px;color:#fff;line-height:1.2;letter-spacing:.3px}
 .brand .bs{font-size:11px;color:#8E9AC0;letter-spacing:.4px;text-transform:uppercase;margin-top:2px}
 .photocard{border-top:1px solid rgba(255,255,255,.08);padding:12px 0}
.photocard img{width:100%;border-radius:12px;display:block;margin-bottom:8px}
 .pgroup{margin:16px 0 4px;padding:8px 12px;border-radius:10px;background:linear-gradient(90deg,#1B2450,#101736);border:1px solid rgba(255,255,255,.09);font-weight:800;font-size:13.5px;letter-spacing:.4px;color:#fff;text-transform:uppercase}
 .confbar{height:8px;border-radius:99px;background:rgba(255,255,255,.12);overflow:hidden;margin:6px 0 2px}
 .conffill{height:100%;border-radius:99px;background:linear-gradient(90deg,#2FBF9B,#7BE0C3)}
 .logwrap{margin-top:14px;border-top:1px solid rgba(255,255,255,.1);padding-top:10px}
 .logtitle{font-weight:800;font-size:12.5px;letter-spacing:.5px;text-transform:uppercase;color:#fff;margin:0 2px 7px}
 .logbox{max-height:220px;overflow-y:auto;display:flex;flex-direction:column;gap:5px;padding-right:2px}
 .logline{font-size:11.5px;line-height:1.45;color:#A9B6DE;background:rgba(255,255,255,.05);border-radius:8px;padding:5px 8px;word-break:break-word}
 .logtime{display:block;color:#7BE0C3;font-size:10.5px;font-weight:700}
.nav{display:flex;flex-direction:column;gap:3px}
 .nav a{display:flex;align-items:center;gap:11px;padding:9px 11px;border-radius:10px;color:#A9B6DE;
   text-decoration:none;font-size:14px;font-weight:600}
 .nav a:hover{background:rgba(255,255,255,.07);color:#fff}
 .nav a.active{background:linear-gradient(135deg,#3550C8,#5468D4);color:#fff}
 .nav .nlabel{font-size:11px;letter-spacing:.9px;text-transform:uppercase;color:#66739F;margin:14px 11px 5px;font-weight:700}
 .side-foot{margin-top:auto;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.09);
   border-radius:12px;padding:10px 12px;font-size:12px;color:#A9B6DE;line-height:1.55}
 .side-foot .dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#3ECF6E;margin-right:6px}
 .side-foot b{color:#fff}
 /* ===== TOPBAR (HP) ===== */
 .topbar{display:none;position:fixed;top:0;left:0;right:0;height:56px;background:rgba(9,13,30,.94);color:#fff;z-index:55;
   align-items:center;gap:12px;padding:0 14px;border-bottom:1px solid rgba(255,255,255,.07)}
 .topbar .logoimg{width:34px;height:34px;border-radius:9px}
 .topbar .tt{font-weight:800;font-size:14px;letter-spacing:.3px}
 .topbar button{margin-left:auto;background:none;border:none;color:#fff;cursor:pointer;padding:8px;display:flex}
 .overlay{display:none;position:fixed;inset:0;background:rgba(3,5,12,.55);z-index:58}
 /* ===== KONTEN ===== */
 .main{margin-left:236px;padding:22px 24px 40px;min-width:0}
 .wrap{max-width:1020px;margin:0 auto}
 header{display:flex;align-items:center;gap:12px;margin-bottom:4px}
 h1{font-size:22px;margin:0;color:#fff;font-weight:800;letter-spacing:.2px}
 .sub{color:#8E9AC0;font-size:13px;margin:2px 0 16px}
 section{scroll-margin-top:20px}
 #alertbar{display:none;background:rgba(192,57,43,.16);border:1px solid rgba(248,113,113,.45);color:#FFB4A8;border-radius:14px;
   padding:12px 16px;margin-bottom:14px;font-weight:700;animation:blink 1.2s infinite;align-items:center;gap:9px}
 @keyframes blink{50%{background:rgba(192,57,43,.32)}}
 .card{background:rgba(255,255,255,.045);border:1px solid rgba(255,255,255,.09);border-radius:16px;padding:16px;
   box-shadow:0 4px 24px rgba(0,0,0,.22)}
 .card h2{font-size:12px;margin:0 0 10px;color:#9DB4FF;text-transform:uppercase;letter-spacing:.8px;font-weight:700;
   display:flex;align-items:center;gap:9px}
 .card h2 .chip{width:27px;height:27px;border-radius:8px;background:rgba(124,154,255,.13);color:#AFC2FF;
   display:inline-flex;align-items:center;justify-content:center;flex:none}
 .card h2 .chip svg.ic{width:16px;height:16px}
 .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px}
 .row{margin-bottom:12px}
 .val{font-size:26px;font-weight:800;color:#fff}
 .unit{font-size:13px;color:#8E9AC0;font-weight:600}
 .stats{font-size:12px;color:#93A0C4;margin-top:8px;line-height:1.5}
 .camwrap{position:relative;border-radius:12px;overflow:hidden;background:#04060D}
 .camph{position:absolute;inset:0;display:flex;flex-direction:column;gap:9px;align-items:center;justify-content:center;color:#66739F;font-size:13px}
 .camph svg.ic{width:38px;height:38px}
 img.stream{position:relative;display:block;width:100%;aspect-ratio:4/3;object-fit:cover}
 canvas{width:100%;height:260px;border-radius:8px}
 .ok{color:#4ADE80}.warn{color:#FBBF24}.bad{color:#F87171}
 .pill{display:inline-block;padding:2px 10px;border-radius:999px;font-size:13px;font-weight:700;background:rgba(255,255,255,.08);color:#E9EDFA}
 a.btn{display:inline-flex;align-items:center;gap:8px;background:linear-gradient(135deg,#3550C8,#5B7CFF);color:#fff;padding:9px 16px;border-radius:10px;
   text-decoration:none;font-size:13px;font-weight:700;margin:10px 8px 0 0}
 a.btn svg.ic{width:16px;height:16px}
 a.btn:active{filter:brightness(1.15)}
 a.btn.ghost{background:transparent;color:#DCE4FF;border:1px solid rgba(255,255,255,.18)}
 .wqiwrap{display:flex;align-items:center;gap:16px;flex-wrap:wrap}
 .wqival{font-size:44px;font-weight:800;color:#fff;line-height:1}
 .pad{display:grid;grid-template-columns:repeat(3,64px);gap:8px;justify-content:center;margin:6px 0 4px}
 .pad button{height:52px;border-radius:12px;border:1px solid rgba(255,255,255,.12);background:rgba(255,255,255,.05);color:#E9EDFA;
   font-size:19px;font-weight:700;cursor:pointer;touch-action:none;-webkit-user-select:none;user-select:none;
   display:flex;align-items:center;justify-content:center}
 .pad button svg.ic{width:22px;height:22px}
 .pad button:active{background:#3550C8;color:#fff;border-color:#3550C8}
 .pad .stop{background:rgba(216,67,54,.14);color:#FF9D94;border-color:rgba(216,67,54,.4);font-size:12px}
 .pad .empty{border:none;background:none;cursor:default}
 .spdrow{display:flex;align-items:center;gap:10px;justify-content:center;font-size:13px;color:#93A0C4;margin-top:8px}
 input[type=range]{accent-color:#6D8DFF;width:180px}
 input[type=checkbox]{accent-color:#5468D4;width:17px;height:17px;vertical-align:-3px}
 .wform label{display:block;font-size:12px;color:#93A0C4;margin-bottom:10px;font-weight:600}
 .wform input{display:block;width:100%;max-width:420px;margin-top:5px;padding:10px 12px;border-radius:10px;
   border:1px solid rgba(255,255,255,.15);background:rgba(255,255,255,.05);color:#fff;font-size:14px}
 @media (max-width:1023px){
  .sidebar{transform:translateX(-105%)}
  .sidebar.open{transform:none;box-shadow:0 0 60px rgba(0,0,0,.5)}
  .overlay.show{display:block}
  .main{margin-left:0;padding:74px 14px 34px}
  .topbar{display:flex}
  section{scroll-margin-top:70px}
 }
</style></head><body>

<aside class="sidebar" id="sidebar">
 <div class="brand"><img class="logoimg" src="data:image/jpeg;base64,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" alt="Logo Axon Expanse">
  <div><div class="bt">FIR</div><div class="bs">Floating Inspector Robot</div></div></div>
 <nav class="nav">
  <a href="#ringkasan" class="active" onclick="navGo(this)"><svg class="ic" viewBox="0 0 24 24"><rect x="4" y="4" width="7" height="7" rx="1.5"/><rect x="13" y="4" width="7" height="7" rx="1.5"/><rect x="4" y="13" width="7" height="7" rx="1.5"/><rect x="13" y="13" width="7" height="7" rx="1.5"/></svg>Ringkasan</a>
  <a href="#kamera" onclick="navGo(this)"><svg class="ic" viewBox="0 0 24 24"><rect x="3" y="7" width="18" height="13" rx="2"/><circle cx="12" cy="13" r="3.5"/><path d="M8.5 7L10 4.5h4L15.5 7"/></svg>Kamera</a>
  <a href="#ai" onclick="navGo(this)"><svg class="ic" viewBox="0 0 24 24"><path d="M4 8V5a1 1 0 0 1 1-1h3M16 4h3a1 1 0 0 1 1 1v3M20 16v3a1 1 0 0 1-1 1h-3M8 20H5a1 1 0 0 1-1-1v-3"/><circle cx="12" cy="12" r="3"/></svg>Deteksi AI</a>
  <a href="#riwayat" onclick="navGo(this);loadPhotos()"><svg class="ic" viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="2"/><circle cx="9" cy="10" r="1.6"/><path d="M4 17l5-4 3.5 3L16 13l4 4"/></svg>Riwayat Foto</a>
  <a href="#sensor" onclick="navGo(this)"><svg class="ic" viewBox="0 0 24 24"><path d="M3 12h4l2.5-6 4 12 2.5-6h5"/></svg>Sensor</a>
  <a href="#motor" onclick="navGo(this)"><svg class="ic" viewBox="0 0 24 24"><rect x="3" y="7" width="18" height="11" rx="5"/><path d="M8 10.5v4M6 12.5h4"/><circle cx="15.5" cy="11.3" r=".4" fill="currentColor"/><circle cx="17.8" cy="13.8" r=".4" fill="currentColor"/></svg>Kendali Motor</a>
  <a href="/files"><svg class="ic" viewBox="0 0 24 24"><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></svg>Berkas SD</a>
  <a href="#wifi" onclick="navGo(this)"><svg class="ic" viewBox="0 0 24 24"><path d="M4 10.5a12.5 12.5 0 0 1 16 0M6.8 13.9a8.2 8.2 0 0 1 10.4 0M9.6 17.1a4.2 4.2 0 0 1 4.8 0"/><circle cx="12" cy="19.7" r="1.1" fill="currentColor" stroke="none"/></svg>WiFi Robot</a>
  <a href="/live"><svg class="ic" viewBox="0 0 24 24"><rect x="3" y="6" width="13" height="12" rx="2"/><path d="M16 10.5l5-3v9l-5-3"/></svg>Video Live</a>
  <div class="nlabel">Lainnya</div>
  <a href="#tren" onclick="navGo(this)"><svg class="ic" viewBox="0 0 24 24"><path d="M4 4v15a1 1 0 0 0 1 1h15"/><path d="M8 15l3.5-4 3 2.5L19 8"/></svg>Tren &amp; Log</a>
  <a href="/download"><svg class="ic" viewBox="0 0 24 24"><path d="M12 4v10.5M7.5 11L12 15.5 16.5 11M4.5 20h15"/></svg>Unduh CSV Log</a>
  <a href="/download-ai"><svg class="ic" viewBox="0 0 24 24"><path d="M12 4v10.5M7.5 11L12 15.5 16.5 11M4.5 20h15"/></svg>Riwayat AI (CSV)</a>
 </nav>
 <div class="logwrap"><div class="logtitle">Log Robot</div><div class="logbox" id="logbox"><div class="logline">Memuat log...</div></div></div>
 <div class="side-foot"><span class="dot"></span><b>Terhubung ke robot</b><br>AP: FIR-Robot<br>Alamat: 192.168.4.1<br><span id="footbat">Baterai: -</span><br><span id="footchg"></span></div>
</aside>
<div class="overlay" id="overlay" onclick="closeSide()"></div>

<div class="topbar">
 <img class="logoimg" src="data:image/jpeg;base64,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" alt="Logo Axon Expanse"><div class="tt">FIR</div>
 <button onclick="openSide()" aria-label="Buka menu"><svg class="ic" viewBox="0 0 24 24" style="width:24px;height:24px"><path d="M4 7h16M4 12h16M4 17h16"/></svg></button>
</div>

<div class="main"><div class="wrap">
<header><h1>Monitoring Lingkungan Perairan</h1></header>
<div class="sub">Floating Inspector Robot &middot; Smart Home &amp; Environment &middot; 3 parameter + WQI + peringatan dini + kemiringan &middot; log tiap 60 detik</div>
<div id="alertbar"><svg class="ic" viewBox="0 0 24 24"><path d="M12 4L2.8 20h18.4z"/><path d="M12 10v4.5"/><circle cx="12" cy="17.2" r=".4" fill="currentColor"/></svg><span>PERINGATAN DINI: <span id="alertmsg"></span></span></div>

<section id="ringkasan">
<div class="card row"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M4.5 19a9 9 0 1 1 15 0"/><path d="M12 15l4.2-4.2"/><circle cx="12" cy="15" r="1.2" fill="currentColor" stroke="none"/></svg></span>Indeks Kualitas Air (WQI)</h2>
 <div class="wqiwrap"><div class="wqival"><span id="wqi">-</span><span class="unit"> / 100</span></div>
 <span class="pill" id="wqicat"></span></div>
 <div class="stats">Rata-rata sub-indeks suhu, kekeruhan &amp; TDS (bobot setara). &ge;80 Baik &middot; 50&ndash;79 Sedang &middot; &lt;50 Buruk.</div></div>
</section>

<section id="kamera">
<div class="card row"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><rect x="3" y="7" width="18" height="13" rx="2"/><circle cx="12" cy="13" r="3.5"/><path d="M8.5 7L10 4.5h4L15.5 7"/></svg></span>Kamera Robot</h2>
 <div class="camwrap"><div class="camph"><svg class="ic" viewBox="0 0 24 24"><rect x="3" y="7" width="18" height="13" rx="2"/><circle cx="12" cy="13" r="3.5"/><path d="M8.5 7L10 4.5h4L15.5 7"/></svg><span>Umpan kamera &mdash; foto baru tiap 1,2 detik</span></div>
 <img class="stream" id="camimg" src="/capture" alt="video robot" onerror="this.style.opacity=0" onload="this.style.opacity=1"></div>
 <div class="stats">Foto berkala dari kamera robot (SVGA 800&times;600). Dipakai juga sebagai bahan analisis AI.</div></div>
</section>

<section id="ai">
<div class="card row"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M4 8V5a1 1 0 0 1 1-1h3M16 4h3a1 1 0 0 1 1 1v3M20 16v3a1 1 0 0 1-1 1h-3M8 20H5a1 1 0 0 1-1-1v-3"/><circle cx="12" cy="12" r="3"/></svg></span>Deteksi Kandidat Korosi (AI)</h2>
 <div class="val" id="aires" style="font-size:20px">Belum ada analisis</div>
 <div class="stats" id="aidet">Analisis foto kamera saat ini memakai model AI di dalam robot (tanpa internet). Hasil &ldquo;kandidat korosi&rdquo; selalu wajib diverifikasi operator.</div>
 <div class="stats"><b>Alur bertahap (lebih aman):</b> foto diambil &amp; disimpan dulu, analisis berjalan sesudahnya pada foto yang tersimpan itu. Bila robot restart di tengah analisis, fotonya tidak ikut hilang &mdash; tinggal dianalisis ulang dari menu Riwayat Foto.</div>
 <img id="aiprev" class="stream" style="display:none" alt="foto tersimpan">
 <div class="stats" id="aistep">Belum ada foto sesi ini. Tekan Langkah 1 untuk mengambil foto.</div>
 <a class="btn" href="#" onclick="capSave();return false;"><svg class="ic" viewBox="0 0 24 24"><rect x="3" y="7" width="18" height="13" rx="2"/><circle cx="12" cy="13" r="3.5"/><path d="M8.5 7L10 4.5h4L15.5 7"/></svg>1. Ambil Foto &amp; Simpan</a>
 <a class="btn" href="#" onclick="analyzeLast();return false;"><svg class="ic" viewBox="0 0 24 24"><circle cx="11" cy="11" r="7"/><path d="M20.5 20.5L16 16"/></svg>2. Analisis Foto Terakhir</a>
 <a class="btn ghost" href="#" onclick="analyze();return false;">Analisis Langsung (tanpa simpan)</a>
 <a class="btn ghost" href="/download-ai"><svg class="ic" viewBox="0 0 24 24"><path d="M12 4v10.5M7.5 11L12 15.5 16.5 11M4.5 20h15"/></svg>Unduh Riwayat AI (aidet.csv)</a>
 <a class="btn ghost" href="/files"><svg class="ic" viewBox="0 0 24 24"><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></svg>Kelola Berkas SD</a>
 <div class="stats"><span id="aicount">0</span> analisis tersimpan di robot &mdash; tidak hilang saat robot mati.</div>
 <div class="stats" style="margin-top:10px"><label><input type="checkbox" id="aiauto" onchange="setAuto(this.checked)"> <b>Mode Otomatis (Computer Vision)</b></label><br>
 <span id="aistat">Memuat status...</span></div>
</div>
</section>

<section id="riwayat">
<div class="card row"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="2"/><circle cx="9" cy="10" r="1.6"/><path d="M4 17l5-4 3.5 3L16 13l4 4"/></svg></span>Riwayat Foto</h2>
 <a class="btn ghost" href="#" id="riwayattoggle" onclick="toggleRiwayat();return false;">Sembunyikan</a>
 <div class="stats">Semua foto yang diambil lewat menu Deteksi AI, tersimpan di SD card robot lengkap dengan hasil analisisnya. Foto tetap ada walau robot restart &mdash; foto yang belum dianalisis bisa dianalisis dari sini.</div>
 <div id="riwayatbody">
 <div class="stats" id="photobar">Memuat ringkasan foto...</div>
 <div id="photolist"><div class="stats">Memuat...</div></div>
 <a class="btn ghost" href="#" onclick="loadPhotos();return false;">Muat Ulang</a>
 </div>
</div>
</section>

<section id="sensor">
<div class="grid row">
 <div class="card"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M10 4a2 2 0 0 1 4 0v9.2a4.6 4.6 0 1 1-4 0z"/><circle cx="12" cy="17.6" r="1.4" fill="currentColor" stroke="none"/></svg></span>Suhu Air</h2><div class="val"><span id="t">-</span> <span class="unit">&deg;C</span></div></div>
 <div class="card"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M2 8c3.3-2.6 6.7-2.6 10 0s6.7 2.6 10 0"/><path d="M2 15c3.3-2.6 6.7-2.6 10 0s6.7 2.6 10 0"/></svg></span>Kekeruhan</h2><div class="val" id="tblvl">-</div></div>
 <div class="card"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M12 3.5s6 6.2 6 9.9a6 6 0 1 1-12 0c0-3.7 6-9.9 6-9.9z"/></svg></span>TDS</h2><div class="val"><span id="tds">-</span> <span class="unit">ppm</span></div></div>
 <div class="card"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M12 3l7 2.5V11c0 4.6-3 7.7-7 9.5C8 18.7 5 15.6 5 11V5.5z"/><path d="M9.3 11.6l2 2 3.6-4.2"/></svg></span>Kebocoran</h2><div class="val" id="lk">-</div><div class="stats" id="lkraw">nilai mentah sensor: -</div></div>
 <div class="card"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M7 14.5a4.3 4.3 0 1 1 .7-8.5A5.4 5.4 0 0 1 18.3 7.6 3.7 3.7 0 0 1 17.4 14.5z"/><path d="M8.4 17.5l-1 2.2M12.4 17.5l-1 2.2M16.4 17.5l-1 2.2"/></svg></span>Sensor Hujan</h2><div class="val" id="rn">-</div><div class="stats">keluaran digital DO (GPIO42); ambang diatur trimpot modul</div></div>
 <div class="card"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M4 10.5a12.5 12.5 0 0 1 16 0M6.8 13.9a8.2 8.2 0 0 1 10.4 0M9.6 17.1a4.2 4.2 0 0 1 4.8 0"/><circle cx="12" cy="19.7" r="1.1" fill="currentColor" stroke="none"/></svg></span>WiFi / Log</h2><div class="val"><span id="rssi">-</span> <span class="unit">dBm</span></div><div class="unit"><span id="lg">-</span> data tercatat</div></div>
</div>
</section>

<section id="motor">
<div class="card row"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><rect x="3" y="7" width="18" height="11" rx="5"/><path d="M8 10.5v4M6 12.5h4"/><circle cx="15.5" cy="11.3" r=".4" fill="currentColor"/><circle cx="17.8" cy="13.8" r=".4" fill="currentColor"/></svg></span>Kendali Motor</h2>
 <div class="pad">
  <span class="empty"></span><button onpointerdown="holdStart('F')" onpointerup="holdEnd()" onpointerleave="holdEnd()" onpointercancel="holdEnd()" oncontextmenu="return false" title="Maju (tekan-tahan)"><svg class="ic" viewBox="0 0 24 24"><path d="M12 4l7 13H5z" fill="currentColor" stroke="none"/></svg></button><span class="empty"></span>
  <button onpointerdown="holdStart('L')" onpointerup="holdEnd()" onpointerleave="holdEnd()" onpointercancel="holdEnd()" oncontextmenu="return false" title="Putar kiri (tekan-tahan)"><svg class="ic" viewBox="0 0 24 24"><path d="M4 12l13-7v14z" fill="currentColor" stroke="none"/></svg></button><button class="stop" onpointerdown="motor('S')">STOP</button><button onpointerdown="holdStart('R')" onpointerup="holdEnd()" onpointerleave="holdEnd()" onpointercancel="holdEnd()" oncontextmenu="return false" title="Putar kanan (tekan-tahan)"><svg class="ic" viewBox="0 0 24 24"><path d="M20 12L7 5v14z" fill="currentColor" stroke="none"/></svg></button>
  <span class="empty"></span><button onpointerdown="holdStart('B')" onpointerup="holdEnd()" onpointerleave="holdEnd()" onpointercancel="holdEnd()" oncontextmenu="return false" title="Mundur (tekan-tahan)"><svg class="ic" viewBox="0 0 24 24"><path d="M12 20L5 7h14z" fill="currentColor" stroke="none"/></svg></button><span class="empty"></span>
 </div>
 <div class="spdrow"><label for="spd">Kecepatan</label><input type="range" id="spd" min="30" max="100" value="70" oninput="document.getElementById('spdv').textContent=this.value+'%'"><span id="spdv">70%</span></div>
 <div class="stats" style="text-align:center">Tekan dan tahan tombol untuk bergerak &mdash; lepas tombol = berhenti. Pengaman: motor berhenti otomatis 1,5 detik bila perintah berhenti tidak sampai.</div>
 <div class="stats" id="mstat" style="text-align:center">Status motor: -</div>
</div>
</section>

<section id="wifi">
<div class="card row"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M4 10.5a12.5 12.5 0 0 1 16 0M6.8 13.9a8.2 8.2 0 0 1 10.4 0M9.6 17.1a4.2 4.2 0 0 1 4.8 0"/><circle cx="12" cy="19.7" r="1.1" fill="currentColor" stroke="none"/></svg></span>WiFi Robot</h2>
 <div class="stats" id="wfstat" style="margin-top:0">Memuat status WiFi...</div>
 <div class="wform" style="margin-top:10px">
  <label>Nama hotspot tujuan (SSID)<input type="text" id="wfssid" maxlength="32" placeholder="Nama hotspot"></label>
  <label>Kata sandi<input type="password" id="wfpass" maxlength="63" placeholder="Kata sandi hotspot"></label>
 </div>
 <a class="btn" href="#" onclick="wifiSet();return false;">Simpan &amp; Sambungkan</a>
 <a class="btn ghost" href="#" onclick="wifiReset();return false;">Kembalikan ke Bawaan</a>
 <div class="stats" id="wfmsg">Robot menyimpan kredensial lalu restart dan menyambung ke hotspot baru. Bila hotspot itu tidak ditemukan, AP &ldquo;FIR-Robot&rdquo; tetap menyala untuk perbaikan dari sini.</div>
</div>
</section>

<section id="tren">
<div class="card row"><h2><span class="chip"><svg class="ic" viewBox="0 0 24 24"><path d="M4 4v15a1 1 0 0 0 1 1h15"/><path d="M8 15l3.5-4 3 2.5L19 8"/></svg></span>Tren Suhu, Kekeruhan &amp; TDS (Sesi Ini)</h2><canvas id="chart" width="900" height="260"></canvas>
<div class="stats" id="stats">Memuat histori...</div>
<a class="btn" href="/download"><svg class="ic" viewBox="0 0 24 24"><path d="M12 4v10.5M7.5 11L12 15.5 16.5 11M4.5 20h15"/></svg>Unduh CSV (firlog.csv)</a></div>
</section>
</div></div>

<script>
function openSide(){document.getElementById('sidebar').classList.add('open');document.getElementById('overlay').classList.add('show');}
function closeSide(){document.getElementById('sidebar').classList.remove('open');document.getElementById('overlay').classList.remove('show');}
function navGo(a){var n=document.querySelectorAll('.nav a');for(var i=0;i<n.length;i++)n[i].classList.remove('active');a.classList.add('active');closeSide();}
function fmtT(s){var m=Math.floor(s/60),ss=s%60;return m+":"+("0"+ss).slice(-2);}
function motor(c){var s=document.getElementById('spd').value;fetch('/motor?cmd='+c+'&speed='+s).catch(function(){});}
var holdTimer=null;
function holdStart(c){motor(c);clearInterval(holdTimer);holdTimer=setInterval(function(){motor(c);},500);}
function holdEnd(){if(holdTimer){clearInterval(holdTimer);holdTimer=null;}motor('S');}
function showAi(d){
 var r=document.getElementById('aires');
 if(d.ai_kandidat){r.textContent='KANDIDAT KOROSI — perlu verifikasi operator';r.className='val bad';}
 else{r.textContent='Kelas teratas: '+d.ai_label+' ('+d.ai_score.toFixed(2)+')';r.className='val';}
 document.getElementById('aidet').textContent='Skor — karat '+d.ai_karat.toFixed(2)+' | cat_coklat '+d.ai_cat.toFixed(2)+
  ' | logam_bersih '+d.ai_logam.toFixed(2)+' | lumut_kerak '+d.ai_lumut.toFixed(2)+' | inferensi '+d.ai_ms+' ms';
 document.getElementById('aicount').textContent=d.ai_count;
}
function aiStat(d){
 if(d.ai_auto===undefined)return;
 document.getElementById('aiauto').checked=d.ai_auto;
 document.getElementById('aistat').textContent='Mode otomatis: '+(d.ai_auto?'AKTIF — analisis otomatis tiap 5 detik':'mati')+
  ' | SD card: '+(d.sd_ready?'terpasang':'belum terpasang')+' | foto kandidat tersimpan: '+d.ai_photos;
}
async function setAuto(on){try{const d=await (await fetch('/ai-auto?on='+(on?1:0))).json(); aiStat(d);}catch(e){}}
async function analyze(){
 var r=document.getElementById('aires');r.textContent='Menganalisis...';r.className='val';
 try{const d=await (await fetch('/analyze')).json();
  if(d.ok){showAi(d);}else{r.textContent='Analisis gagal — coba lagi.';}
 }catch(e){r.textContent='Analisis gagal — coba lagi.';}
}
var lastPhoto='';
async function capSave(){
 var st=document.getElementById('aistep');st.textContent='Mengambil foto...';
 try{const d=await (await fetch('/capture-save')).json();
  if(d.ok&&d.file){lastPhoto=d.file;var im=document.getElementById('aiprev');im.src='/photo-get?f='+d.file;im.style.display='block';
   st.textContent='Foto tersimpan: '+d.file+' — lanjut Langkah 2 untuk analisis. Foto ini juga muncul di menu Riwayat Foto.';
   loadPhotos();
  }else{st.textContent='Gagal menyimpan foto (SD card belum siap / AI sedang sibuk).';}
 }catch(e){st.textContent='Gagal menyimpan foto.';}
}
async function analyzeLast(){
 if(!lastPhoto){document.getElementById('aistep').textContent='Ambil foto dulu (Langkah 1).';return;}
 analyzePhoto(lastPhoto);
}
async function analyzePhoto(f){
 var r=document.getElementById('aires');r.textContent='Menganalisis '+f+'...';r.className='val';
 try{const d=await (await fetch('/analyze-photo?f='+f)).json();
  if(d.ok){showAi(d);var nf=(d.newfile&&d.newfile!==f)?d.newfile:null;if(nf&&lastPhoto===f)lastPhoto=nf;document.getElementById('aistep').textContent='Analisis '+f+' selesai'+(nf?(' \u2014 nama foto berubah menjadi '+nf):'')+' \u2014 hasilnya tercatat di Riwayat Foto.';loadPhotos();}
  else{r.textContent='Analisis gagal — coba lagi.';}
 }catch(e){r.textContent='Analisis gagal — coba lagi.';}
}
function fmtDT(ep){
 if(!ep||ep<1700000000)return 'waktu belum tersinkron';
 var d=new Date(ep*1000);
 return d.toLocaleDateString('id-ID',{weekday:'long',day:'numeric',month:'long',year:'numeric'})+' \u2022 '+d.toLocaleTimeString('id-ID',{hour:'2-digit',minute:'2-digit',second:'2-digit'});
}
function photoCard(p){
 var st,bar='';
 if(p.done){var pct=Math.round(Number(p.score)*100);
  st='Hasil: <b>'+p.label+'</b> ('+Number(p.score).toFixed(2)+')'+(p.label==='karat'?' \u2014 kandidat korosi, perlu verifikasi operator':'');
  bar='<div class="confbar"><div class="conffill" style="width:'+pct+'%"></div></div><div class="stats">Keyakinan model: '+pct+'%</div>';
  st+='<br>Dianalisis: '+fmtDT(p.tsa);
 }else{st='Belum dianalisis';}
 return '<div class="photocard"><img src="/photo-get?f='+p.f+'" loading="lazy"><div class="stats"><b>'+p.f+'</b><br>Diambil: '+fmtDT(p.ts)+'<br>'+st+'</div>'+bar+
 '<a class="btn" href="#" onclick="analyzePhoto(\''+p.f+'\');return false;">Analisis</a> '+
 '<a class="btn ghost" href="#" onclick="delPhoto(\''+p.f+'\');return false;">Hapus</a></div>';
}
async function loadPhotos(){
 var el=document.getElementById('photolist');if(!el)return;
 var bar=document.getElementById('photobar');
 try{const d=await (await fetch('/photos')).json();
  var ps=d.photos||[];
  if(bar){var un0=ps.filter(function(p){return !p.done}).length;
   bar.innerHTML='Total <b>'+ps.length+'</b> foto \u2022 <b>'+un0+'</b> belum dianalisis \u2022 <b>'+(ps.length-un0)+'</b> sudah dianalisis<br>Jam robot: '+(d.synced?fmtDT(d.now):'belum tersinkron \u2014 halaman ini menyinkronkan jam robot dari HP');}
  if(!ps.length){el.innerHTML='<div class="stats">Belum ada foto tersimpan. Ambil foto dari menu Deteksi AI (Langkah 1).</div>';return;}
  var un=ps.filter(function(p){return !p.done}),dn=ps.filter(function(p){return p.done});
  var h='<div class="pgroup">Belum Dianalisa ('+un.length+')</div>'+(un.length?un.map(photoCard).join(''):'<div class="stats">Tidak ada foto yang menunggu analisis.</div>');
  h+='<div class="pgroup">Sudah Dianalisa ('+dn.length+')</div>'+(dn.length?dn.map(photoCard).join(''):'<div class="stats">Belum ada foto yang sudah dianalisis.</div>');
  el.innerHTML=h;
 }catch(e){el.innerHTML='<div class="stats">Gagal memuat riwayat foto.</div>';}
}
async function delPhoto(f){
 if(!confirm('Hapus foto '+f+'?'))return;
 try{await fetch('/photo-del?f='+f);}catch(e){}
 loadPhotos();
}
async function live(){try{
 const d=await (await fetch('/telemetry')).json();
 document.getElementById('t').textContent=d.temperature_c.toFixed(1);
 document.getElementById('tds').textContent=d.tds_ppm.toFixed(0);
 var lv=document.getElementById('tblvl');
 if(d.turbidity_override){lv.textContent='Klasifikasi: '+d.turbidity_level;
 lv.className='val '+(d.turbidity_level==='keruh'?'bad':(d.turbidity_level==='sedang'?'warn':'ok'));}
 else if(d.turbidity_fault){lv.textContent='Klasifikasi: tidak merespons — cek kabel A / suplai 5V / probe';lv.className='val bad';}
 else{lv.textContent='Klasifikasi: '+d.turbidity_level;
 lv.className='val '+(d.turbidity_level==='keruh'?'bad':(d.turbidity_level==='sedang'?'warn':'ok'));}
 var lk=document.getElementById('lk');
 lk.textContent=d.leak?'BOCOR TERDETEKSI':'Aman';
 lk.className='val '+(d.leak?'bad':'ok');
 document.getElementById('lkraw').textContent='nilai mentah sensor: '+d.leak_raw;
 var rn=document.getElementById('rn');
 rn.textContent=d.rain?'HUJAN':'Tidak hujan';
 rn.className='val '+(d.rain?'warn':'ok');
 // v7.10.0: blok JS roll/pitch dibuang bersama kartu Kemiringan (gyro dihapus).
 var wq=document.getElementById('wqi');
 wq.textContent=d.wqi<0?'-':d.wqi;
 var wc=document.getElementById('wqicat');
 wc.textContent=d.wqi_cat==='-'?'':d.wqi_cat;
 wc.className='pill '+(d.wqi_cat==='BAIK'?'ok':(d.wqi_cat==='SEDANG'?'warn':(d.wqi_cat==='BURUK'?'bad':'')));
 document.getElementById('rssi').textContent=d.rssi_dbm;
 var ms=document.getElementById('mstat');
 if(ms&&d.motor_tgt_l!==undefined){ms.textContent=d.motor_hold?'Status motor: DITAHAN — sesi SD aktif':('Status motor: target L '+d.motor_tgt_l+' / R '+d.motor_tgt_r+' — terapan L '+d.motor_cur_l+' / R '+d.motor_cur_r);}
 document.getElementById('lg').textContent=d.log_count;
 var ab=document.getElementById('alertbar');
 if(d.alert){ab.style.display='flex';document.getElementById('alertmsg').textContent=d.alert_msg;}
 else{ab.style.display='none';}
 if(d.ai_valid)showAi(d);
 aiStat(d);
 var fb=document.getElementById('footbat');
 if(fb)fb.textContent=(d.battery_pct>=0)?('Baterai: '+d.battery_pct+'% ('+d.battery_mv+' mV)'):'Baterai: belum terukur';
 var fc=document.getElementById('footchg');
 if(fc&&d.chg!==undefined)fc.textContent=d.chg?'Status cas: SEDANG DICAS':'Status cas: tidak dicas';
}catch(e){}}
async function chart(){try{
 const d=await (await fetch('/history')).json();
 var recs=d.records||[];
 var cv=document.getElementById('chart'),ctx=cv.getContext('2d');
 var dpr=window.devicePixelRatio||1,dispW=cv.clientWidth||900,dispH=260;
 if(cv.width!==Math.round(dispW*dpr)){cv.width=Math.round(dispW*dpr);cv.height=Math.round(dispH*dpr);}
 ctx.setTransform(dpr,0,0,dpr,0,0);
 var W=dispW,H=dispH;ctx.clearRect(0,0,W,H);
 if(recs.length<2){document.getElementById('stats').textContent='Belum cukup data (log tiap 60 detik).';return;}
 var pad=44;
 function rng(key){var mn=1e9,mx=-1e9;recs.forEach(function(r){var v=r[key];if(v<mn)mn=v;if(v>mx)mx=v;});if(mx-mn<0.01){mx+=0.01;mn-=0.01;}return[mn,mx];}
 var rt=rng('t'),rb=rng('tb'),rd=rng('tds');
 var t0=recs[0].ts,t1=recs[recs.length-1].ts;if(t1===t0)t1=t0+1;
 function X(ts){return pad+(ts-t0)/(t1-t0)*(W-2*pad);}
 function Y(v,mn,mx){return H-pad-(v-mn)/(mx-mn)*(H-2*pad);}
 ctx.strokeStyle='rgba(255,255,255,.18)';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(pad,10);ctx.lineTo(pad,H-pad);ctx.lineTo(W-10,H-pad);ctx.stroke();
 ctx.fillStyle='#93A0C4';ctx.font='11px sans-serif';
 ctx.fillText(rt[1].toFixed(1)+' C',6,16);ctx.fillText(rt[0].toFixed(1)+' C',6,H-pad);
 ctx.fillText(rb[1].toFixed(2)+' V',W-52,16);ctx.fillText(rb[0].toFixed(2)+' V',W-52,H-pad);
 ctx.fillText(fmtT(t0),pad,H-8);ctx.fillText(fmtT(t1),W-70,H-8);
 function line(key,mm,color){ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();
  recs.forEach(function(r,i){var x=X(r.ts),y=Y(r[key],mm[0],mm[1]);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});ctx.stroke();}
 line('t',rt,'#FF9E64');line('tb',rb,'#57C7FF');line('tds',rd,'#B79CFF');
 var leg=[['— suhu (°C)','#FF9E64'],['— kekeruhan (V)','#57C7FF'],['— TDS (ppm)','#B79CFF']];
 var lx=pad+8,ly=18;
 leg.forEach(function(it){ctx.fillStyle=it[1];var wdt=ctx.measureText(it[0]).width;
  if(lx+wdt>W-58&&lx>pad+8){lx=pad+8;ly+=15;}
  ctx.fillText(it[0],lx,ly);lx+=wdt+16;});
 var st={t:[1e9,-1e9,0],tb:[1e9,-1e9,0],tds:[1e9,-1e9,0]};
 recs.forEach(function(r){['t','tb','tds'].forEach(function(k){var a=st[k],v=r[k];if(v<a[0])a[0]=v;if(v>a[1])a[1]=v;a[2]+=v;});});
 var n=recs.length;
 document.getElementById('stats').textContent=
  'Suhu: min '+st.t[0].toFixed(1)+' / maks '+st.t[1].toFixed(1)+' / rata2 '+(st.t[2]/n).toFixed(1)+' °C   |   '+
  'Kekeruhan: min '+st.tb[0].toFixed(2)+' / maks '+st.tb[1].toFixed(2)+' / rata2 '+(st.tb[2]/n).toFixed(2)+' V   |   '+
  'TDS: min '+st.tds[0].toFixed(0)+' / maks '+st.tds[1].toFixed(0)+' / rata2 '+(st.tds[2]/n).toFixed(0)+' ppm   ('+n+' data)';
}catch(e){document.getElementById('stats').textContent='Gagal memuat histori.';}}
async function loadWifi(){try{const d=await (await fetch('/wifi')).json();
 document.getElementById('wfstat').textContent='Mode: '+d.mode+' | Target: '+d.target_ssid+' | STA: '+(d.sta_connected?('terhubung ke '+d.sta_ssid+' - IP '+d.sta_ip+' - RSSI '+d.rssi_dbm+' dBm'):'tidak terhubung')+' | AP: '+d.ap_ssid+' - '+d.ap_ip;
 var f=document.getElementById('wfssid');if(document.activeElement!==f)f.value=d.target_ssid||'';
}catch(e){document.getElementById('wfstat').textContent='Gagal memuat status WiFi.';}}
async function wifiSet(){var ssid=document.getElementById('wfssid').value.trim(),pass=document.getElementById('wfpass').value;
 var m=document.getElementById('wfmsg');if(!ssid){m.textContent='SSID tidak boleh kosong.';return;}
 m.textContent='Menyimpan... robot akan restart.';
 try{var body=new URLSearchParams();body.set('ssid',ssid);body.set('pass',pass);
  const d=await (await fetch('/wifi-set',{method:'POST',body:body})).json();
  m.textContent=d.ok?('Tersimpan - robot restart dan menyambung ke "'+ssid+'". Sambungkan perangkat ini ke hotspot itu, atau tetap lewat AP FIR-Robot.'):('Gagal: '+(d.err||'tidak diketahui'));
 }catch(e){m.textContent='Sambungan putus karena robot restart - itu normal. Tunggu beberapa detik lalu muat ulang halaman.';}}
async function wifiReset(){var m=document.getElementById('wfmsg');m.textContent='Mengembalikan ke bawaan... robot akan restart.';
 try{var body=new URLSearchParams();body.set('reset','1');await fetch('/wifi-set',{method:'POST',body:body});}catch(e){}
 m.textContent='Kredensial dikembalikan ke bawaan - robot restart.';}
setInterval(loadWifi,15000);loadWifi();
setInterval(live,2000);live();
setInterval(chart,30000);chart();
async function pushTime(){try{await fetch('/time-set?e='+Math.floor(Date.now()/1000));}catch(e){}}
async function loadLog(){
 var el=document.getElementById('logbox');if(!el)return;
 try{const d=await (await fetch('/log')).json();
  if(!d.log||!d.log.length){el.innerHTML='<div class="logline">Belum ada log.</div>';return;}
  el.innerHTML=d.log.map(function(x){
   var s2=x.t?fmtDT(x.t):('boot +'+x.up+' dtk');
   return '<div class="logline"><span class="logtime">'+s2+'</span>'+x.m+'</div>';
  }).join('');
 }catch(e){}
}
function applyRiwayatHidden(h){
 var b=document.getElementById('riwayatbody');if(!b)return;
 b.style.display=h?'none':'';
 var t=document.getElementById('riwayattoggle');if(t)t.textContent=h?'Tampilkan':'Sembunyikan';
}
function toggleRiwayat(){
 var h=true;try{h=localStorage.getItem('fir-riwayat-hidden')!=='1';localStorage.setItem('fir-riwayat-hidden',h?'1':'0');}catch(e){}
 applyRiwayatHidden(h);
}
try{applyRiwayatHidden(localStorage.getItem('fir-riwayat-hidden')==='1');}catch(e){}
pushTime();setInterval(pushTime,60000);
loadLog();setInterval(loadLog,5000);
loadPhotos();
var camBusy=false;
function camTick(){if(camBusy)return;camBusy=true;var im=new Image();
 im.onload=function(){var c=document.getElementById('camimg');c.src=im.src;c.style.opacity=1;camBusy=false;};
 im.onerror=function(){camBusy=false;};
 im.src='/capture?t='+Date.now();}
setInterval(camTick,1200);camTick();
</script></body></html>
)rawliteral";

void handleRoot() { server.send_P(200, "text/html", DASHBOARD_HTML); }

// ============================ SENSOR ==========================================
void readSensors() {
  // v7.3.5: SAMPLING TURBIDITY BARU - ledakan 24 bacaan
  // analogReadMilliVolts (jalur kalibrasi eFuse) lalu MEDIAN.
  // ADC ESP32-S3 berisik: satu bacaan analogRead tunggal (cara
  // lama) membuat tegangan simpul & persen kekeruhan
  // loncat-loncat antar siklus di air yang sama. Median membuang
  // lonjakan sesaat; milivolt terkalibrasi menggantikan konversi
  // kasar raw x 3,3/4095 yang mengabaikan ketidaklinearan ADC.
  // Jangkar TURB_PROBE_* tidak berubah - yang berubah hanya cara
  // tegangan simpul diperoleh.
  const int TURB_BURST_N = 24;
  int mvBuf[TURB_BURST_N];
  for (int i = 0; i < TURB_BURST_N; i++) mvBuf[i] = (int)analogReadMilliVolts(TURBIDITY_PIN);
  for (int i = 1; i < TURB_BURST_N; i++) {          // insertion sort menaik
    int k = mvBuf[i], j = i - 1;
    while (j >= 0 && mvBuf[j] > k) { mvBuf[j + 1] = mvBuf[j]; j--; }
    mvBuf[j + 1] = k;
  }
  int mvMed = (mvBuf[TURB_BURST_N / 2 - 1] + mvBuf[TURB_BURST_N / 2]) / 2;
  g_turbMv = mvMed;                    // v7.3.5: median = nilai penggerak (dulu g_turbMv hanya saksi [T])
  g_turbRaw = (mvMed * 4095) / 3300;   // raw-setara dari median yang sama, untuk baris [T]
  g_turbDig = digitalRead(TURBIDITY_PIN);          // buffer digital pin (tanpa pinMode tambahan)
  float vPin = mvMed / 1000.0f;
  // v7.2.3: MODE PROBE LANGSUNG. g_turbV = tegangan SIMPUL probe apa
  // adanya (kabel kuning -> 20k -> simpul GPIO1; simpul ditarik ke
  // 3V3 lewat 10k). Rekonstruksi divider mode A (x TURB_VDIV_RATIO)
  // TIDAK berlaku lagi di mode ini. Arah dibalik dari mode A:
  // simpul RENDAH = fototransistor menghantar = cahaya tembus =
  // jernih; simpul TINGGI (dekat 3,3 V) = gelap = keruh.
  // v7.3.5: perataan antar-siklus EMA alpha 0,5 di atas median -
  // sisa jitter antar siklus di air yang sama teredam, perubahan
  // nyata (celup ke air lain) tetap sampai dalam 1-2 siklus.
  // Siklus pertama memakai bacaan apa adanya.
  static bool turbEmaInit = false;
  if (!turbEmaInit) { g_turbV = vPin; turbEmaInit = true; }
  else g_turbV = g_turbV + 0.5f * (vPin - g_turbV);
  // Fraksi kejernihan 0..1 dari dua jangkar pra-kalibrasi.
  float turbFrac = (TURB_PROBE_DARK_V - g_turbV) / (TURB_PROBE_DARK_V - TURB_PROBE_CLEAR_V);
  if (turbFrac < 0.0f) turbFrac = 0.0f;
  if (turbFrac > 1.0f) turbFrac = 1.0f;
  // Persen kekeruhan: 0% = jernih (jangkar CLEAR), 100% = gelap total.
  g_turbPct = (int)lroundf((1.0f - turbFrac) * 100.0f);
  // Deteksi fault mode probe: simpul menempel di rel 3V3 berarti
  // fototransistor TIDAK PERNAH menarik simpul turun - jalur probe
  // mati/putus (di udara terbuka pun simpul wajib jatuh ke sekitar
  // TURB_PROBE_CLEAR_V). Penghitung g_turbLowCount dipakai ulang
  // sebagai penghitung bacaan jenuh-gelap berturut-turut.
  // v7.3.5: syarat kedua (mvMed >= 3100) menutup kejenuhan jalur
  // mV - simpul yang menempel di rel 3,3 V terbaca mentok di
  // sekitar 3,1 V pada milivolt terkalibrasi, jadi ambang
  // 3,25 V tidak tercapai dari jalur itu saja. 3100 mV = 94%
  // rel, jauh di atas keadaan diam gelap 2,2 V (keputusan
  // v7.2.5: tetap "keruh", bukan fault).
  // v7.9.0: deteksi BEKU untuk divider terpasang user (penarik ke
  // 3V3 + 20k ke GND). Saluran mati di rangkaian ini tidak selalu
  // menempel di rel: bila kaki 20k ke GND menghantar, fototransistor
  // yang tidak pernah menghantar membiarkan simpul duduk diam di
  // tegangan diam divider (2,2 V) dan terbaca "keruh" abadi. Median
  // yang sama persis (±1 mV) selama TURB_FREEZE_N siklus berurutan
  // tidak mungkin berasal dari fototransistor hidup - noise termal
  // dan kedip cahaya selalu menggerakkan median - jadi keadaan itu
  // dinyatakan mati juga.
  static int turbLastMvMed = -1;
  static uint16_t turbFreezeCount = 0;
  if (turbLastMvMed >= 0 && abs(mvMed - turbLastMvMed) <= 1) {
    if (turbFreezeCount < 60000) turbFreezeCount++;
  } else {
    turbFreezeCount = 0;
  }
  turbLastMvMed = mvMed;
  if (g_turbV >= TURB_PROBE_FAULT_V || mvMed >= 3100) {
    if (g_turbLowCount < 255) g_turbLowCount++;
  } else {
    g_turbLowCount = 0;
  }
  g_turbFault = (g_turbLowCount >= TURB_FAULT_N) ||
                (turbFreezeCount >= TURB_FREEZE_N);
  if (g_turbFault) {
    g_turbLevel = "tidak merespons";
  } else {
    g_turbLevel = (turbFrac >= 0.75f) ? "jernih"
                : (turbFrac >= 0.40f) ? "sedang" : "keruh";
  }
  // Modul level air analog: rata-rata 5 bacaan agar tidak berisik (patch v5).
  int leakSum = 0;
  for (int i = 0; i < 5; i++) { leakSum += analogRead(LEAK_PIN); delay(2); }
  g_leakRaw = leakSum / 5;
  g_leak = (g_leakRaw > LEAK_THRESHOLD);
  // v7.1.1: baca pin SATU kali ke g_rainRaw; g_rain diturunkan darinya.
  g_rainRaw = digitalRead(RAIN_PIN);
  g_rain = (g_rainRaw == (RAIN_ACTIVE_LOW ? LOW : HIGH));
}

void readTDS() {
  int raw = analogRead(TDS_PIN);
  float v = raw * 3.3f / 4095.0f;
  float t = (g_tempC > -127.0) ? g_tempC : 25.0f;
  float vc = v / (1.0f + 0.02f * (t - 25.0f));
  float tds = (133.42f * vc * vc * vc - 255.86f * vc * vc + 857.39f * vc) * 0.5f;
  g_tdsPpm = (tds < 0) ? 0 : tds;
}

void pollTemperature(unsigned long now) {
  if (!g_tempPending && (now - g_lastTempReq >= TEMP_READ_INTERVAL_MS)) {
    tempSensor.requestTemperatures();
    g_tempReqAt = now; g_lastTempReq = now; g_tempPending = true;
    g_tempRetried = false;   // v7.2.1: siklus baru - pemulihan boleh dicoba sekali lagi
  }
  if (g_tempPending && (now - g_tempReqAt >= TEMP_CONV_WAIT_MS)) {
    float t = tempSensor.getTempCByIndex(0);
    if (t > -127.0) {
      // v7.2.1: bacaan valid pertama sesudah episode gagal -> catat pulih sekali.
      if (g_suhuFailEpisode) {
        Serial.println("[SUHU] bacaan pulih - suhu valid lagi");
        g_suhuFailEpisode = false;
      }
      g_tempC = t;
      g_tempPending = false;
    } else if (!g_tempRetried) {
      // v7.2.1: baca putus (-127) -> pulihkan SEKALI dalam siklus ini:
      // enumerasi ulang bus (tempSensor.begin()), lalu SATU konversi +
      // baca ulang dengan pola asinkron yang sama (request -> tunggu
      // TEMP_CONV_WAIT_MS -> baca). g_lastTempReq TIDAK digeser agar
      // irama TEMP_READ_INTERVAL_MS tetap seperti sebelumnya.
      g_tempRetried = true;
      g_suhuFailEpisode = true;
      tempSensor.begin();
      int nDev = (int)tempSensor.getDeviceCount();
      int busState = (nDev > 0) ? 1 : 0;
      if (busState != g_suhuBusState) {   // log [SUHU] hanya pada transisi keadaan bus
        g_suhuBusState = busState;
        if (nDev > 0) {
          DeviceAddress rom;
          if (tempSensor.getAddress(rom, 0)) {
            Serial.printf("[SUHU] enumerasi bus: %d perangkat, ROM[0] = %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
                          nDev, rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
          } else {
            Serial.printf("[SUHU] enumerasi bus: %d perangkat (ROM[0] tak terbaca)\n", nDev);
          }
        } else {
          Serial.println("[SUHU] bus 1-Wire kosong (0 perangkat) - tak ada DS18B20 yang menjawab");
        }
      }
      tempSensor.requestTemperatures();
      g_tempReqAt = now;   // g_tempPending tetap true: baca ulang terjadi sesudah TEMP_CONV_WAIT_MS
    } else {
      g_tempPending = false;   // percobaan ulang pun gagal - akhiri siklus ini, nilai lama dipertahankan
    }
  }
}

// ============================ WQI (Water Quality Index) =======================
// Skor tunggal 0-100 (makin tinggi = makin baik): rata-rata tiga sub-indeks
// berbobot setara. VERSI SEDERHANA untuk lomba -- asumsi optimum & rentang
// linear di bawah ini WAJIB dikalibrasi/dijustifikasi dengan data lokasi uji
// sebelum masuk paper (lihat fir_dokumen/catatan_WQI.md).
int computeWQI() {
  if (g_tempC <= -127.0) return -1;  // suhu belum valid -> WQI belum valid
  // v7.0.6: sensor kekeruhan fault -> WQI TIDAK valid (-1, tampil "-"),
  // sama seperti suhu tidak valid. Cabang v7.0.3 (rata-rata dua sub-indeks
  // sehat) dihapus: ia menampilkan WQI 100 (BAIK) padahal sensor mati -
  // menyesatkan. (Aturan ini sempat gugur di mode D v7.2.0 dan
  // dipulihkan di v7.2.2 bersama kembalinya sumber A.)
  if (g_turbFault) return -1;
  // Sub-indeks suhu: 100 bila |T-28|<=2 C, turun linear ke 0 di batas alert.
  float dT = fabsf(g_tempC - WQI_TEMP_OPT);
  float span = (g_tempC >= WQI_TEMP_OPT)
      ? (TEMP_HIGH_ALERT - WQI_TEMP_OPT) : (WQI_TEMP_OPT - TEMP_LOW_ALERT);
  float subT = (dT <= WQI_TEMP_TOL) ? 100.0f
      : 100.0f * (1.0f - (dT - WQI_TEMP_TOL) / (span - WQI_TEMP_TOL));
  subT = constrain(subT, 0.0f, 100.0f);
  // Sub-indeks kekeruhan v7.2.3 (mode probe langsung): fraksi
  // kejernihan dari tegangan simpul di antara dua jangkar probe -
  // simpul di TURB_PROBE_CLEAR_V -> 100, di TURB_PROBE_DARK_V -> 0.
  // Linear di antaranya. (Rumus mode A dari tegangan pin A gugur
  // bersama bypass adaptor ini.)
  float subTb = 100.0f * (TURB_PROBE_DARK_V - g_turbV) / (TURB_PROBE_DARK_V - TURB_PROBE_CLEAR_V);
  subTb = constrain(subTb, 0.0f, 100.0f);
  // Sub-indeks TDS: 0 ppm -> 100; >= 500 ppm (baku mutu) -> 0.
  float subTds = 100.0f * (1.0f - g_tdsPpm / TDS_ALERT_PPM);
  subTds = constrain(subTds, 0.0f, 100.0f);
  return constrain((int)lroundf((subT + subTb + subTds) / 3.0f), 0, 100);
}

// ============================ EARLY WARNING ===================================
void evaluateAlert() {
  String msg = "";
  if (g_turbLevel == "keruh" && !g_turbFault) msg += "Kekeruhan ABNORMAL; ";
  if (g_turbFault) msg += "Sensor kekeruhan tidak merespons (cek kabel kuning probe / resistor simpul / suplai adaptor); ";
  if (g_tdsPpm >= TDS_ALERT_PPM) msg += "TDS tinggi (" + String(g_tdsPpm, 0) + " ppm); ";
  if (g_tempC > -127.0) {
    if (g_tempC >= TEMP_HIGH_ALERT) msg += "Suhu tinggi (" + String(g_tempC, 1) + " C); ";
    else if (g_tempC <= TEMP_LOW_ALERT) msg += "Suhu rendah (" + String(g_tempC, 1) + " C); ";
  }
  bool triggered = msg.length() > 0;
  if (triggered) { g_clearCount = 0; if (g_alertCount < 255) g_alertCount++; }
  else           { g_alertCount = 0; if (g_clearCount < 255) g_clearCount++; }

  if (!g_alert && g_alertCount >= ALERT_DEBOUNCE_N) {
    g_alert = true; g_alertMsg = msg;
    g_buzzBurstActive = true;            // v6.5: bunyikan ledakan 3 bip, sekali ini saja
    g_buzzBurstStart = millis();
    Serial.printf("[ALERT] AKTIF: %s\n", msg.c_str());
  } else if (g_alert && g_clearCount >= ALERT_DEBOUNCE_N) {
    g_alert = false; g_alertMsg = "";
    g_buzzBurstActive = false;
    Serial.println("[ALERT] clear — kondisi kembali normal.");
  } else if (g_alert) {
    g_alertMsg = msg;
  }
}

void pollBuzzer(unsigned long now) {
  if (BUZZER_PIN < 0) return;
  // Pola v6.5: buzzer hanya berbunyi sebagai ledakan singkat (3 bip)
  // saat ALERT pertama kali aktif. Sesudah itu DIAM walaupun ALERT
  // masih menyala (banner dashboard tetap tampil sebagai peringatan).
  // Bunyi lagi hanya bila ALERT sempat mati lalu aktif kembali.
  // v7.3.6: bila pola ALERT diam, buzzer dipakai pola indikator cas
  // (g_chgBuzzOn dari pollCharge). ALERT selalu menang.
  bool on = false;
  if (g_alert && g_buzzBurstActive) {
    unsigned long elapsed = now - g_buzzBurstStart;
    // 3 bip: 180 ms bunyi / 180 ms diam -> selesai dalam +-1,1 detik.
    if (elapsed >= 1080) g_buzzBurstActive = false;
    else on = ((elapsed / 180) % 2) == 0;
  }
  // v7.3.9: pola bip boot 2x - di bawah ALERT, di atas pola cas.
  // 2 bip pendek: 150 bunyi / 150 diam / 150 bunyi -> tuntas 450 ms.
  if (!on && g_bootBeepActive) {
    unsigned long e = now - g_bootBeepStart;
    if (e >= 450) g_bootBeepActive = false;
    else on = (e < 150) || (e >= 300 && e < 450);
  }
  if (!on) on = g_chgBuzzOn;
  if (on != g_buzzOn) { g_buzzOn = on; digitalWrite(BUZZER_PIN, on ? HIGH : LOW); }
}

// v7.3.6: pembaca tap CHRG TP4056 (GPIO44) + penjadwal pola bunyi cas.
// Non-blocking: hanya membaca pin dan menghitung garis waktu pola.
// LOW stabil = mengecas; lepas ke HIGH sesudah >= 10 menit = PENUH.
void pollCharge(unsigned long now) {
  // --- garis waktu pola yang sedang berjalan -> level buzzer ---
  if (g_chgPatKind == 1) {
    unsigned long e = now - g_chgPatStart;
    // 2 bip pendek: 150 bunyi / 150 diam / 150 bunyi -> tuntas 450 ms.
    g_chgBuzzOn = (e < 150) || (e >= 300 && e < 450);
    if (e >= 450) { g_chgPatKind = 0; g_chgBuzzOn = false; }
  } else if (g_chgPatKind == 2) {
    unsigned long e = now - g_chgPatStart;
    // 3 bip panjang: (700 bunyi / 300 diam) x3 -> tuntas 2700 ms.
    g_chgBuzzOn = (e < 700) || (e >= 1000 && e < 1700) || (e >= 2000 && e < 2700);
    if (e >= 2700) { g_chgPatKind = 0; g_chgBuzzOn = false; }
  } else {
    g_chgBuzzOn = false;
  }

  // --- sampel pin tiap 250 ms ---
  // v7.4.2: gerbang asimetris. Menuju status "mengecas" butuh LOW
  // STABIL 5 DETIK (20 sampel) - level liar sesaat (node tap
  // mengambang saat charger tidak berdaya, gangguan jalur serial)
  // tidak lagi cukup untuk mendeklarasikan cas. Lepas dari cas
  // (menuju PENUH / dicabut) tetap cepat: 3 sampel (750 ms).
  if (now - g_chgLastSample < 250) return;
  g_chgLastSample = now;
  bool raw = (digitalRead(CHG_PIN) == LOW);  // LOW = chip menarik = mengecas
  if (raw == g_chgCand) { if (g_chgCandN < 20) g_chgCandN++; }
  else { g_chgCand = raw; g_chgCandN = 1; }
  int need = g_chgCand ? 20 : 3;
  if (g_chgCandN < need || g_chgCand == g_chgCharging) {
    // tidak ada transisi: jadwalkan bip berkala selama cas stabil
    if (g_chgCharging && g_chgPatKind == 0 && now >= g_chgNextBeep) {
      g_chgPatKind = 1; g_chgPatStart = now; g_chgNextBeep = now + 60000;
    }
    return;
  }
  // --- transisi stabil diterima ---
  g_chgCharging = g_chgCand;
  if (g_chgCharging) {
    g_chgSince = now;
    g_chgPatKind = 1; g_chgPatStart = now; g_chgNextBeep = now + 60000;
    Serial.println("[CHG] mengecas terdeteksi (pin CHRG LOW).");
  } else {
    unsigned long dur = (g_chgSince > 0) ? (now - g_chgSince) : 0;
    if (dur >= 600000) {
      g_chgPatKind = 2; g_chgPatStart = now;
      Serial.printf("[CHG] PENUH: cas selesai sesudah %lu menit.\n",
                    (unsigned long)(dur / 60000));
    } else {
      Serial.printf("[CHG] cas berhenti sesudah %lu detik (belum 10 menit - dianggap dicabut, bukan penuh).\n",
                    (unsigned long)(dur / 1000));
    }
    g_chgSince = 0;
  }
}

// ============================ MOTOR DRV8833 ===================================
// v7.4.0 MOTOR LANGSUNG: satu sisi: maju = pin A PWM + pin B 0;
// mundur = pin A 0 + pin B PWM (sign-magnitude simetris, persis
// v7.1.2); keduanya 0 = berhenti. Perintah ditulis ke LEDC SAAT ITU
// JUGA - tidak ada slew, tidak ada ramp, tidak ada penahan diam.
// API LEDC gaya channel (Arduino-ESP32 core 2.x bawaan PlatformIO espressif32).
// Channel 0 dipakai kamera - motor memakai channel 4-7.
int g_motorSpeed = 70;               // persen, dari slider dashboard
unsigned long g_motorDeadline = 0;   // watchdog berhenti otomatis
unsigned long g_motorTestUntil = 0;  // v7.8.2: akhir jendela uji tahan motor (0 = mati)
int g_curL = 0, g_curR = 0;          // persen yang sedang diterapkan ke LEDC
int g_tgtL = 0, g_tgtR = 0;          // persen target dari perintah terakhir
// v7.4.0: antrean perintah motor yang datang saat sesi SD aktif -
// dulu perintah itu DIBUANG; kini disimpan dan dijalankan otomatis
// oleh pollMotors begitu sesi selesai.
char g_pendCmd = 0;                  // 0 = tidak ada antrean
int  g_pendSpd = 0;

// Deklarasi awal (definisi di bawah, masih di blok motor ini):
void motorWake();

// v7.4.0: driveSide kembali PERSIS v7.1.2 (sign-magnitude simetris).
// Skema anti-phase mundur v7.1.6 DIBATALKAN.
void driveSide(int chA, int chB, int speedPct) {
  // v7.6.1: tulis duty lewat driver ESP-IDF langsung (bukan lagi
  // ledcWrite Arduino) - pasangan motorPwmAttach() di initMotors.
  int duty = constrain(abs(speedPct), 0, 100) * 255 / 100;
  int dA = (speedPct >= 0) ? duty : 0;
  int dB = (speedPct >= 0) ? 0 : duty;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)chA, (uint32_t)dA);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)chA);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)chB, (uint32_t)dB);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)chB);
}

// v7.4.0: terapkan target ke LEDC SEKARANG (tanpa slew). EEP dipaksa
// HIGH dulu - driver tidak boleh tertidur sesudah perintah jalan.
static void applyMotorsNow() {
  g_curL = g_tgtL;
  g_curR = g_tgtR;
  if (!g_ledcAttached || g_sdSession) return;
  motorWake();
  driveSide(4, 5, g_curL);   // channel 4/5 -> MOTOR_L_IN1/IN2
  driveSide(6, 7, g_curR);   // channel 6/7 -> MOTOR_R_IN1/IN2
}

void setMotors(int leftPct, int rightPct) {
  // Kalibrasi arah & keselarasan roda diterapkan di satu titik ini,
  // supaya semua perintah (F/B/L/R) otomatis ikut terkoreksi.
  if (MOTOR_SWAP_LR) { int t = leftPct; leftPct = rightPct; rightPct = t; }
  if (MOTOR_L_INVERT) leftPct = -leftPct;
  if (MOTOR_R_INVERT) rightPct = -rightPct;
  leftPct  = leftPct  * MOTOR_L_TRIM / 100;
  rightPct = rightPct * MOTOR_R_TRIM / 100;
  g_tgtL = leftPct;
  g_tgtR = rightPct;
  applyMotorsNow();   // v7.4.0: LANGSUNG ditulis, tidak menunggu poll
}

void stopMotors() { setMotors(0, 0); g_motorDeadline = 0; g_motorTestUntil = 0; }  // v7.8.2: stop juga membatalkan uji tahan

// Berhenti PENUH seketika (target & terapan 0, ditulis ke LEDC saat
// itu juga) - dipakai interlock sesi SD dan initMotors, karena pin
// dilepas/dipasang ulang dari LEDC di sana dan driver dibangunkan
// tepat sesudahnya.
void motorForceStopNow() {
  g_tgtL = g_tgtR = 0;
  g_curL = g_curR = 0;
  g_motorDeadline = 0;
  g_pendCmd = 0;   // v7.4.0: berhenti penuh juga membatalkan antrean
  if (g_ledcAttached) { driveSide(4, 5, 0); driveSide(6, 7, 0); }
}

void motorCommand(char cmd, int speedPct) {
  g_motorSpeed = constrain(speedPct, 0, 100);
  int s = g_motorSpeed;
  int l = 0, r = 0;  // v7.1.3: pasangan target perintah ini, untuk baris bukti [MOTOR]
  switch (cmd) {
    case 'F': l = s;  r = s;  break;  // maju
    case 'B': l = -s; r = -s; break;  // mundur
    case 'L': l = -s; r = s;  break;  // putar kiri di tempat
    case 'R': l = s;  r = -s; break;  // putar kanan di tempat
    default:  // 'S' atau apa pun = berhenti
      stopMotors();
      Serial.printf("[MOTOR] cmd=%c spd=%d -> target L=0 R=0\n", cmd, s);
      return;
  }
  setMotors(l, r);
  // v7.1.3: penanda bukti [MOTOR] - HANYA di sini, bukan di setMotors/
  // stopMotors, agar watchdog & sesi SD tidak ikut mencetak (spam).
  // Baris ini tampil = perintah benar-benar diterima program dan
  // targetnya persis seperti yang tertulis.
  Serial.printf("[MOTOR] cmd=%c spd=%d -> target L=%d R=%d\n", cmd, s, l, r);
  // v7.4.0: saksi MOTOR-LANGSUNG - duty empat channel persis yang
  // baru saja ditulis applyMotorsNow ke LEDC + status EEP & sesi SD.
  // v7.4.1: EEP kini BACA-BALIK dari pin (digitalRead sesudah
  // ditulis HIGH) - angka jujur dari pad, bukan klaim program.
  {
    int dL = constrain(abs(g_curL), 0, 100) * 255 / 100;
    int dR = constrain(abs(g_curR), 0, 100) * 255 / 100;
    Serial.printf("[MOTOR-LANGSUNG] IN1=%d IN2=%d IN3=%d IN4=%d | EEP terbaca=%d sesiSD=%d\n",
                  (g_curL >= 0) ? dL : 0, (g_curL >= 0) ? 0 : dL,
                  (g_curR >= 0) ? dR : 0, (g_curR >= 0) ? 0 : dR,
                  digitalRead(MOTOR_EEP_PIN),
                  g_sdSession ? 1 : 0);
  }
  g_motorDeadline = millis() + MOTOR_WATCHDOG_MS;
}

void pollMotors(unsigned long now) {
  if (g_sdSession) return;  // sesi SD aktif: pin motor sedang dipinjam bus SD
  // v7.8.2: UJI TAHAN MOTOR - selama jendela uji, watchdog motor
  // terus didorong ke akhir jendela supaya duty bertahan penuh
  // untuk pengukuran multimeter hands-free; status dicetak 1x/dtk.
  if (g_motorTestUntil) {
    if ((long)(now - g_motorTestUntil) >= 0) {
      g_motorTestUntil = 0;
      stopMotors();
      Serial.println("[MOTOR-TEST] SELESAI - motor berhenti otomatis");
    } else {
      g_motorDeadline = g_motorTestUntil;
      static unsigned long s_lastTestPrint = 0;
      if (now - s_lastTestPrint >= 1000) {
        s_lastTestPrint = now;
        Serial.printf("[MOTOR-TEST] jalan: sisa %lu dtk | target L=%d R=%d | EEP terbaca=%d\n",
                      (unsigned long)(g_motorTestUntil - now) / 1000, g_tgtL, g_tgtR,
                      digitalRead(MOTOR_EEP_PIN));
      }
    }
  }
  // v7.4.0: antrean perintah (datang saat sesi SD aktif) dijalankan
  // otomatis begitu sesi selesai - perintah tidak hilang lagi.
  if (g_pendCmd) {
    char c = g_pendCmd; int sp = g_pendSpd;
    g_pendCmd = 0;
    Serial.printf("[MOTOR] ANTRI dijalankan: cmd=%c spd=%d (sesi SD selesai)\n", c, sp);
    motorCommand(c, sp);
  }
  if (g_motorDeadline && (long)(now - g_motorDeadline) >= 0) stopMotors();
  // v7.4.0: tanpa slew - duty sudah diterapkan langsung di setMotors.
  // Jaring pengaman: bila terapan menyimpang dari target (mis. sesi
  // SD baru selesai), samakan sekarang + paksa EEP HIGH lagi.
  if (g_curL != g_tgtL || g_curR != g_tgtR) applyMotorsNow();
  else if ((g_tgtL != 0 || g_tgtR != 0) && g_ledcAttached) {
    motorWake();
    // v7.4.1: tulis ulang duty tiap 50 ms selama target bukan nol -
    // bila channel LEDC sempat diubah proses lain, terapan pulih.
    static unsigned long s_lastReapply = 0;
    if (now - s_lastReapply >= 50) {
      s_lastReapply = now;
      driveSide(4, 5, g_curL);
      driveSide(6, 7, g_curR);
    }
  }
}

// Kunci pin motor LOW secara digital SEJAK AWAL setup (sebelum kamera &
// WiFi mulai), supaya input DRV8833 tidak mengambang saat board booting
// atau firmware terhenti di tengah - dinamo tidak boleh jalan sendiri.
// PWM LEDC baru dipasang belakangan oleh initMotors().
void motorPinsSafeLow() {
  const int pins[4] = { MOTOR_L_IN1, MOTOR_L_IN2, MOTOR_R_IN1, MOTOR_R_IN2 };
  for (int i = 0; i < 4; i++) { pinMode(pins[i], OUTPUT); digitalWrite(pins[i], LOW); }
}

// Kendali tidur DRV8833 lewat EEP (nSLEEP) di GPIO45 (v6.9.1).
// Driver tidur = output H-bridge mati total: dinamo mustahil bergerak
// walau pin IN berdenyut - inilah jantung pengaman sesi SD card.
// GPIO45 adalah pin strapping VDD_SPI: aman dipakai karena EEP
// ber-pull-down internal sehingga pin terbaca LOW saat boot (keadaan
// normal yang dibutuhkan board); firmware menaikkan HIGH hanya sesudah
// channel LEDC terpasang dan duty berhenti (0) sudah diterapkan.
void motorSleep() { digitalWrite(MOTOR_EEP_PIN, LOW); }
void motorWake()  { digitalWrite(MOTOR_EEP_PIN, HIGH); }

// v7.6.1: lapisan PWM ditulis ulang ke driver ESP-IDF langsung
// dengan clock APB eksplisit. Bukti board v7.5.2 (boot #1044):
// keempat pin 0% HIGH di SEMUA fase LEDC (maju & mundur, dua sisi)
// tetapi 100% HIGH di fase digitalWrite -> yang mati lapisan PWM
// Arduino-nya (timer LEDC tidak pernah berjalan: ledcSetup Arduino
// memakai clock bawaan XTAL/AUTO yang di board ini tidak
// menggerakkan timer 2/3), BUKAN pad, kabel, atau pin mundur.
// motorPwmAttach() dipakai initMotors DAN semua titik pasang-lagi
// (akhir sesi SD, self-test digital, cmd=T, watchdog sesi) supaya
// jalur pasangnya selalu sama. Frekuensi timer dibaca-balik dan
// dicetak sebagai bukti di log boot.
static const ledc_channel_t MOTOR_LEDC_CH[4] = {
  LEDC_CHANNEL_4, LEDC_CHANNEL_5, LEDC_CHANNEL_6, LEDC_CHANNEL_7
};

void motorPwmAttach() {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_8_BIT;
  t.freq_hz = 20000;
  t.clk_cfg = LEDC_USE_APB_CLK;
  t.timer_num = LEDC_TIMER_2;
  esp_err_t e2 = ledc_timer_config(&t);
  t.timer_num = LEDC_TIMER_3;
  esp_err_t e3 = ledc_timer_config(&t);
  const int pins[4] = { MOTOR_L_IN1, MOTOR_L_IN2, MOTOR_R_IN1, MOTOR_R_IN2 };
  for (int i = 0; i < 4; i++) {
    ledc_channel_config_t c = {};
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = MOTOR_LEDC_CH[i];
    c.timer_sel = (i < 2) ? LEDC_TIMER_2 : LEDC_TIMER_3;
    c.intr_type = LEDC_INTR_DISABLE;
    c.gpio_num = pins[i];
    c.duty = 0;
    c.hpoint = 0;
    ledc_channel_config(&c);
  }
  Serial.printf("[MOTOR] PWM IDF langsung: timer2 err=%d freq=%u Hz | timer3 err=%d freq=%u Hz (harapan err 0, freq 20000)\n",
                (int)e2, (unsigned)ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_2),
                (int)e3, (unsigned)ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_3));
}

void initMotors() {
  motorPwmAttach();
  g_ledcAttached = true;
  motorForceStopNow();   // cur=tgt=0 diterapkan ke LEDC dulu...
  motorWake();    // ...baru driver dibangunkan, paling akhir inisialisasi
  Serial.println("DRV8833: PWM 20 kHz IDF langsung (clock APB) siap di GPIO38-41 (EEP dikendalikan GPIO45)");
}

// v7.4.1: SELF-TEST MOTOR SAAT BOOT - dipanggil sekali dari setup
// sesudah initMotors(), SEBELUM WiFi mulai. Menggerakkan kiri lalu
// kanan tanpa HP/dashboard/perintah: jalur drive diuji dalam
// keadaan paling sederhana yang mungkin. Robot WAJIB dipegang /
// diangkat sebelum dinyalakan bila self-test ini mau diamati.
// v7.4.2: diperkuat jadi SAKSI UKUR - duty 100%, dan selama sisi
// aktif, pin IN-nya disampel terus untuk menghitung persen terbaca
// HIGH langsung di pad ESP.
// v7.5.1: HASIL UKUR PERTAMA (6 Nov 11:34): kedua sisi 0% HIGH walau
// EEP terbaca=1 dan seluruh rantai kode terudit bersih (define lurus
// 38/39/40/41, trim 100, sesi SD selesai sebelum self-test). Artinya
// perintah 100% TIDAK menjadi tegangan di pad. Self-test kini DUA
// FASE per sisi untuk membuktikan lapisan yang mati:
//   FASE 1 = jalan normal (LEDC PWM 100%);
//   FASE 2 = LEDC dilepas total, pin dikendarai digitalWrite HIGH.
// Vonis: FASE2 ~100% tapi FASE1 0% = lapisan LEDC yang mati (murni
// kode/periferal). FASE2 ikut 0% = pad ditahan LOW secara fisik
// (jalur pin terbebani/dipegang pihak lain di board).
// v7.5.2: cakupan diperluas ke EMPAT pin + DUA arah. Laporan user
// 6 Nov 16:38: kendali motor SUDAH BISA (maju/putar jalan) kecuali
// MUNDUR. Mundur memakai pin lawan sebagai pin aktif PWM (IN2 kiri,
// IN4 kanan) - dua pin itu belum pernah diukur sebagai pin aktif.
// Kini tiap sisi diuji MAJU lalu MUNDUR lewat LEDC (jalan normal);
// arah yang terbaca rendah (<50%) langsung diuji ulang DIGITAL
// (LEDC dilepas) untuk memisahkan mati-LEDC vs pad tertahan fisik.
static void stMeasure(int pinAktif, int pinLawan, long* pctA, long* pctB) {
  unsigned long t0 = millis();
  long n = 0, hiA = 0, hiB = 0;
  while (millis() - t0 < 900) {
    n++;
    if (digitalRead(pinAktif)) hiA++;
    if (digitalRead(pinLawan)) hiB++;
  }
  *pctA = n ? hiA * 100 / n : 0;
  *pctB = n ? hiB * 100 / n : 0;
}

static void stDigital(const char* nama, const char* arah, int pinAktif, int pinLawan) {
  const int pins[4] = { MOTOR_L_IN1, MOTOR_L_IN2, MOTOR_R_IN1, MOTOR_R_IN2 };
  for (int i = 0; i < 4; i++) {
    ledcDetachPin(pins[i]);
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], LOW);
  }
  motorWake();
  digitalWrite(pinAktif, HIGH);
  long a, b;
  stMeasure(pinAktif, pinLawan, &a, &b);
  digitalWrite(pinAktif, LOW);
  motorPwmAttach();  // v7.6.1: pasang lagi lewat jalur IDF yang sama
  motorForceStopNow();
  motorWake();
  Serial.printf("[SELFTEST] %s %s (DIGITAL): pin aktif %ld%% HIGH, pin lawan %ld%% (harapan 100/0)\n",
                nama, arah, a, b);
}

static void selftestSide(const char* nama, bool kiri) {
  int pinA = kiri ? MOTOR_L_IN1 : MOTOR_R_IN1;   // aktif saat MAJU
  int pinB = kiri ? MOTOR_L_IN2 : MOTOR_R_IN2;   // aktif saat MUNDUR
  long a, b;
  // MAJU lewat LEDC (jalan normal)
  if (kiri) setMotors(100, 0); else setMotors(0, 100);
  stMeasure(pinA, pinB, &a, &b);
  stopMotors();
  applyMotorsNow();
  Serial.printf("[SELFTEST] %s MAJU (LEDC): pin aktif %ld%% HIGH, pin lawan %ld%% (harapan 100/0)\n",
                nama, a, b);
  long pctMaju = a;
  // MUNDUR lewat LEDC (jalan normal)
  if (kiri) setMotors(-100, 0); else setMotors(0, -100);
  stMeasure(pinB, pinA, &a, &b);
  stopMotors();
  applyMotorsNow();
  Serial.printf("[SELFTEST] %s MUNDUR (LEDC): pin aktif %ld%% HIGH, pin lawan %ld%% (harapan 100/0)\n",
                nama, a, b);
  long pctMundur = a;
  // Arah yang rendah diuji ulang DIGITAL sebagai pembeda lapisan.
  if (pctMaju < 50) stDigital(nama, "MAJU", pinA, pinB);
  if (pctMundur < 50) stDigital(nama, "MUNDUR", pinB, pinA);
}

void motorBootSelfTest() {
  Serial.println("[SELFTEST] MULAI (robot dipegang/diangkat!)");
  Serial.println("[SELFTEST] catatan daya: roda hanya bisa bergerak bila DRV8833 berdaya = baterai terpasang + saklar ON; dari USB saja (saklar OFF) driver tidak berdaya karena VCC-nya langsung dari MT3608.");
  selftestSide("kiri", true);
  delay(300);
  selftestSide("kanan", false);
  Serial.printf("[SELFTEST] SELESAI - EEP terbaca=%d (1 = perintah bangun sampai ke pin GPIO45)\n",
                digitalRead(MOTOR_EEP_PIN));
}

// v7.4.0: MODE UJI LANGSUNG cmd=T - memintas SELURUH jalur PWM/LEDC.
// Keempat pin motor dilepas dari LEDC, dikendarai digital penuh
// (IN1/IN3 HIGH, IN2/IN4 LOW) selama 1,2 detik, lalu semua LOW, LEDC
// dipasang lagi dan driver dibangunkan. Buka lewat browser di alamat
// robot: /motor?cmd=T - robot harus dalam keadaan terangkat/bebas
// (roda tidak menapak) saat uji ini dipakai.
void motorDirectTest() {
  if (g_sdSession || !g_ledcAttached) {
    Serial.println("[MOTOR-TEST] batal: sesi SD aktif / LEDC belum terpasang");
    return;
  }
  const int pins[4] = { MOTOR_L_IN1, MOTOR_L_IN2, MOTOR_R_IN1, MOTOR_R_IN2 };
  Serial.println("[MOTOR-TEST] MULAI: digital penuh IN1/IN3 HIGH, IN2/IN4 LOW, 1,2 dtk");
  motorForceStopNow();
  for (int i = 0; i < 4; i++) ledcDetachPin(pins[i]);
  for (int i = 0; i < 4; i++) pinMode(pins[i], OUTPUT);
  motorWake();   // EEP HIGH - driver bangun penuh
  digitalWrite(MOTOR_L_IN1, HIGH); digitalWrite(MOTOR_L_IN2, LOW);
  digitalWrite(MOTOR_R_IN1, HIGH); digitalWrite(MOTOR_R_IN2, LOW);
  delay(1200);
  for (int i = 0; i < 4; i++) digitalWrite(pins[i], LOW);
  motorPwmAttach();  // v7.6.1: pasang lagi lewat jalur IDF yang sama
  motorForceStopNow();
  motorWake();
  Serial.println("[MOTOR-TEST] SELESAI: pin kembali ke LEDC, duty 0, EEP HIGH");
}

void handleMotor() {
  String cmd = server.arg("cmd");
  int spd = server.hasArg("speed") ? server.arg("speed").toInt() : g_motorSpeed;
  char c = cmd.length() ? toupper(cmd[0]) : 0;
  // v7.4.0: mode uji langsung - hanya dari URL, bukan tombol dashboard.
  if (c == 'T') {
    motorDirectTest();
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }
  // Sesi SD aktif: pin motor sedang dipinjam bus SD.
  // v7.4.0: perintah TIDAK LAGI DIBUANG (perilaku v7.2.8 ke bawah) -
  // disimpan sebagai antrean dan dijalankan pollMotors begitu sesi
  // selesai. Perintah S tetap langsung membatalkan antrean.
  // v7.7.0: pengecualian - sesi LENGKET (kartu idle ter-mount milik
  // loopTask) dilepas saat itu juga, jadi perintah motor langsung
  // jalan tanpa menunggu. Antrean hanya untuk sesi berjalan milik
  // task lain (task AI sedang menulis foto kandidat).
  if (g_sdSession && g_sdSticky) sdSessionForceEnd();
  if (g_sdSession) {
    if (c == 'S') {
      g_pendCmd = 0;
      stopMotors();
      Serial.println("[MOTOR] cmd=S saat sesi SD - antrean dibatalkan");
    } else if (c) {
      g_pendCmd = c;
      g_pendSpd = spd;
      Serial.printf("[MOTOR] ANTRI: sesi SD aktif (%lu dtk) - cmd=%c spd=%d disimpan, jalan otomatis sesudah sesi\n",
                    (unsigned long)((millis() - g_sdSessionStartMs) / 1000), c, spd);
    }
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (c) motorCommand(c, spd);
  server.send(200, "application/json", "{\"ok\":true}");
}

// v7.8.2: UJI TAHAN MOTOR - menahan satu arah (F/B) pada kecepatan
// tetap selama 5-30 detik supaya pin DRV8833 bisa diukur multimeter
// tanpa memegang HP. Bukti board 8 Nov malam: perintah motor terbukti
// diterapkan firmware (IN1=178 @spd70, EEP terbaca=1) tetapi roda
// tidak berputar - fault terisolasi di hilir GPIO (VCC/EEP/OUT di
// papan driver), dan pengukuran di sana butuh duty yang BERTAHAN.
// Berhenti otomatis di akhir jendela; cmd=S membatalkan lebih awal.
void handleMotorTest() {
  server.sendHeader("Cache-Control", "no-store");
  int spd = server.hasArg("spd") ? constrain(server.arg("spd").toInt(), 10, 100) : 70;
  int sec = server.hasArg("sec") ? constrain(server.arg("sec").toInt(), 5, 30) : 15;
  char dir = 'F';
  if (server.hasArg("dir") && server.arg("dir").length()) {
    char d = toupper(server.arg("dir")[0]);
    if (d == 'B') dir = 'B';
  }
  if (g_sdSession && g_sdSticky) sdSessionForceEnd();
  if (g_sdSession) { server.send(503, "application/json", "{\"ok\":false,\"err\":\"sd-busy\"}"); return; }
  Serial.printf("[MOTOR-TEST] MULAI: arah %c spd=%d selama %d dtk - ROBOT DIPEGANG, roda bebas\n", dir, spd, sec);
  motorCommand(dir, spd);
  g_motorTestUntil = millis() + (unsigned long)sec * 1000UL;
  g_motorDeadline = g_motorTestUntil;
  { char lb[72]; snprintf(lb, sizeof(lb), "Uji tahan motor %c spd %d (%d dtk)", dir, spd, sec); logEvent(lb); }
  server.send(200, "application/json", String("{\"ok\":true,\"sec\":") + sec + "}");
}


// ============================ SETUP / LOOP ====================================
// ===================== KONFIGURASI WIFI RUNTIME (v7.3.2) =====================
// Kredensial STA kini bisa diganti dari aplikasi (tab WiFi Robot)
// tanpa flash ulang: tersimpan di LittleFS "/wifi.txt" (baris 1 =
// SSID, baris 2 = sandi) dan dimuat sebelum startWiFi. Bila berkas
// tidak ada, dipakai bawaan terkompilasi (WIFI_SSID/WIFI_PASS).
// Fail-safe: bila STA dengan kredensial baru gagal tersambung,
// startWiFi tetap jatuh ke AP darurat "FIR-Robot" seperti biasa -
// dari AP itu kredensial bisa diperbaiki lagi lewat aplikasi.
String g_staSsid = WIFI_SSID;
String g_staPass = WIFI_PASS;
bool g_staResting = false;             // v7.7.1: STA sedang diistirahatkan (mode AP saja)
unsigned long g_staNextRetryMs = 0;    // v7.7.1: kapan jendela coba berikutnya dibuka
unsigned long g_staRetryDeadlineMs = 0;  // v7.7.1: batas jendela coba berjalan (0 = tak ada)
unsigned long g_staDownSinceMs = 0;    // v7.7.1: sejak kapan STA putus (0 = tersambung)

void loadWifiConfig() {
  if (!fsReady) return;
  // v7.3.3: cek exists dulu - open() pada berkas yang belum ada
  // mencetak baris error VFS merah di log boot (tidak berbahaya,
  // tapi mengganggu). Berkas memang belum ada sebelum pengguna
  // pertama kali mengganti kredensial dari aplikasi.
  if (!LittleFS.exists("/wifi.txt")) return;
  File f = LittleFS.open("/wifi.txt", FILE_READ);
  if (!f) return;
  String s = f.readStringUntil('\n'); s.trim();
  String p = f.readStringUntil('\n'); p.trim();
  f.close();
  if (s.length() > 0) {
    g_staSsid = s;
    g_staPass = p;
    Serial.printf("[WIFI] kredensial tersimpan dipakai: '%s'\n", g_staSsid.c_str());
  }
}

bool saveWifiConfig(const String& ssid, const String& pass) {
  if (!fsReady) return false;
  File f = LittleFS.open("/wifi.txt", FILE_WRITE);
  if (!f) return false;
  f.println(ssid);
  f.println(pass);
  f.close();
  return true;
}

void handleWifiStatus() {
  bool sta = (WiFi.status() == WL_CONNECTED);
  String j = "{";
  j += "\"mode\":\"" + String(WiFi.getMode() == WIFI_AP ? "AP" : (sta ? "STA" : "STA-putus")) + "\",";
  j += "\"target_ssid\":\"" + g_staSsid + "\",";
  j += "\"sta_connected\":" + String(sta ? "true" : "false") + ",";
  j += "\"sta_ssid\":\"" + (sta ? WiFi.SSID() : String("")) + "\",";
  j += "\"sta_ip\":\"" + (sta ? WiFi.localIP().toString() : String("0.0.0.0")) + "\",";
  j += "\"rssi_dbm\":" + String(sta ? WiFi.RSSI() : 0) + ",";
  j += "\"ap_ssid\":\"" + String(AP_SSID) + "\",";
  j += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
  j += "\"custom\":" + String((fsReady && LittleFS.exists("/wifi.txt")) ? "true" : "false");
  j += "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void handleWifiSet() {
  if (server.hasArg("reset")) {   // kembali ke kredensial bawaan
    if (fsReady) LittleFS.remove("/wifi.txt");
    server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
    Serial.println("[WIFI] kredensial direset ke bawaan - restart");
    delay(500);
    ESP.restart();
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 63) {
    server.send(400, "application/json",
                "{\"ok\":false,\"err\":\"SSID 1-32 karakter, sandi maks 63\"}");
    return;
  }
  if (!saveWifiConfig(ssid, pass)) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"gagal menyimpan\"}");
    return;
  }
  g_staSsid = ssid;
  g_staPass = pass;
  server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  Serial.printf("[WIFI] kredensial STA diganti ke '%s' - restart\n", ssid.c_str());
  delay(500);
  ESP.restart();
}

// v7.7.1: istirahatkan STA. Radio ESP32 cuma satu: selama STA terus
// memindai/mencoba menyambung (auto-reconnect), beacon AP tersendat
// dan "FIR-Robot" hilang-timbul dari daftar WiFi HP. Dengan STA
// dimatikan (mode AP saja), AP memancar stabil terus. STA dibangunkan
// berkala oleh pollStaRest() untuk satu jendela coba singkat.
void staEnterRest() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  g_staResting = true;
  g_staRetryDeadlineMs = 0;
  g_staDownSinceMs = 0;
  g_staNextRetryMs = millis() + STA_REST_RETRY_MS;
  Serial.println("[WIFI] STA diistirahatkan (mode AP saja) - AP stabil; STA dicoba lagi tiap 5 menit");
}

// v7.7.1: mesin keadaan STA istirahat - dipanggil tiap putaran loop.
// v7.7.5: PROBE ULANG OTOMATIS. Bila kartu belum pernah mount di boot
// ini, tanya lagi tiap 60 detik (maks 20x) selama robot diam - kartu
// yang melewatkan jendela boot bisa menjawab belakangan saat rel
// benar-benar sepi; sekali mount, sesi lengket v7.7.0 menahannya.
// Bila 20x tetap diam, lognya adalah bukti kartu terkunci sejak
// power-on (wilayah fisik), bukan kekurangan jumlah percobaan.
void pollSdReprobe(unsigned long now) {
  if (g_sdEverMounted || g_sdSession) return;
  if (g_sdReprobeCount >= 20) return;
  if (g_motorDeadline && (long)(now - g_motorDeadline) < 0) return;
  // v7.8.1: jeda probe ulang MELEBAR (60 dtk -> 2 -> 4 -> 8 menit):
  // kartu mati tidak perlu ditanya tiap menit dengan tangga yang
  // membekukan kendali; kartu yang bangun tetap tertangkap <= 8 mnt.
  unsigned long reprobeInterval = 60000UL << ((g_sdReprobeCount < 3) ? g_sdReprobeCount : 3);
  if (g_sdReprobeLastMs && now - g_sdReprobeLastMs < reprobeInterval) return;
  g_sdReprobeLastMs = now;
  g_sdReprobeCount++;
  Serial.printf("[SD] probe ulang otomatis #%u (kartu belum pernah mount di boot ini; jeda berikut %lu dtk)\n",
                (unsigned)g_sdReprobeCount,
                (unsigned long)(60000UL << ((g_sdReprobeCount < 3) ? g_sdReprobeCount : 3)) / 1000UL);
  sdSessionBegin();
  sdSessionEnd();
  if (!g_sdEverMounted && g_sdReprobeCount >= 20) {
    Serial.println("[SD] probe ulang BERHENTI sesudah 20x - kartu tidak menjawab sepanjang boot ini; perlu siklus daya / periksa fisik slot.");
  }
}

void pollStaRest(unsigned long now) {
  if (g_staResting) {
    if (g_staRetryDeadlineMs) {  // sedang dalam jendela coba
      if (WiFi.status() == WL_CONNECTED) {
        g_staResting = false;
        g_staRetryDeadlineMs = 0;
        g_staDownSinceMs = 0;
        WiFi.setAutoReconnect(true);
        Serial.printf("[WIFI] STA tersambung lagi. IP: %s - AP tetap menyala (AP+STA)\n",
                      WiFi.localIP().toString().c_str());
      } else if ((long)(now - g_staRetryDeadlineMs) >= 0) {
        Serial.println("[WIFI] jendela coba STA habis - kembali istirahat");
        staEnterRest();
      }
    } else if ((long)(now - g_staNextRetryMs) >= 0) {
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(g_staSsid.c_str(), g_staPass.c_str());
      g_staRetryDeadlineMs = now + STA_REST_WINDOW_MS;
      Serial.println("[WIFI] membangunkan STA - jendela coba 12 dtk");
    }
    return;
  }
  // Tidak istirahat: STA yang pernah tersambung lalu putus lama
  // (>2 menit) ikut diistirahatkan - auto-reconnect diberi
  // kesempatan penuh dulu selama jendela itu.
  if (WiFi.status() != WL_CONNECTED) {
    if (!g_staDownSinceMs) g_staDownSinceMs = now;
    else if (now - g_staDownSinceMs > STA_DOWN_REST_MS) {
      Serial.println("[WIFI] STA putus >2 menit - diistirahatkan demi AP stabil");
      staEnterRest();
    }
  } else {
    g_staDownSinceMs = 0;
  }
}

void startWiFi() {
  Serial.printf("[RADIO] WiFi mulai | heap internal: %u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  WiFi.mode(WIFI_AP_STA);   // v7.3.3: AP robot selalu menyala bersama STA
  // Modem sleep DIMATIKAN (v7.1.2). Dulu ia WAJIB MENYALA
  // selama Bluetooth aktif (v7.0.2): koeksistensi RF WiFi+BT
  // mewajibkannya, dan warisan pengerasan v6.9.3 yang
  // mematikannya membuat driver WiFi ABORT tepat sesudah BLE
  // init - terbukti dari log board v7.0.1 boot #335:
  // "wifi:Error! Should enable WiFi modem sleep when both WiFi
  // and Bluetooth are enabled!!!!!!" lalu abort() -> PANIC ->
  // reboot loop. Sejak BLE dihapus total di v7.1.0 alasan itu
  // HILANG: tak ada lagi radio kedua yang berbagi RF. Setelan
  // kini kembali ke konfigurasi era WiFi-only v6.9.x
  // (setSleep(false)) yang terbukti stabil untuk AP +
  // dashboard; radio yang selalu terjaga juga menghilangkan
  // jeda bangun modem sleep dari jalur respons dashboard &
  // kendali motor. Auto-reconnect (v6.9.3) dipertahankan; dua
  // setelan ini berlaku untuk jalur STA maupun AP fallback.
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  // Daya pancar diturunkan selama asosiasi (v7.0.1): mitigasi
  // puncak arus asosiasi di bangku uji (rel daya USB) - rel yang
  // lemah bisa runtuh tepat di transisi radio ini. Bisa dinaikkan
  // lagi bila jangkauan STA dibutuhkan.
  WiFi.setTxPower(WIFI_POWER_13dBm);
  // v7.3.3: hotspot robot dinyalakan SELALU (mode AP+STA) -
  // robot terjangkau di 192.168.4.1 sambil STA tersambung ke
  // hotspot tujuan (mis. 192.168.1.25); web dashboard dan
  // aplikasi sama-sama jalan di kedua alamat. Bila STA
  // gagal/tempo, AP ini tetap hidup sebagai jalur masuk.
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("AP robot menyala: %s  IP: %s\n",
                AP_SSID, WiFi.softAPIP().toString().c_str());
  WiFi.begin(g_staSsid.c_str(), g_staPass.c_str());
  Serial.printf("Menyambung ke %s", g_staSsid.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
    delay(400); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nSTA tersambung. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nSTA gagal/tempo - robot tetap bisa dijangkau lewat AP robot (192.168.4.1)");
    staEnterRest();  // v7.7.1: berhenti memindai - AP dijamin stabil
  }
}

void setup() {
  g_loopTaskHandle = xTaskGetCurrentTaskHandle();  // v7.7.0: pemilik sesi lengket
  Serial.begin(115200);
  setenv("TZ", "WIB-7", 1);  // v7.8.0: zona waktu tampilan (WIB = UTC+7)
  tzset();
  delay(500);
  Serial.println("\n=== FIR v7.10.2: kamera + 3 parameter + WQI + EARLY WARNING + MOTOR LANGSUNG + SELFTEST UKUR + HUJAN + AI KOROSI + CV OTOMATIS + SD + FILE MANAGER + INDIKATOR CAS ===");

  // Alasan reset boot ini (v6.9.5): kematian boot sebelumnya menamai
  // dirinya sendiri di baris ini - PANIC / watchdog / brownout
  // terbaca langsung, tanpa menebak dari register dump.
  {
    esp_reset_reason_t rr = esp_reset_reason();
    const char* rrTeks = nullptr;
    switch (rr) {
      case ESP_RST_POWERON:  rrTeks = "nyala awal (power on)"; g_bootRstTag = "POWERON"; break;
      case ESP_RST_PANIC:    rrTeks = "PANIC (crash program)"; g_bootRstTag = "PANIC"; break;
      case ESP_RST_TASK_WDT: rrTeks = "TASK WATCHDOG"; g_bootRstTag = "TASK_WDT"; break;
      case ESP_RST_INT_WDT:  rrTeks = "INTERRUPT WATCHDOG"; g_bootRstTag = "INT_WDT"; break;
      case ESP_RST_WDT:      rrTeks = "WATCHDOG LAIN"; g_bootRstTag = "WDT"; break;
      case ESP_RST_BROWNOUT: rrTeks = "BROWNOUT (tegangan anjlok)"; g_bootRstTag = "BROWNOUT"; break;
      case ESP_RST_SW:       rrTeks = "software reset"; g_bootRstTag = "SW"; break;
      default: break;
    }
    if (rrTeks) Serial.printf("[Boot] alasan reset: %s\n", rrTeks);
    else        Serial.printf("[Boot] alasan reset: lainnya (%d)\n", (int)rr);
  }

  motorPinsSafeLow();  // PERTAMA: kunci dinamo diam sebelum apa pun dimulai
  pinMode(MOTOR_EEP_PIN, OUTPUT);
  motorSleep();        // v6.9.1: DRV8833 ditidurkan sejak awal boot -
                       // dibangunkan di akhir initMotors(), sesudah probe
                       // SD selesai dan PWM siap dengan duty berhenti.

  // LEAK_PIN (GPIO3) dibaca analog - tidak perlu pinMode.
  // v7.1.1: DO modul hujan LM393 itu open-collector - WAJIB pull-up;
  // tanpa itu pin mengambang dan bisa terkunci HIGH (tidak hujan terus).
  pinMode(RAIN_PIN, INPUT_PULLUP);
  if (BUZZER_PIN >= 0) { pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW); }
  pinMode(CHG_PIN, INPUT_PULLUP);  // v7.3.6: tap CHRG TP4056 (GPIO44 = bekas RX)
  Serial.println("[CHG] tap CHRG di GPIO44 siap - ini BUKAN deteksi cas; status mengecas diumumkan hanya sesudah pin LOW stabil 5 detik.");
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  tempSensor.begin();
  tempSensor.setWaitForConversion(false);
  Serial.printf("DS18B20 terdeteksi: %d sensor\n", tempSensor.getDeviceCount());

  fsReady = LittleFS.begin(true);
  Serial.printf("LittleFS: %s\n", fsReady ? "OK" : "GAGAL (logging flash nonaktif)");
  if (fsReady) {
    ensureLogHeader();
    File f = LittleFS.open(LOG_PATH, FILE_READ);
    if (f) {
      while (f.available()) {
        String l = f.readStringUntil('\n'); l.trim();
        if (!l.length() || l.startsWith("ts,")) continue;
        EnvLogRecord r{};
        int p1 = l.indexOf(','), p2 = l.indexOf(',', p1 + 1),
            p3 = l.indexOf(',', p2 + 1), p4 = l.indexOf(',', p3 + 1),
            p5 = l.indexOf(',', p4 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0) continue;
        r.ts = l.substring(0, p1).toInt();
        r.tempC = l.substring(p1 + 1, p2).toFloat();
        r.turbV = l.substring(p2 + 1, p3).toFloat();
        r.tdsPpm = l.substring(p3 + 1, p4).toFloat();
        r.leak = (p5 < 0 ? l.substring(p4 + 1) : l.substring(p4 + 1, p5)).toInt();
        if (p5 >= 0) {  // file v4/v5 lama berhenti di wqi; roll/pitch opsional
          int p6 = l.indexOf(',', p5 + 1);
          r.wqi = (p6 < 0) ? l.substring(p5 + 1).toInt()
                           : l.substring(p5 + 1, p6).toInt();
          // v7.10.0: kolom roll/pitch di berkas lama diabaikan (gyro dihapus).
        } else r.wqi = -1;
        addRecordToRam(r);
      }
      f.close();
      Serial.printf("Log dimuat ke RAM: %d record\n", ramCount);
    }
    ramSessionBase = ramCount;  // semua record hasil muatan = sesi lama
    // Nomor sesi boot persisten: dibaca, dinaikkan, ditulis kembali -
    // supaya riwayat AI bisa dibedakan antar sesi menyala.
    if (LittleFS.exists(BOOT_PATH)) {
      File bf = LittleFS.open(BOOT_PATH, FILE_READ);
      if (bf) { g_bootNo = (uint32_t)bf.readString().toInt(); bf.close(); }
    }
    g_bootNo++;
    File bw = LittleFS.open(BOOT_PATH, FILE_WRITE);
    if (bw) { bw.print(g_bootNo); bw.close(); }
    // Riwayat AI: hitung deteksi lama, lalu tulis penanda sesi ini.
    ensureAiLogHeader();
    if (LittleFS.exists(AI_LOG_PATH)) {
      File cf = LittleFS.open(AI_LOG_PATH, FILE_READ);
      if (cf) {
        while (cf.available()) {
          String l = cf.readStringUntil('\n'); l.trim();
          if (l.length() && !l.startsWith("boot,") && !l.startsWith("#")) g_aiLogCount++;
        }
        cf.close();
      }
      File af = LittleFS.open(AI_LOG_PATH, FILE_APPEND);
      if (af) { af.printf("#BOOT %lu v7.6.9 rst=%s\n", (unsigned long)g_bootNo, g_bootRstTag); af.close(); }
    }
    Serial.printf("Sesi boot #%lu | riwayat AI tersimpan: %ld deteksi\n",
                  (unsigned long)g_bootNo, g_aiLogCount);
  }

  // Penahan anti-putaran crash (v6.9.4): penanda /aiguard.txt hanya
  // masih ada bila boot sebelumnya MATI di tengah inferensi (penanda
  // selalu dihapus sesudah run_classifier kembali). Hapus penandanya
  // dan tahan mode otomatis khusus boot ini; boot berikutnya normal
  // lagi. Analisis manual lewat /analyze TIDAK ditahan.
  if (fsReady && LittleFS.exists(AI_GUARD_PATH)) {
    LittleFS.remove(AI_GUARD_PATH);
    g_aiAutoHeld = true;
    Serial.println("[AI] boot sebelumnya mati saat inferensi - mode otomatis ditahan boot ini (anti putaran crash)");
  }

  bool aiSetAda = fsReady && LittleFS.exists(AI_SET_PATH);
  loadAiAutoSetting();
  loadTurbOverride();   // v7.10.0: pulihkan override klasifikasi turbidity (/adminin)
  // v7.3.4: KUNCI ANTI-PUTARAN. Penahan lama hanya menahan mode
  // otomatis SATU boot; boot sesudahnya analisis otomatis jalan
  // lagi dan - selama penyebab crash inferensinya belum beres -
  // mati lagi di titik yang sama: putaran reboot tanpa henti
  // (teramati 3 Nov malam, boot #606 dan terus naik). Kini: bila
  // boot sebelumnya mati di tengah inferensi, mode otomatis
  // DIMATIKAN SECARA PERSISTEN (tersimpan) dan hanya hidup lagi
  // bila dinyalakan manual dari dashboard/aplikasi.
  if (g_aiAutoHeld && g_aiAuto) {
    g_aiAuto = false;
    saveAiAutoSetting();
    Serial.println("[AI] mode otomatis DIMATIKAN PERMANEN (anti putaran crash) - nyalakan lagi manual dari dashboard/aplikasi sesudah penyebab crash beres");
  }
  Serial.printf("Mode otomatis (CV): %s (saklar di dashboard, tersimpan%s)%s\n",
                g_aiAuto ? "AKTIF" : "mati",
                aiSetAda ? "" : ", bawaan: nyala",
                g_aiAutoHeld ? " - DITAHAN boot ini (anti putaran crash)" : "");

  // v7.7.5: PROBE SD PALING AWAL. Mutex dibuat di sini (sdSessionBegin
  // membutuhkannya) dan kartu di-mount SEBELUM kamera/AI/WiFi menyala -
  // momen rel 3,3 V paling sepi di seluruh boot, supaya tahap ACMD41
  // yang gagal 0x107 tidak lagi berbagi rel dengan beban lain. Pin
  // motor sudah dikunci diam di atas; kamera tidak memakai pin 38-40.
  g_sdMutex = xSemaphoreCreateMutex();
  Serial.println("[SD] probe awal boot (sebelum kamera/AI/WiFi)");
  sdSessionBegin();
  sdSessionEnd();

  if (!initCamera()) {
    // v6.3: kamera gagal TIDAK lagi mematikan firmware - WiFi, dashboard,
    // telemetri, dan kendali motor web tetap jalan; hanya stream yang mati.
    // v7.9.2: sertakan kode + nama error agar kegagalannya terdiagnosis.
    Serial.printf("PERINGATAN: kamera gagal init err=0x%x (%s) - firmware TETAP JALAN tanpa stream. Cek ribbon/konektor kamera board kalian!\n",
                  (unsigned)g_camErr, esp_err_to_name((esp_err_t)g_camErr));
  } else {
    Serial.printf("Kamera OK (JPEG SVGA 800x600) PID=0x%04x\n", (unsigned)g_camPid);
  }

  // Buffer AI dialokasikan sekali (fitur piksel + hasil dekode RGB frame
  // SVGA). v6.8.1: jalur alokasi diperkuat berlapis + diagnostik boot -
  // di v6.8 heap_caps_malloc(MALLOC_CAP_SPIRAM) menghasilkan pointer
  // kosong di board user ("[AI] buffer tidak siap") walau kamera yang
  // juga memakai PSRAM berjalan normal.
  // v7.0.3: angka PSRAM diambil dari heap_caps SPIRAM. ESP.getPsramSize()/
  // getFreePsram() terbukti mencetak 0 di konfigurasi ini padahal heap
  // PSRAM nyata bekerja (buffer AI beralamat 0x3d80xxxx) - menyesatkan.
  Serial.printf("PSRAM: total=%u free=%u\n",
                (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  aiFeatures = (float*)ps_malloc(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE * sizeof(float));
  if (!aiFeatures) aiFeatures = (float*)malloc(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE * sizeof(float));
  aiRgb = (uint8_t*)ps_malloc(800UL * 600UL * 3UL);
  if (!aiRgb) aiRgb = (uint8_t*)heap_caps_malloc(800UL * 600UL * 3UL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!aiRgb) aiRgb = (uint8_t*)malloc(800UL * 600UL * 3UL);
  // v7.6.3: aiThumb kini berlapis juga. Di v7.6.0-v7.6.2 ia hanya
  // ps_malloc satu jalur TANPA fallback - dan Langkah 1 di board
  // user menjawab err "buffer" terus (laporan 7 Nov 08:28): satu
  // pointer null di antara aiThumb/aiRgb, tanpa jejak pointer mana.
  // Kini ketiga buffer berlapis + ketiganya dicetak di log boot.
  aiThumb = (uint8_t*)ps_malloc(AI_THUMB_BYTES);
  if (!aiThumb) aiThumb = (uint8_t*)heap_caps_malloc(AI_THUMB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!aiThumb) aiThumb = (uint8_t*)malloc(AI_THUMB_BYTES);
  Serial.printf("AI kandidat korosi: %s (int8 96x96, ambang karat %.2f) | fitur=%p rgb=%p thumb=%p freePsram=%u\n",
                (aiFeatures && aiRgb) ? "SIAP" : "NONAKTIF",
                AI_RUST_THRESHOLD, (void*)aiFeatures, (void*)aiRgb, (void*)aiThumb,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  // Mutex SD + semaphore & task inferensi (v6.9.7): dibuat SESUDAH
  // inisialisasi LittleFS/AI di atas dan SEBELUM probe SD pertama &
  // startWiFi() - blok stack task 32 KB contiguous paling mudah
  // didapat selagi heap masih lapang. g_sdMutex WAJIB sudah ada
  // sebelum sdSessionBegin() pertama (probe SD tepat di bawah ini).
  // v7.7.5: g_sdMutex SUDAH dibuat sebelum initCamera() (probe awal).
  g_aiStartSem = xSemaphoreCreateBinary();
  g_aiDoneSem = xSemaphoreCreateBinary();
  // v7.7.3: task AI DIPIN ke core 1. Bukti board boot u1121
  // (7 Nov 13:07): inferensi thumbnail mati oleh TASK WATCHDOG -
  // "task_wdt: - IDLE0 (CPU 0)" + "CPU 0: ai" lalu abort. run_classifier
  // adalah kerja CPU murni +/-6,5 dtk tanpa yield; task "ai" yang
  // dibuat TANPA pin bisa mendarat di core 0, dan selama ia berlari
  // IDLE0 (yang terdaftar di task watchdog) tidak pernah kebagian
  // jalan -> watchdog meng-abort board di tengah inferensi.
  // disableLoopWDT() di aiInferCore hanya menenangkan loopTask,
  // bukan langganan IDLE. Di core 1 (core loopTask Arduino) inferensi
  // tidak menyentuh IDLE0 sama sekali - konfigurasi yang terbukti
  // lolos di semua uji berhasil sebelumnya (mis. boot #1086).
  if (xTaskCreatePinnedToCore(aiTaskFn, "ai", 32768, NULL, 1, NULL, 1) == pdPASS) {
    Serial.println("[AI] task inferensi siap (stack 32 KB, core 1)");
  } else {
    Serial.println("[AI] task inferensi GAGAL dibuat");
  }
  // v7.7.5: probe SD SUDAH dilakukan sebelum initCamera() (probe awal,
  // momen rel paling sepi). Di titik ini tidak ada probe ulang.

  // v7.10.0: initMPU() dibuang - gyro dihapus user dari robot.
  initMotors();
  motorBootSelfTest();   // v7.4.1: uji gerak kiri/kanan tanpa HP, sebelum WiFi

  loadWifiConfig();   // v7.3.2: kredensial STA tersimpan (bila ada) sebelum WiFi mulai
  startWiFi();
  Serial.printf("[RADIO] WiFi selesai | heap internal: %u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  server.on("/", handleRoot);
  server.on("/stream", handleStream);
  server.on("/live", handleLive);
  server.on("/capture", handleCapture);
  server.on("/cam-diag", handleCamDiag);  // v7.9.2
  server.on("/adminin", handleAdminin);   // v7.10.0: override klasifikasi turbidity (hanya via alamat ini)
  server.on("/telemetry", handleTelemetry);
  server.on("/wifi", handleWifiStatus);
  server.on("/wifi-set", HTTP_POST, handleWifiSet);
  server.on("/history", handleHistory);
  server.on("/download", handleDownload);
  server.on("/motor", handleMotor);
  server.on("/motor-test", handleMotorTest);  // v7.8.2
  server.on("/analyze", handleAnalyze);
  server.on("/capture-save", handleCaptureSave);
  server.on("/photos", handlePhotos);
  server.on("/log", handleLog);          // v7.8.0
  server.on("/time-set", handleTimeSet);  // v7.8.0
  server.on("/photo-get", handlePhotoGet);
  server.on("/analyze-photo", handleAnalyzePhoto);
  server.on("/photo-del", handlePhotoDel);
  server.on("/download-ai", handleDownloadAi);
  server.on("/ai-auto", handleAiAuto);
  server.on("/files", handleFiles);
  server.on("/files-json", handleFilesJson);
  server.on("/file-get", handleFileGet);
  server.on("/file-mkdir", HTTP_POST, handleFileMkdir);
  server.on("/file-upload", HTTP_POST, handleFileUploadDone, handleFileUpload);
  server.on("/file-del", handleFileDel);
  server.begin();
  logEvent(g_sdReady ? "Boot selesai - SD siap" : "Boot selesai - SD belum siap");  // v7.8.0
  Serial.println("HTTP: /  /stream  /capture  /telemetry  /history  /download  /analyze  /download-ai  /ai-auto  /files");
  Serial.printf("Early warning: turb>=keruh, TDS>=%.0f ppm, suhu>[%.0f]/<[%.0f] C\n",
                TDS_ALERT_PPM, TEMP_HIGH_ALERT, TEMP_LOW_ALERT);
  // v7.3.9: BIP BOOT 2x - tanda robot selesai boot sekaligus uji
  // jalur buzzer (GPIO21) di setiap penyalaan. Dipicu paling akhir
  // setup; garis waktunya dijalankan pollBuzzer() dari loop.
  if (BUZZER_PIN >= 0) { g_bootBeepActive = true; g_bootBeepStart = millis(); }
  Serial.println("[BUZZ] bip boot 2x - robot selesai boot");
  g_sdStickyEnabled = true;  // v7.7.0: mulai di sini sesi handler lengket
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  // Watchdog WiFi ringan (v6.9.3): tiap 15 detik, bila mode STA dan
  // sambungan putus, minta sambung ulang. Mode AP fallback TIDAK
  // diganggu sama sekali (AP sedang melayani dashboard darurat).
  static unsigned long lastWifiCheckMs = 0;
  if (now - lastWifiCheckMs >= 15000) {
    lastWifiCheckMs = now;
    if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] sambungan putus - menyambung ulang");
      WiFi.reconnect();
    }
  }
  pollStaRest(now);
  pollSdReprobe(now);  // v7.7.5
  if (WiFi.status() == WL_CONNECTED) ntpStartOnce();  // v7.8.0
  pollClock();  // v7.8.0  // v7.7.1: mesin keadaan STA istirahat (AP stabil)
  pollTemperature(now);
  pollCharge(now);
  pollBuzzer(now);
  pollMotors(now);
  if (now - g_lastTelemetry >= TELEMETRY_INTERVAL_MS) {
    g_lastTelemetry = now;
    readSensors();
    readTDS();
    // v7.2.8: WATCHDOG SESI SD. Satu sesi sehat selesai dalam
    // hitungan detik (mount + tulis foto). Bila sesi berjalan
    // > 20 detik, sesi itu macet (kartu sekarat menggantung di
    // dalam panggilan SD) dan selama itu motor mati total oleh
    // interlock - tanpa batas waktu dan tanpa bukti di versi
    // lama. Di sini pin motor dipulihkan paksa persis seperti
    // restorasi sdSessionEnd (SD di-end, LEDC dipasang lagi,
    // duty 0, driver dibangunkan) dan sesi dinyatakan gagal.
    // Mutex SENGAJA tidak dilepas di sini: ia tetap milik task
    // pemilik sesi; bila task itu akhirnya kembali, sdSessionEnd
    // miliknya yang melepaskannya satu kali seperti biasa. Sesi
    // SD berikutnya menunggu mutex itu, tetapi MOTOR HIDUP LAGI.
    if (g_sdSession && !g_sdSticky && g_sdSessionStartMs &&
        (millis() - g_sdSessionStartMs > 20000)) {
      Serial.println("[MOTOR] sesi SD macet >20 dtk - pin motor dipulihkan paksa, sesi dianggap gagal");
#if SD_PINS_KNOWN
      if (g_sdMounted) { SD_MMC.end(); g_sdMounted = false; }
#endif
      if (g_ledcAttached) {
        const int pins[4] = { MOTOR_L_IN1, MOTOR_L_IN2, MOTOR_R_IN1, MOTOR_R_IN2 };
        motorPwmAttach();  // v7.6.1: pasang lagi lewat jalur IDF yang sama
        motorForceStopNow();
        motorWake();
      }
      g_sdReady = false;
      g_sdSession = false;
      g_sdSessionStartMs = 0;
    }
    g_wqi = computeWQI();
    g_wqiCat = (g_wqi < 0) ? "-" : (g_wqi >= 80 ? "BAIK" : (g_wqi >= 50 ? "SEDANG" : "BURUK"));
    evaluateAlert();
    Serial.printf("[T] %.1f C | turb %.2f V raw %d mv %d dig %d pct %d (%s) | TDS %.0f ppm | WQI %d (%s) | leak %s (raw %d) | hujan %s (DO %d) | ALERT %s | log %d | motor %d/%d | chg_raw %d chg %d\n",
                  g_tempC, g_turbV, g_turbRaw, g_turbMv, g_turbDig, g_turbPct, g_turbLevel.c_str(), g_tdsPpm,
                  g_wqi, g_wqiCat.c_str(),
                  g_leak ? "YA!" : "tidak", g_leakRaw,
                  g_rain ? "YA" : "tidak", g_rainRaw, g_alert ? "YA!" : "-",
                  ramCount, g_curL, g_curR,
                  digitalRead(CHG_PIN) == LOW ? 1 : 0, g_chgCharging ? 1 : 0);
  }
  // Mode otomatis (computer vision): analisis berkala tanpa tombol.
  // v6.9.7: inferensi berjalan di task AI khusus (stack 32 KB) - loop
  // TIDAK lagi terblokir 1-3 detik; baris ini hanya menyerahkan
  // permintaan. Dinamo tetap berhenti selama penyimpanan foto
  // kandidat (driver ditidurkan sesi SD di dalam task AI).
  if (g_aiAuto && !g_aiAutoHeld && millis() - g_lastAiAuto >= AI_AUTO_INTERVAL_MS) {
    g_lastAiAuto = millis();
    sdSessionForceEnd();  // v7.7.0: serah-terima sesi ke task AI
    requestAiAnalysis(true, false);
  }
  logTick();
}
