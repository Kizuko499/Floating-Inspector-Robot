package id.fir.robot;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.Uri;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * FIR Robot - aplikasi pendamping robot FIR (ESP32-S3).
 *
 * Transportasi: WiFi saja (HTTP ke firmware): /telemetry, /capture,
 * /motor, /analyze, /ai-auto, dan pengelola berkas SD.
 *
 * Riwayat versi: v1.6 menghapus jalur Bluetooth (BLE) sepenuhnya atas
 * keputusan pengguna — aplikasi kini WiFi-only. Versi sebelumnya
 * (v1.0–v1.5) sempat memakai BLE sebagai jalur cadangan telemetri +
 * kendali saat WiFi putus.
 *
 * v1.8 (firmware tetap v7.3.4): layar "Video Live" di sidebar —
 * memutar stream MJPEG robot (GET /stream) apa adanya di
 * ImageView bawaan (parser penanda SOI/EOI JPEG), lengkap
 * dengan cacah frame + taksiran fps. Selama video jalan,
 * polling telemetri + foto berkala DIJEDA: server web robot
 * satu antrean, stream yang mengalir terus akan menahan
 * permintaan lain. Keluar dari layar video -> stream diputus,
 * semua polling lanjut seperti biasa.
 *
 * v1.7 (pendamping firmware v7.3.2): tab sidebar "WiFi Robot" —
 * melihat robot sedang terhubung ke hotspot mana (mode, SSID,
 * IP, RSSI dari GET /wifi) dan mengganti hotspot tujuan STA
 * (nama + kata sandi, POST /wifi-set; robot menyimpan lalu
 * restart; reset ke bawaan tersedia). Ditambah indikator
 * baterai di bilah atas dari field telemetri battery_pct —
 * selama firmware melaporkan -1 (jalur ukur baterai belum
 * terpasang di robot) tampil "belum terukur", bukan angka palsu.
 */
public class MainActivity extends Activity {

    // ---------------- Warna (samakan dashboard web) ----------------
    private static final int C_BG     = 0xFF0A0F22; // v2.3: navy web
    private static final int C_CARD   = 0xFF151D38; // v2.3: kartu navy
    private static final int C_TITLE  = 0xFF0A0F22;
    private static final int C_TEAL   = 0xFF5468D4; // v2.3: indigo web
    private static final int C_ORANGE = 0xFFB8480F;
    private static final int C_TEXT   = 0xFFE7ECFA; // v2.3
    private static final int C_MUTED  = 0xFFA9B6DE; // v2.3
    private static final int C_RED    = 0xFFFF7060; // v2.3
    private static final int C_ALERT_BG = 0xFFF9DEDC;
    private static final int C_ALERT_TX = 0xFF7A1F14;
    private static final int C_GREEN  = 0xFF66BB6A; // v2.3

    private enum Transport { NONE, WIFI }

    private volatile Transport transport = Transport.NONE;
    private volatile boolean manualDisconnect = false;
    private volatile boolean activityAlive = true;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService net = Executors.newFixedThreadPool(3);
    // Jalur motor WiFi khusus (v1.4): antrean tunggal terpisah dari pool
    // umum agar perintah tekan-tahan tidak tertahan di belakang polling.
    private final ExecutorService motorNet = Executors.newSingleThreadExecutor();
    // Generasi tahan (v1.4; diubah v1.9): dinaikkan HANYA tiap
    // startHold. Pengirim motor membuang perintah jalan yang masih
    // antre bila tahan BARU sudah dimulai (maksud tombol sudah
    // pindah), kecuali "S" yang selalu dikirim. v1.4 menaikkan
    // generasi juga di stopHold - akibatnya perintah jalan yang
    // masih antre saat jari diangkat ikut terbuang, dan ketukan
    // pendek tidak pernah menggerakkan motor sama sekali (di log
    // board hanya "S" yang tiba). Watchdog motor 1,5 dtk di
    // firmware tetap menjadi batas aman perintah yang terlambat.
    private volatile int holdGen = 0;
    private volatile long holdStartMs = 0;  // v1.9: kapan tahan ini dimulai
    // true bila ada kirim motor WiFi yang sukses selama tahan berjalan.
    private volatile boolean holdSentOk = false;

    // ---------------- View ----------------
    private TextView chipStatus;
    private EditText editIp;
    private Button btnWifi, btnDisc;
    private TextView txtConnError;
    private TextView bannerAlert;
    private TextView txtWqi, txtWqiCat, txtWqiInfo;
    private TextView valSuhu, valTurb, valTurbLvl, valTds, valLeak, valRain, valTilt;
    private ImageView imgCam;
    private TextView txtCamNote;
    private Button btnAnalyze;
    private Button btnCapSave, btnAnalyzePhoto;
    private TextView txtStep;
    private String lastPhotoName = null;
    private LinearLayout cardRiwayat, riwayatList;
    private ImageView imgRiwayat;
    private ImageView imgAiPrev;   // v2.5: preview foto Langkah 1 di kartu Deteksi AI
    private TextView txtRiwayatStatus;
    private Switch switchAuto;
    private TextView txtAiResult, txtAiDetail;
    private SeekBar seekSpeed;
    private TextView txtSpeed;
    private TextView motNote;
    private final List<View> actionViews = new ArrayList<>();
    private boolean syncingSwitch = false;

    // ---------------- Sidebar & layar berkas SD (v1.1) ----------------
    private ScrollView mainScroll;
    private LinearLayout cardWqi, cardSensor, cardCam, cardAi, cardMotor;
    private LinearLayout cardConn, cardTren;   // v2.3: section ala web
    private TextView txtTren, txtSideBatt;     // v2.3
    // cardRiwayat dideklarasikan di kelompok field AI (v2.1)
    private View scrimView;
    private LinearLayout sidebarView;
    private boolean sidebarOpen = false;
    private LinearLayout filesScreen;
    private TextView filesCrumb, filesMsg;
    private LinearLayout filesList;
    private Button btnFilesNew, btnFilesUpload, btnFilesRetry;
    private String filesDir = "/";
    private boolean filesVisible = false;

    // ---------------- Layar WiFi Robot (v1.7) ----------------
    private LinearLayout wifiScreen;
    private boolean wifiVisible = false;
    private TextView wifiStatusTxt, wifiMsg;
    private EditText editSsid, editWifiPass;
    private Button btnWifiSave, btnWifiResetBtn, btnWifiRefresh;

    // ---------------- Indikator baterai (v1.7) ----------------
    private TextView txtBattery;

    // ---------------- Layar Video Live (v1.8) ----------------
    private LinearLayout videoScreen;
    private boolean videoVisible = false;
    private ImageView imgVideo;
    private TextView videoStatusTxt, videoMsg;
    private volatile boolean videoActive = false;
    private Thread videoThread;
    private volatile HttpURLConnection videoConn;
    private Bitmap lastVideoBitmap;
    private volatile int videoFrames = 0;
    private volatile long videoStartMs = 0;

    private static class SdEntry {
        String name;
        boolean dir;
        long size;
    }

    // ---------------- WiFi ----------------
    private int wifiFailCount = 0;
    private boolean cameraBusy = false;

    // ---------------- Pengikatan jaringan WiFi (v1.2) ----------------
    // AP robot tidak punya internet -> Android/HyperOS mengalihkan lalu
    // lintas aplikasi ke data seluler sehingga 192.168.4.1 tak tercapai.
    // Obatnya: semua koneksi HTTP dibuka eksplisit lewat Network WiFi.
    private ConnectivityManager connMgr;
    private volatile Network wifiNetwork;
    private volatile String lastWifiError = "";

    private static final int REQ_PICK_FILE = 44;

    // ============================ SIKLUS HIDUP ============================

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
        setTransport(Transport.NONE);
        ui.postDelayed(pollRunnable, 600);
        ui.postDelayed(cameraRunnable, 1500);
        // Sambung otomatis: coba WiFi (v1.6: satu-satunya jalur).
        tryWifiAuto();
    }

    @Override
    protected void onDestroy() {
        activityAlive = false;
        stopVideo();
        ui.removeCallbacksAndMessages(null);
        net.shutdownNow();
        motorNet.shutdownNow();
        super.onDestroy();
    }

    // ============================ PEMBANGUNAN UI ============================

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private GradientDrawable rounded(int color, int radiusDp) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(radiusDp));
        return g;
    }

    private TextView tv(String text, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        return t;
    }

    private LinearLayout card(LinearLayout parent, String title) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(rounded(C_CARD, 14));
        c.setPadding(dp(14), dp(12), dp(14), dp(14));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, 0, 0, dp(12));
        c.setLayoutParams(lp);
        if (title != null) {
            TextView h = tv(title, 13, C_TEAL, true);
            h.setPadding(0, 0, 0, dp(8));
            c.addView(h);
        }
        parent.addView(c);
        return c;
    }

    private Button btn(String label, int bg) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextColor(Color.WHITE);
        b.setTextSize(13);
        b.setAllCaps(false);
        b.setBackground(rounded(bg, 10));
        return b;
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(C_BG);

        // --- Bilah atas: hamburger + judul + chip status (selalu terlihat) ---
        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.VERTICAL);
        top.setBackgroundColor(C_TITLE);
        top.setPadding(dp(16), dp(12), dp(16), dp(12));
        LinearLayout topRow = new LinearLayout(this);
        topRow.setOrientation(LinearLayout.HORIZONTAL);
        topRow.setGravity(Gravity.CENTER_VERTICAL);
        TextView btnMenu = tv("\u2630", 24, Color.WHITE, true);
        btnMenu.setPadding(dp(8), dp(4), dp(14), dp(4));
        btnMenu.setOnClickListener(v -> toggleSidebar());
        topRow.addView(btnMenu);
        LinearLayout titleCol = new LinearLayout(this);
        titleCol.setOrientation(LinearLayout.VERTICAL);
        titleCol.setLayoutParams(new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView appTitle = tv("FIR", 17, Color.WHITE, true);
        titleCol.addView(appTitle);
        TextView appSub = tv("Floating Inspector Robot", 11, 0xFFBFD9D6, false);
        titleCol.addView(appSub);
        topRow.addView(titleCol);
        top.addView(topRow);
        chipStatus = new TextView(this);
        chipStatus.setTextSize(13);
        chipStatus.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        chipStatus.setTextColor(Color.WHITE);
        chipStatus.setPadding(dp(12), dp(7), dp(12), dp(7));
        LinearLayout.LayoutParams chipLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        chipLp.setMargins(0, dp(10), 0, 0);
        chipStatus.setLayoutParams(chipLp);
        top.addView(chipStatus);
        // Indikator baterai robot (v1.7): diisi polling telemetri.
        txtBattery = new TextView(this);
        txtBattery.setText("\uD83D\uDD0B Baterai: \u2014");
        txtBattery.setTextSize(12);
        txtBattery.setTextColor(0xFFBFD9D6);
        txtBattery.setPadding(dp(12), dp(5), dp(12), 0);
        top.addView(txtBattery);
        root.addView(top);

        // --- Bingkai konten: layar utama + layar berkas + sidebar di atasnya ---
        // Bilah atas (berisi chip status) berada DI LUAR bingkai ini, jadi
        // sidebar tidak pernah menghalangi chip status.
        FrameLayout contentFrame = new FrameLayout(this);
        contentFrame.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        root.addView(contentFrame);

        // --- Badan bergulir ---
        mainScroll = new ScrollView(this);
        mainScroll.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(dp(12), dp(12), dp(12), dp(12));
        mainScroll.addView(body);
        contentFrame.addView(mainScroll);

        // Kartu sambungan
        cardConn = card(body, "SAMBUNGAN");
        TextView lblIp = tv("Alamat IP robot (mode AP: 192.168.4.1)", 12, C_MUTED, false);
        cardConn.addView(lblIp);
        editIp = new EditText(this);
        editIp.setText("192.168.4.1");
        editIp.setTextSize(15);
        editIp.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        editIp.setSingleLine(true);
        cardConn.addView(editIp);
        LinearLayout rowBtn = new LinearLayout(this);
        rowBtn.setOrientation(LinearLayout.HORIZONTAL);
        rowBtn.setPadding(0, dp(6), 0, 0);
        btnWifi = btn("Coba WiFi", C_TEAL);
        btnDisc = btn("Putuskan", C_ORANGE);
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        blp.setMargins(0, 0, dp(6), 0);
        btnWifi.setLayoutParams(blp);
        LinearLayout.LayoutParams blp2 = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        blp2.setMargins(dp(6), 0, 0, 0);
        btnDisc.setLayoutParams(blp2);
        rowBtn.addView(btnWifi);
        rowBtn.addView(btnDisc);
        cardConn.addView(rowBtn);
        // Alasan galat sambungan terakhir (v1.2); kosong bila tidak ada galat.
        txtConnError = tv("", 12, C_RED, false);
        LinearLayout.LayoutParams elp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        elp.setMargins(0, dp(6), 0, 0);
        txtConnError.setLayoutParams(elp);
        cardConn.addView(txtConnError);
        btnWifi.setOnClickListener(v -> {
            manualDisconnect = false;
            connectWifi(true);
        });
        btnDisc.setOnClickListener(v -> {
            manualDisconnect = true;
            disconnectAll();
            toast("Koneksi diputuskan.");
        });

        // Banner peringatan dini
        bannerAlert = tv("", 14, C_ALERT_TX, true);
        bannerAlert.setBackground(rounded(C_ALERT_BG, 12));
        bannerAlert.setPadding(dp(14), dp(11), dp(14), dp(11));
        LinearLayout.LayoutParams alp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        alp.setMargins(0, 0, 0, dp(12));
        bannerAlert.setLayoutParams(alp);
        bannerAlert.setVisibility(View.GONE);
        body.addView(bannerAlert);

        // Kartu WQI
        cardWqi = card(body, "INDEKS KUALITAS AIR (WQI)");
        LinearLayout wqiRow = new LinearLayout(this);
        wqiRow.setOrientation(LinearLayout.HORIZONTAL);
        wqiRow.setGravity(Gravity.CENTER_VERTICAL);
        txtWqi = tv("-", 44, C_TEXT, true);
        wqiRow.addView(txtWqi);
        TextView per100 = tv(" / 100", 15, C_MUTED, false);
        wqiRow.addView(per100);
        txtWqiCat = tv("-", 13, Color.WHITE, true);
        txtWqiCat.setPadding(dp(10), dp(4), dp(10), dp(4));
        LinearLayout.LayoutParams catLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        catLp.setMargins(dp(12), 0, 0, 0);
        txtWqiCat.setLayoutParams(catLp);
        wqiRow.addView(txtWqiCat);
        cardWqi.addView(wqiRow);
        txtWqiInfo = tv("", 11, C_MUTED, false);
        txtWqiInfo.setPadding(0, dp(6), 0, 0);
        cardWqi.addView(txtWqiInfo);

        // Kartu sensor (grid 2 kolom x 3 baris)
        cardSensor = card(body, "SENSOR");
        buildSensorGrid(cardSensor);

        // Kartu kamera
        cardCam = card(body, "KAMERA ROBOT");
        imgCam = new ImageView(this);
        imgCam.setScaleType(ImageView.ScaleType.FIT_CENTER);
        imgCam.setAdjustViewBounds(true);
        imgCam.setBackgroundColor(0xFF000000);
        LinearLayout.LayoutParams imgLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(220));
        imgCam.setLayoutParams(imgLp);
        cardCam.addView(imgCam);
        txtCamNote = tv("Kamera hanya tersedia lewat WiFi.", 12, C_MUTED, false);
        txtCamNote.setPadding(0, dp(6), 0, 0);
        cardCam.addView(txtCamNote);

        // Kartu AI
        cardAi = card(body, "DETEKSI AI - KANDIDAT KOROSI");
        btnAnalyze = btn("Analisis Langsung (tanpa simpan)", C_TEAL);
        btnAnalyze.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        btnCapSave = btn("1. Ambil Foto & Simpan", C_TEAL);
        cardAi.addView(btnCapSave);
        actionViews.add(btnCapSave);
        btnAnalyzePhoto = btn("2. Analisis Foto Terakhir", C_TEAL);
        LinearLayout.LayoutParams stepLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        stepLp.setMargins(0, dp(8), 0, 0);
        btnAnalyzePhoto.setLayoutParams(stepLp);
        cardAi.addView(btnAnalyzePhoto);
        actionViews.add(btnAnalyzePhoto);
        txtStep = tv("Alur bertahap: ambil foto dulu (langkah 1), analisis menyusul "
                + "(langkah 2). Foto tersimpan di robot dan muncul di Riwayat Foto.",
                12, C_MUTED, false);
        txtStep.setPadding(0, dp(6), 0, dp(10));
        cardAi.addView(txtStep);
        // Preview foto terakhir yang disimpan (v2.5) - tampil sesudah
        // Langkah 1 berhasil, supaya pengguna melihat hasil jepretan
        // sebelum menganalisis (meniru perilaku web).
        imgAiPrev = new ImageView(this);
        LinearLayout.LayoutParams aiPrevLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(220));
        aiPrevLp.setMargins(0, dp(8), 0, 0);
        imgAiPrev.setLayoutParams(aiPrevLp);
        imgAiPrev.setScaleType(ImageView.ScaleType.CENTER_CROP);
        imgAiPrev.setBackgroundColor(0xFF000000);
        imgAiPrev.setVisibility(View.GONE);
        cardAi.addView(imgAiPrev);
        cardAi.addView(btnAnalyze);
        actionViews.add(btnAnalyze);
        LinearLayout autoRow = new LinearLayout(this);
        autoRow.setOrientation(LinearLayout.HORIZONTAL);
        autoRow.setGravity(Gravity.CENTER_VERTICAL);
        autoRow.setPadding(0, dp(10), 0, 0);
        TextView lblAuto = tv("Mode Otomatis (analisis berkala)", 13, C_TEXT, false);
        lblAuto.setLayoutParams(new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        autoRow.addView(lblAuto);
        switchAuto = new Switch(this);
        autoRow.addView(switchAuto);
        cardAi.addView(autoRow);
        actionViews.add(switchAuto);
        txtAiResult = tv("Belum ada hasil analisis.", 15, C_TEXT, true);
        txtAiResult.setPadding(0, dp(10), 0, 0);
        cardAi.addView(txtAiResult);
        txtAiDetail = tv("", 12, C_MUTED, false);
        cardAi.addView(txtAiDetail);
        btnAnalyze.setOnClickListener(v -> doAnalyze());
        btnCapSave.setOnClickListener(v -> doCaptureSave());
        btnAnalyzePhoto.setOnClickListener(v -> {
            if (lastPhotoName == null) {
                txtAiResult.setText("Ambil foto dulu (Langkah 1), atau pilih Analisis di Riwayat Foto.");
            } else {
                doAnalyzePhoto(lastPhotoName);
            }
        });
        switchAuto.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (syncingSwitch) return;
            doAiAuto(isChecked);
        });

        // Kartu Riwayat Foto (v2.1, pendamping firmware v7.6.0)
        cardRiwayat = card(body, "RIWAYAT FOTO");
        imgRiwayat = new ImageView(this);
        LinearLayout.LayoutParams riwImgLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(220));
        imgRiwayat.setLayoutParams(riwImgLp);
        imgRiwayat.setScaleType(ImageView.ScaleType.CENTER_CROP);
        imgRiwayat.setVisibility(View.GONE);
        cardRiwayat.addView(imgRiwayat);
        Button btnReloadRiwayat = btn("Muat Riwayat Foto", C_TEAL);
        cardRiwayat.addView(btnReloadRiwayat);
        actionViews.add(btnReloadRiwayat);
        txtRiwayatStatus = tv("Foto yang diambil dari Deteksi AI tersimpan di robot, "
                + "lengkap dengan hasil analisisnya.", 12, C_MUTED, false);
        txtRiwayatStatus.setPadding(0, dp(6), 0, 0);
        cardRiwayat.addView(txtRiwayatStatus);
        riwayatList = new LinearLayout(this);
        riwayatList.setOrientation(LinearLayout.VERTICAL);
        cardRiwayat.addView(riwayatList);
        btnReloadRiwayat.setOnClickListener(v -> loadRiwayat());

        // Kartu motor
        cardMotor = card(body, "KENDALI MOTOR");
        LinearLayout spdRow = new LinearLayout(this);
        spdRow.setOrientation(LinearLayout.HORIZONTAL);
        spdRow.setGravity(Gravity.CENTER_VERTICAL);
        TextView lblSpd = tv("Kecepatan", 13, C_TEXT, false);
        spdRow.addView(lblSpd);
        seekSpeed = new SeekBar(this);
        seekSpeed.setMax(70);           // 30..100 persen
        seekSpeed.setProgress(40);      // bawaan 70%
        seekSpeed.setLayoutParams(new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        spdRow.addView(seekSpeed);
        txtSpeed = tv("70%", 13, C_TEXT, true);
        spdRow.addView(txtSpeed);
        cardMotor.addView(spdRow);
        actionViews.add(seekSpeed);
        seekSpeed.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int p, boolean fromUser) {
                txtSpeed.setText((p + 30) + "%");
            }
            @Override public void onStartTrackingTouch(SeekBar sb) { }
            @Override public void onStopTrackingTouch(SeekBar sb) { }
        });
        // Pad arah: baris Maju / (Kiri, Stop, Kanan) / Mundur
        cardMotor.addView(motorRow(new String[]{"F"}, new String[]{"\u25B2 Maju"}));
        cardMotor.addView(motorRow(new String[]{"L", "S", "R"},
                new String[]{"\u25C0 Kiri", "STOP", "Kanan \u25B6"}));
        cardMotor.addView(motorRow(new String[]{"B"}, new String[]{"\u25BC Mundur"}));
        motNote = tv("Tekan-tahan tombol arah untuk bergerak; lepas untuk berhenti.",
                11, C_MUTED, false);
        motNote.setPadding(0, dp(8), 0, 0);
        cardMotor.addView(motNote);

        // Kartu Tren & Log (v2.3, samakan web: grup Lainnya)
        cardTren = card(body, "TREN & LOG");
        txtTren = tv("Riwayat telemetri sesi ini dari robot. Tekan Muat Tren.",
                12, C_MUTED, false);
        cardTren.addView(txtTren);
        Button btnTren = btn("Muat Tren", C_TEAL);
        LinearLayout.LayoutParams trenLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        trenLp.setMargins(0, dp(8), 0, 0);
        btnTren.setLayoutParams(trenLp);
        cardTren.addView(btnTren);
        actionViews.add(btnTren);
        btnTren.setOnClickListener(v -> loadTren());

        buildFilesScreen(contentFrame);
        buildWifiScreen(contentFrame);
        buildVideoScreen(contentFrame);
        buildSidebar(contentFrame);

        // v2.3: mulai di section Ringkasan (navigasi per section ala web).
        cardCam.setVisibility(View.GONE);
        cardAi.setVisibility(View.GONE);
        cardRiwayat.setVisibility(View.GONE);
        cardSensor.setVisibility(View.GONE);
        cardMotor.setVisibility(View.GONE);
        cardTren.setVisibility(View.GONE);
        markNavActive("ringkasan");

        setContentView(root);
        // Parkir sidebar di luar tepi kiri sesudah tata letak pertama terukur.
        root.post(() -> {
            if (!sidebarOpen && sidebarView != null) {
                sidebarView.setTranslationX(-sidebarView.getWidth());
            }
        });
    }

    private TextView[] sensorValues = new TextView[6];

    private void buildSensorGrid(LinearLayout cSens) {
        String[] labels = {"SUHU AIR", "KEKERUHAN", "TDS", "KEBOCORAN", "HUJAN", "KEMIRINGAN"};
        for (int r = 0; r < 3; r++) {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setPadding(0, dp(4), 0, dp(4));
            for (int c = 0; c < 2; c++) {
                int idx = r * 2 + c;
                LinearLayout cell = new LinearLayout(this);
                cell.setOrientation(LinearLayout.VERTICAL);
                cell.setLayoutParams(new LinearLayout.LayoutParams(0,
                        ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
                cell.setPadding(dp(4), 0, dp(4), 0);
                TextView lab = tv(labels[idx], 10, C_TEAL, true);
                cell.addView(lab);
                TextView val = tv("-", 17, C_TEXT, true);
                cell.addView(val);
                sensorValues[idx] = val;
                row.addView(cell);
            }
            cSens.addView(row);
        }
        valSuhu = sensorValues[0];
        valTurb = sensorValues[1];
        valTds = sensorValues[2];
        valLeak = sensorValues[3];
        valRain = sensorValues[4];
        valTilt = sensorValues[5];
        // Baris klasifikasi kekeruhan di bawah grid
        valTurbLvl = tv("", 12, C_MUTED, false);
        valTurbLvl.setPadding(dp(4), dp(2), 0, 0);
        cSens.addView(valTurbLvl);
    }

    private LinearLayout motorRow(String[] cmds, String[] labels) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, dp(4), 0, dp(4));
        for (int i = 0; i < cmds.length; i++) {
            final String cmd = cmds[i];
            Button b = btn(labels[i], "S".equals(cmd) ? C_ORANGE : C_TEAL);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(56), 1f);
            lp.setMargins(dp(4), 0, dp(4), 0);
            b.setLayoutParams(lp);
            b.setTextSize(15);
            actionViews.add(b);
            if ("S".equals(cmd)) {
                b.setOnClickListener(v -> sendMotor("S"));
            } else {
                b.setOnTouchListener((v, ev) -> {
                    if (transport == Transport.NONE) return false;
                    switch (ev.getActionMasked()) {
                        case MotionEvent.ACTION_DOWN:
                            // Cegah ScrollView mencuri gestur (v1.4): tanpa
                            // ini tahan langsung kena ACTION_CANCEL dan
                            // motor tidak pernah bergerak.
                            v.getParent().requestDisallowInterceptTouchEvent(true);
                            v.setPressed(true);
                            startHold(cmd);
                            return true;
                        case MotionEvent.ACTION_UP:
                        case MotionEvent.ACTION_CANCEL:
                            v.getParent().requestDisallowInterceptTouchEvent(false);
                            v.setPressed(false);
                            if (holdCmd != null) stopHold();
                            return true;
                        default:
                            return false;
                    }
                });
            }
            row.addView(b);
        }
        return row;
    }

    // ============================ TEKAN-TAHAN MOTOR ============================

    private String holdCmd = null;
    private final Runnable holdRepeater = new Runnable() {
        @Override public void run() {
            if (holdCmd != null && transport != Transport.NONE) {
                sendMotor(holdCmd);
                ui.postDelayed(this, 400);
            }
        }
    };

    private void startHold(String cmd) {
        holdGen++;
        holdStartMs = System.currentTimeMillis();
        holdSentOk = false;
        holdCmd = cmd;
        sendMotor(cmd);
        ui.postDelayed(holdRepeater, 400);
    }

    private void stopHold() {
        // v1.9: generasi TIDAK dinaikkan di sini lagi (lihat catatan
        // di deklarasi holdGen) - perintah jalan yang masih antre
        // biar terkirim; "S" yang antre tepat di belakangnya yang
        // menghentikan robot.
        String lastCmd = holdCmd;
        holdCmd = null;
        ui.removeCallbacks(holdRepeater);
        boolean sentOk = holdSentOk;
        Transport t = transport;
        long heldMs = System.currentTimeMillis() - holdStartMs;
        boolean tapPulse = (lastCmd != null && !sentOk && heldMs < 500);
        if (tapPulse) {
            // Ketukan pendek yang perintah jalannya belum sempat
            // terkirim: beri denyut jalan singkat - "S" ditunda
            // 600 ms, dan batal bila tahan baru sudah dimulai.
            final int genAtTap = holdGen;
            ui.postDelayed(() -> { if (genAtTap == holdGen) sendMotor("S"); }, 600);
        } else {
            sendMotor("S");
        }
        // Selama tahan tidak ada satu pun kirim yang sampai -> beri
        // tahu pengguna (v1.4). Sebelumnya kegagalan ditelan diam.
        // (v1.6: hanya jalur WiFi — jalur BLE sudah dihapus.)
        // (v1.9: kasus denyut ketukan di atas dikecualikan - perintah
        // jalannya memang sedang dikirim, bukan gagal.)
        if (t == Transport.WIFI && !sentOk && !tapPulse) {
            toast("Perintah motor tidak sampai ke robot \u2014 cek sambungan.");
        }
    }

    private int speedPct() {
        return seekSpeed == null ? 70 : seekSpeed.getProgress() + 30;
    }

    // ============================ STATUS & TRANSPORT ============================

    private void toast(String msg) {
        ui.post(() -> {
            if (activityAlive) Toast.makeText(this, msg, Toast.LENGTH_SHORT).show();
        });
    }

    /** Tulis alasan galat sambungan di kartu SAMBUNGAN (v1.2). */
    private void showConnError(String msg) {
        final String m = msg == null ? "" : msg;
        ui.post(() -> {
            if (activityAlive && txtConnError != null) txtConnError.setText(m);
        });
    }

    /** Ringkas pesan exception untuk ditampilkan (tanpa newline, maks ~120 char). */
    private static String shortReason(Exception e) {
        String m = e == null ? "" : e.getMessage();
        if (m == null || m.trim().isEmpty()) {
            m = e == null ? "galat tidak dikenal" : e.getClass().getSimpleName();
        }
        m = m.replace('\n', ' ').trim();
        return m.length() > 120 ? m.substring(0, 120) : m;
    }

    private void setTransport(Transport t) {
        transport = t;
        ui.post(() -> {
            if (!activityAlive || chipStatus == null) return;
            switch (t) {
                case WIFI:
                    chipStatus.setText("Terhubung: WiFi \u2014 " + ipText());
                    chipStatus.setBackground(rounded(C_TEAL, 20));
                    break;
                default:
                    chipStatus.setText("Tidak terhubung");
                    chipStatus.setBackground(rounded(C_ORANGE, 20));
                    break;
            }
            if (t == Transport.NONE && txtBattery != null) {
                txtBattery.setText("\uD83D\uDD0B Baterai: \u2014");
            }
            if (t == Transport.NONE && txtSideBatt != null) {
                txtSideBatt.setText("\uD83D\uDD0B Baterai: \u2014\nStatus cas: \u2014");
            }
            // Sambungan berhasil -> kosongkan alasan galat (v1.2).
            if (t != Transport.NONE && txtConnError != null) txtConnError.setText("");
            // Frame kamera basi (v1.5): di luar WiFi kosongkan gambar
            // agar sisa frame sesi WiFi tidak tampil membingungkan.
            if (t != Transport.WIFI && imgCam != null) imgCam.setImageBitmap(null);
            refreshActionStates();
            refreshCameraNote();
            refreshFilesGate();
        });
    }

    private void refreshActionStates() {
        boolean on = transport != Transport.NONE;
        for (View v : actionViews) {
            v.setEnabled(on);
            // Keadaan nonaktif harus terlihat (v1.4): tombol berlatar
            // kustom tampak sama saat mati, jadi redupkan saat nonaktif.
            v.setAlpha(on ? 1.0f : 0.45f);
        }
        if (motNote != null) {
            motNote.setText(on
                    ? "Tekan-tahan tombol arah untuk bergerak; lepas untuk berhenti."
                    : "Kendali nonaktif \u2014 sambungkan dulu ke robot.");
        }
    }

    private void refreshCameraNote() {
        if (txtCamNote == null) return;
        if (transport == Transport.WIFI) {
            txtCamNote.setText("Foto berkala dari robot (WiFi).");
        } else {
            txtCamNote.setText("Kamera: tidak terhubung.");
        }
    }

    private String ipText() {
        String s = editIp == null ? "192.168.4.1" : editIp.getText().toString().trim();
        return s.isEmpty() ? "192.168.4.1" : s;
    }

    private String baseUrl() {
        String s = ipText();
        if (!s.startsWith("http://") && !s.startsWith("https://")) s = "http://" + s;
        if (s.endsWith("/")) s = s.substring(0, s.length() - 1);
        return s;
    }

    private void disconnectAll() {
        stopHoldSilent();
        wifiFailCount = 0;
        setTransport(Transport.NONE);
    }

    private void stopHoldSilent() {
        holdCmd = null;
        ui.removeCallbacks(holdRepeater);
    }

    // ============================ HTTP (WiFi) ============================

    /**
     * Segarkan pilihan objek Network WiFi (v1.2). Dipanggil di awal setiap
     * operasi HTTP; AP robot tanpa internet membuat jaringan WiFi tidak
     * "tervalidasi", jadi tanpa pengikatan eksplisit Android mengirim
     * paket aplikasi lewat data seluler dan IP lokal robot tak tercapai.
     */
    private void refreshWifiNetwork() {
        try {
            if (connMgr == null) {
                connMgr = (ConnectivityManager)
                        getSystemService(Context.CONNECTIVITY_SERVICE);
            }
            Network found = null;
            if (connMgr != null) {
                for (Network n : connMgr.getAllNetworks()) {
                    NetworkCapabilities caps = connMgr.getNetworkCapabilities(n);
                    if (caps != null && caps.hasTransport(
                            NetworkCapabilities.TRANSPORT_WIFI)) {
                        found = n;
                        break;
                    }
                }
            }
            wifiNetwork = found;
        } catch (Exception ignored) {
            wifiNetwork = null;
        }
    }

    /** Buka koneksi URL — dipaksa lewat jaringan WiFi bila objeknya ada. */
    private URLConnection openConn(URL url) throws Exception {
        refreshWifiNetwork();
        Network n = wifiNetwork;
        if (n != null) return n.openConnection(url);
        return url.openConnection();
    }

    private String httpGet(String urlStr, int connectMs, int readMs) throws Exception {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) openConn(new URL(urlStr));
            c.setConnectTimeout(connectMs);
            c.setReadTimeout(readMs);
            c.setRequestMethod("GET");
            c.setRequestProperty("Cache-Control", "no-cache");
            int code = c.getResponseCode();
            InputStream in = (code >= 200 && code < 300) ? c.getInputStream() : c.getErrorStream();
            if (in == null) throw new Exception("HTTP " + code);
            BufferedReader r = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null) sb.append(line);
            r.close();
            if (code < 200 || code >= 300) throw new Exception("HTTP " + code + " " + sb);
            return sb.toString();
        } finally {
            if (c != null) c.disconnect();
        }
    }

    private byte[] httpGetBytes(String urlStr, int connectMs, int readMs) throws Exception {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) openConn(new URL(urlStr));
            c.setConnectTimeout(connectMs);
            c.setReadTimeout(readMs);
            c.setRequestMethod("GET");
            c.setRequestProperty("Cache-Control", "no-cache");
            int code = c.getResponseCode();
            if (code < 200 || code >= 300) throw new Exception("HTTP " + code);
            InputStream in = c.getInputStream();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            in.close();
            return out.toByteArray();
        } finally {
            if (c != null) c.disconnect();
        }
    }

    /** Sambung otomatis saat aplikasi dibuka: coba WiFi satu kali. */
    private void tryWifiAuto() {
        net.execute(() -> {
            if (fetchTelemetryWifi(1500, 2500)) {
                setTransport(Transport.WIFI);
            } else if (!manualDisconnect) {
                // Gagal: tetap tidak terhubung + alasan tertulis di
                // kartu SAMBUNGAN; pengguna menyambung lagi lewat
                // tombol Coba WiFi (v1.6: tidak ada fallback lain).
                showConnError("WiFi gagal: " + lastWifiError);
            }
        });
    }

    private void connectWifi(boolean announce) {
        net.execute(() -> {
            if (fetchTelemetryWifi(1500, 3000)) {
                wifiFailCount = 0;
                setTransport(Transport.WIFI);
                if (announce) toast("Terhubung lewat WiFi.");
            } else {
                String msg = "WiFi gagal: " + lastWifiError;
                showConnError(msg);
                toast(msg);
                setTransport(Transport.NONE);
            }
        });
    }

    /** Ambil /telemetry lewat WiFi. true bila berhasil dan JSON diterapkan. */
    private boolean fetchTelemetryWifi(int connectMs, int readMs) {
        try {
            String body = httpGet(baseUrl() + "/telemetry", connectMs, readMs);
            JSONObject o = new JSONObject(body);
            lastWifiError = "";
            ui.post(() -> applyTelemetry(o));
            return true;
        } catch (Exception e) {
            lastWifiError = shortReason(e);
            return false;
        }
    }

    // ============================ POLLING & KAMERA ============================

    private final Runnable pollRunnable = new Runnable() {
        @Override public void run() {
            if (!activityAlive) return;
            // Video live (v1.8): stream menahan server robot ->
            // polling telemetri dijeda total selama video jalan,
            // supaya kegagalan semu tidak memutus status koneksi.
            if (videoActive) {
                ui.postDelayed(this, 2000);
                return;
            }
            if (transport == Transport.WIFI) {
                net.execute(() -> {
                    if (fetchTelemetryWifi(1500, 2500)) {
                        wifiFailCount = 0;
                    } else if (++wifiFailCount >= 2 && !manualDisconnect) {
                        // WiFi putus dua kali berturut-turut -> tidak
                        // terhubung + alasan tertulis (v1.6: tidak ada
                        // fallback; Coba WiFi menyambung lagi).
                        wifiFailCount = 0;
                        showConnError("WiFi gagal: " + lastWifiError);
                        setTransport(Transport.NONE);
                        toast("WiFi terputus.");
                    }
                });
            }
            ui.postDelayed(this, 2000);
        }
    };

    private final Runnable cameraRunnable = new Runnable() {
        @Override public void run() {
            if (!activityAlive) return;
            if (transport == Transport.WIFI && !cameraBusy && !videoActive) {
                cameraBusy = true;
                net.execute(() -> {
                    try {
                        byte[] jpg = httpGetBytes(baseUrl() + "/capture?t="
                                + System.currentTimeMillis(), 2000, 5000);
                        final Bitmap bmp = BitmapFactory.decodeByteArray(jpg, 0, jpg.length);
                        if (bmp != null) {
                            ui.post(() -> {
                                if (activityAlive && transport == Transport.WIFI) {
                                    imgCam.setImageBitmap(bmp);
                                }
                            });
                        }
                    } catch (Exception ignored) {
                        // Frame gagal; coba lagi siklus berikutnya.
                    } finally {
                        cameraBusy = false;
                    }
                });
            }
            ui.postDelayed(this, 1200);
        }
    };

    // ============================ AKSI PENGGUNA ============================

    private void sendMotor(String cmd) {
        if (transport == Transport.WIFI) {
            final String url = baseUrl() + "/motor?cmd=" + cmd + "&speed=" + speedPct();
            final int gen = holdGen;
            final boolean isStop = "S".equals(cmd);
            motorNet.execute(() -> {
                // Buang perintah basi dari tahan lama bila tahan
                // BARU sudah dimulai (v1.9), kecuali "S" yang selalu
                // dikirim agar robot pasti berhenti.
                if (!isStop && gen != holdGen) return;
                try {
                    httpGet(url, 1200, 2000);
                    holdSentOk = true;
                } catch (Exception ignored) { }
            });
        }
    }

    // ================= RIWAYAT FOTO & ANALISIS BERTAHAP (v2.1) =================

    /** Ambil kode err dari pesan HTTP firmware (v2.4): teks
     *  'HTTP 500 {"ok":false,"err":"buffer"}' -> sebab yang terbaca.
     *  httpGet melempar exception untuk status bukan-200, jadi jalur
     *  JSON di bawahnya tidak tercapai; sebab dibaca dari sini. */
    private String sebabFromHttp(Exception e) {
        String m = e == null ? "" : String.valueOf(e.getMessage());
        String key = "\"err\":\"";
        int i = m.indexOf(key);
        if (i >= 0) {
            int j = m.indexOf('"', i + key.length());
            if (j > i) return errText(m.substring(i + key.length(), j));
        }
        return null;
    }

    /** Terjemahkan kode err firmware menjadi sebab yang bisa dibaca (v2.2). */
    private String errText(String err) {
        switch (err) {
            case "ai-busy": return "AI sedang sibuk";
            case "buffer":  return "buffer robot belum siap";
            case "camera":  return "kamera sibuk";
            case "decode":  return "gambar kamera gagal dibaca";
            case "sd":      return "SD card tidak siap / gagal tulis";
            case "thumb":   return "data foto tidak ditemukan di SD";
            case "name":    return "nama foto tidak valid";
            default:        return err.isEmpty() ? "tidak diketahui" : err;
        }
    }

    /** Preview foto yang baru disimpan, di kartu Deteksi AI (v2.5). */
    private void loadAiPreview(String file) {
        net.execute(() -> {
            try {
                byte[] jpg = httpGetBytes(baseUrl() + "/photo-get?f=" + file, 3000, 20000);
                final Bitmap bmp = BitmapFactory.decodeByteArray(jpg, 0, jpg.length);
                ui.post(() -> {
                    if (bmp != null && imgAiPrev != null) {
                        imgAiPrev.setImageBitmap(bmp);
                        imgAiPrev.setVisibility(View.VISIBLE);
                    }
                });
            } catch (Exception e) {
                // Preview gagal (mis. sesi SD robot gagal mount) - diamkan;
                // foto tetap tersimpan dan bisa dilihat dari Riwayat Foto.
            }
        });
    }

    /** Langkah 1: robot mengambil foto dan menyimpannya (tanpa analisis). */
    private void doCaptureSave() {
        if (transport != Transport.WIFI) { toast("Belum terhubung ke robot."); return; }
        txtStep.setText("Mengambil foto...");
        net.execute(() -> {
            try {
                String body = httpGet(baseUrl() + "/capture-save", 3000, 20000);
                JSONObject o = new JSONObject(body);
                if (o.optBoolean("ok", false)) {
                    lastPhotoName = o.optString("file", null);
                    ui.post(() -> txtStep.setText("Foto tersimpan: " + lastPhotoName
                            + " — tekan Langkah 2 untuk analisis."));
                    if (lastPhotoName != null) loadAiPreview(lastPhotoName);
                } else {
                    final String sebab = errText(o.optString("err", ""));
                    ui.post(() -> txtStep.setText("Gagal menyimpan foto — sebab: " + sebab + "."));
                }
            } catch (Exception e) {
                final String sebab = sebabFromHttp(e);
                ui.post(() -> txtStep.setText(sebab != null
                        ? "Gagal menyimpan foto \u2014 sebab: " + sebab + "."
                        : "Gagal menyimpan foto: " + e.getMessage()));
            }
        });
    }

    /** Langkah 2: analisis foto tersimpan (dipakai tombol Langkah 2 & Riwayat). */
    private void doAnalyzePhoto(String file) {
        if (transport != Transport.WIFI) { toast("Belum terhubung ke robot."); return; }
        setAiBusy("Menganalisis foto " + file + "...");
        net.execute(() -> {
            try {
                String body = httpGet(baseUrl() + "/analyze-photo?f=" + file, 3000, 40000);
                JSONObject o = new JSONObject(body);
                if (!o.optBoolean("ok", false)) {
                    final String sebab = errText(o.optString("err", ""));
                    ui.post(() -> txtAiResult.setText("Analisis foto gagal — sebab: " + sebab + "."));
                } else {
                    ui.post(() -> {
                        applyAiFields(o);
                        txtStep.setText("Analisis " + file + " selesai — tercatat di Riwayat Foto.");
                    });
                }
            } catch (Exception e) {
                final String sebab2 = sebabFromHttp(e);
                ui.post(() -> txtAiResult.setText(sebab2 != null
                        ? "Analisis foto gagal \u2014 sebab: " + sebab2 + "."
                        : "Analisis foto gagal: " + e.getMessage()));
            }
        });
    }

    /** Muat daftar riwayat foto dari robot dan gambar ulang baris-barisnya. */
    private void loadRiwayat() {
        if (transport != Transport.WIFI) { toast("Belum terhubung ke robot."); return; }
        txtRiwayatStatus.setText("Memuat riwayat...");
        net.execute(() -> {
            try {
                String body = httpGet(baseUrl() + "/photos", 3000, 15000);
                JSONObject o = new JSONObject(body);
                JSONArray arr = o.optJSONArray("photos");
                ui.post(() -> renderRiwayat(arr));
            } catch (Exception e) {
                ui.post(() -> txtRiwayatStatus.setText(
                        "Gagal memuat — pastikan terhubung WiFi robot."));
            }
        });
    }

    private void renderRiwayat(JSONArray arr) {
        riwayatList.removeAllViews();
        if (arr == null || arr.length() == 0) {
            txtRiwayatStatus.setText("Belum ada foto tersimpan. Ambil foto dari Deteksi AI (Langkah 1).");
            return;
        }
        txtRiwayatStatus.setText(arr.length() + " foto tersimpan di robot.");
        for (int i = 0; i < arr.length(); i++) {
            JSONObject p = arr.optJSONObject(i);
            if (p == null) continue;
            final String f = p.optString("f", "");
            boolean done = p.optBoolean("done", false);
            String label = p.optString("label", "");
            double score = p.optDouble("score", 0);
            LinearLayout rowBox = new LinearLayout(this);
            rowBox.setOrientation(LinearLayout.VERTICAL);
            rowBox.setPadding(0, dp(10), 0, dp(4));
            TextView info = tv(f + (done
                    ? "\nHasil: " + label + " (" + String.format(java.util.Locale.US, "%.2f", score) + ")"
                        + ("karat".equals(label) ? " — kandidat korosi, perlu verifikasi operator" : "")
                    : "\nBelum dianalisis"),
                    13, C_TEXT, false);
            rowBox.addView(info);
            LinearLayout btnRow = new LinearLayout(this);
            btnRow.setOrientation(LinearLayout.HORIZONTAL);
            Button bLihat = btn("Lihat", C_TEAL);
            Button bAnalisis = btn("Analisis", C_TEAL);
            Button bHapus = btn("Hapus", C_ORANGE);
            LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            blp.setMargins(dp(2), dp(4), dp(2), 0);
            bLihat.setLayoutParams(blp);
            bAnalisis.setLayoutParams(blp);
            bHapus.setLayoutParams(new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            btnRow.addView(bLihat);
            btnRow.addView(bAnalisis);
            btnRow.addView(bHapus);
            rowBox.addView(btnRow);
            riwayatList.addView(rowBox);
            bLihat.setOnClickListener(v -> lihatFoto(f));
            bAnalisis.setOnClickListener(v -> doAnalyzePhoto(f));
            bHapus.setOnClickListener(v -> hapusFoto(f));
        }
    }

    /** Tampilkan satu foto riwayat di bagian atas kartu Riwayat Foto. */
    private void lihatFoto(String file) {
        net.execute(() -> {
            try {
                byte[] jpg = httpGetBytes(baseUrl() + "/photo-get?f=" + file, 3000, 20000);
                final Bitmap bmp = BitmapFactory.decodeByteArray(jpg, 0, jpg.length);
                ui.post(() -> {
                    if (bmp != null) {
                        imgRiwayat.setImageBitmap(bmp);
                        imgRiwayat.setVisibility(View.VISIBLE);
                        txtRiwayatStatus.setText("Menampilkan: " + file);
                    }
                });
            } catch (Exception e) {
                ui.post(() -> txtRiwayatStatus.setText("Gagal membuka foto."));
            }
        });
    }

    /** Hapus satu foto riwayat di robot lalu muat ulang daftarnya. */
    private void hapusFoto(String file) {
        net.execute(() -> {
            try {
                httpGet(baseUrl() + "/photo-del?f=" + file, 3000, 10000);
            } catch (Exception ignored) {
            }
            ui.post(this::loadRiwayat);
        });
    }

    private void doAnalyze() {
        if (transport == Transport.WIFI) {
            setAiBusy("Menganalisis foto...");
            net.execute(() -> {
                try {
                    String body = httpGet(baseUrl() + "/analyze", 3000, 30000);
                    JSONObject o = new JSONObject(body);
                    if (!o.optBoolean("ok", false)) {
                        ui.post(() -> txtAiResult.setText("Analisis gagal di robot (sibuk). Coba lagi."));
                    } else {
                        ui.post(() -> applyAiFields(o));
                    }
                } catch (Exception e) {
                    ui.post(() -> txtAiResult.setText("Analisis gagal: " + e.getMessage()));
                }
            });
        } else {
            toast("Belum terhubung ke robot.");
        }
    }

    private void doAiAuto(boolean on) {
        if (transport == Transport.WIFI) {
            net.execute(() -> {
                try {
                    String body = httpGet(baseUrl() + "/ai-auto?on=" + (on ? "1" : "0"), 1500, 3000);
                    JSONObject o = new JSONObject(body);
                    ui.post(() -> applyAiFields(o));
                } catch (Exception e) {
                    toast("Gagal mengubah mode otomatis.");
                    ui.post(() -> setSwitchQuiet(!on));
                }
            });
        } else {
            setSwitchQuiet(false);
            toast("Belum terhubung ke robot.");
        }
    }

    private void setSwitchQuiet(boolean v) {
        syncingSwitch = true;
        switchAuto.setChecked(v);
        syncingSwitch = false;
    }

    private void setAiBusy(String msg) {
        txtAiResult.setText(msg);
        txtAiDetail.setText("");
    }

    // ============================ TERAPKAN DATA ============================

    private void applyTelemetry(JSONObject o) {
        if (!activityAlive) return;
        try {
            // WQI
            int wqi = o.optInt("wqi", -1);
            txtWqi.setText(wqi >= 0 ? String.valueOf(wqi) : "-");
            String cat = o.optString("wqi_cat", "-");
            txtWqiCat.setText(cat);
            int catColor = wqi >= 80 ? C_GREEN : (wqi >= 50 ? C_ORANGE : C_RED);
            txtWqiCat.setBackground(rounded(catColor, 12));

            // Banner peringatan dini
            if (o.optBoolean("alert", false)) {
                String msg = o.optString("alert_msg", "");
                bannerAlert.setText("\u26A0 PERINGATAN DINI: " + msg);
                bannerAlert.setVisibility(View.VISIBLE);
            } else {
                bannerAlert.setVisibility(View.GONE);
            }

            // Sensor
            valSuhu.setText(String.format(Locale.US, "%.1f \u00B0C",
                    o.optDouble("temperature_c", Double.NaN)));
            double tv_ = o.optDouble("turbidity_v", Double.NaN);
            valTurb.setText(Double.isNaN(tv_) ? "-"
                    : String.format(Locale.US, "%.2f V", tv_));
            String lvl = o.optString("turbidity_level", "-");
            valTurbLvl.setText("Klasifikasi kekeruhan: " + lvl);
            valTurbLvl.setTextColor("keruh".equals(lvl) ? C_RED
                    : ("sedang".equals(lvl) ? C_ORANGE : C_GREEN));
            valTds.setText(String.format(Locale.US, "%.0f ppm", o.optDouble("tds_ppm", 0)));
            valLeak.setText(o.optBoolean("leak", false) ? "BOCOR!" : "Aman");
            valLeak.setTextColor(o.optBoolean("leak", false) ? C_RED : C_TEXT);
            valRain.setText(o.optBoolean("rain", false) ? "Hujan" : "Tidak hujan");
            // Indikator baterai (v1.7): -1 dari firmware = jalur ukur
            // baterai belum terpasang -> tampilkan apa adanya, jangan
            // mengarang persen.
            if (txtBattery != null) {
                int bp = o.optInt("battery_pct", -1);
                int bmv = o.optInt("battery_mv", -1);
                if (bp >= 0) {
                    txtBattery.setText("\uD83D\uDD0B Baterai: " + bp + "% ("
                            + String.format(Locale.US, "%.2f", bmv / 1000.0) + " V)");
                } else {
                    txtBattery.setText("\uD83D\uDD0B Baterai: belum terukur");
                }
                if (txtSideBatt != null) {
                    boolean chg = o.optBoolean("chg", false);
                    txtSideBatt.setText("\uD83D\uDD0B Baterai: "
                            + (bp >= 0 ? bp + "%" : "belum terukur")
                            + "\nStatus cas: " + (chg ? "sedang dicas" : "tidak dicas"));
                }
            }
            valTilt.setText(String.format(Locale.US, "%.1f\u00B0 / %.1f\u00B0",
                    o.optDouble("roll_deg", 0), o.optDouble("pitch_deg", 0)));

            // Info tambahan di kartu WQI
            long up = o.optLong("uptime_s", 0);
            txtWqiInfo.setText(String.format(Locale.US,
                    "RSSI robot: %d dBm \u2022 Uptime: %d dtk \u2022 Boot #%d \u2022 SD: %s",
                    o.optInt("rssi_dbm", 0), up, o.optInt("boot", 0),
                    o.optBoolean("sd_ready", false) ? "siap" : "tidak ada"));

            // Keadaan AI mengikuti telemetri (termasuk saklar mode otomatis)
            applyAiFields(o);
        } catch (Exception ignored) { }
    }

    /** Terapkan field ai_* dari JSON WiFi (/telemetry, /analyze, /ai-auto). */
    private void applyAiFields(JSONObject o) {
        if (!activityAlive) return;
        try {
            if (o.has("ai_auto")) setSwitchQuiet(o.optBoolean("ai_auto", false));
            if (!o.optBoolean("ai_valid", false)) {
                if (o.has("ai_valid")) {
                    txtAiResult.setText("Belum ada hasil analisis.");
                    txtAiDetail.setText("");
                }
                return;
            }
            String label = o.optString("ai_label", "-");
            double score = o.optDouble("ai_score", 0);
            boolean kandidat = o.optBoolean("ai_kandidat", false);
            txtAiResult.setText(String.format(Locale.US, "%s \u2014 %.0f%%", label, score * 100));
            txtAiResult.setTextColor(kandidat ? C_RED : C_TEXT);
            txtAiDetail.setText(String.format(Locale.US,
                    "%s \u2022 %d ms \u2022 karat %.0f%% \u2022 cat %.0f%% \u2022 logam %.0f%% \u2022 lumut %.0f%%",
                    kandidat ? "KANDIDAT KOROSI - perlu verifikasi operator" : "Aman",
                    o.optInt("ai_ms", 0),
                    o.optDouble("ai_karat", 0) * 100,
                    o.optDouble("ai_cat", 0) * 100,
                    o.optDouble("ai_logam", 0) * 100,
                    o.optDouble("ai_lumut", 0) * 100));
        } catch (Exception ignored) { }
    }

    // ============================ HASIL AKTIVITAS ============================

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_PICK_FILE) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                uploadPicked(data.getData());
            }
        }
    }

    // ============================ SIDEBAR (v1.1) ============================
    // Panel geser dari kiri di dalam bingkai konten (di bawah bilah atas),
    // jadi chip status koneksi tidak pernah tertutup sidebar.

    private TextView sidebarItem(String label) {
        TextView t = tv(label, 15, Color.WHITE, false);
        t.setPadding(dp(20), dp(14), dp(16), dp(14));
        t.setClickable(true);
        return t;
    }

    private void buildSidebar(FrameLayout frame) {
        // Lapisan penggelap di belakang panel; ketuk untuk menutup.
        scrimView = new View(this);
        scrimView.setBackgroundColor(0x66000000);
        scrimView.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        scrimView.setVisibility(View.GONE);
        scrimView.setOnClickListener(v -> closeSidebar());
        frame.addView(scrimView);

        sidebarView = new LinearLayout(this);
        sidebarView.setOrientation(LinearLayout.VERTICAL);
        sidebarView.setBackgroundColor(C_TITLE);
        FrameLayout.LayoutParams slp = new FrameLayout.LayoutParams(
                dp(264), ViewGroup.LayoutParams.MATCH_PARENT);
        slp.gravity = Gravity.LEFT;
        sidebarView.setLayoutParams(slp);
        sidebarView.setClickable(true); // telan ketukan agar tidak tembus ke scrim

        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.setPadding(dp(20), dp(16), dp(8), dp(10));
        TextView hTitle = tv("Menu", 16, Color.WHITE, true);
        hTitle.setLayoutParams(new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        head.addView(hTitle);
        TextView hClose = tv("\u2715", 18, Color.WHITE, false);
        hClose.setPadding(dp(12), dp(6), dp(12), dp(6));
        hClose.setOnClickListener(v -> closeSidebar());
        head.addView(hClose);
        sidebarView.addView(head);

        addSidebarNav("Ringkasan", "ringkasan", v -> { closeSidebar(); showSection("ringkasan"); });
        addSidebarNav("Kamera", "kamera", v -> { closeSidebar(); showSection("kamera"); });
        addSidebarNav("Deteksi AI", "ai", v -> { closeSidebar(); showSection("ai"); });
        addSidebarNav("Riwayat Foto", "riwayat", v -> { closeSidebar(); showSection("riwayat"); });
        addSidebarNav("Sensor", "sensor", v -> { closeSidebar(); showSection("sensor"); });
        addSidebarNav("Kendali Motor", "motor", v -> { closeSidebar(); showSection("motor"); });
        addSidebarItem("Berkas SD", v -> {
            closeSidebar();
            showFilesScreen();
        });
        addSidebarItem("WiFi Robot", v -> {
            closeSidebar();
            showWifiScreen();
        });
        addSidebarItem("Video Live", v -> {
            closeSidebar();
            showVideoScreen();
        });
        // Grup LAINNYA + footer baterai (v2.3, persis susunan web)
        TextView grp = tv("LAINNYA", 11, C_MUTED, true);
        grp.setPadding(dp(20), dp(18), dp(16), dp(2));
        sidebarView.addView(grp);
        addSidebarNav("Tren & Log", "tren", v -> { closeSidebar(); showSection("tren"); loadTren(); });
        addSidebarItem("Unduh CSV Log", v -> {
            closeSidebar();
            downloadUrl(baseUrl() + "/download", "log_telemetri.csv");
        });
        addSidebarItem("Unduh Riwayat AI", v -> {
            closeSidebar();
            downloadUrl(baseUrl() + "/download-ai", "riwayat_ai.csv");
        });
        View spacer = new View(this);
        spacer.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        sidebarView.addView(spacer);
        txtSideBatt = tv("\uD83D\uDD0B Baterai: \u2014\nStatus cas: \u2014", 12, 0xFFBFD9D6, false);
        txtSideBatt.setPadding(dp(20), dp(10), dp(16), dp(18));
        sidebarView.addView(txtSideBatt);
        frame.addView(sidebarView);
    }

    private void addSidebarItem(String label, View.OnClickListener act) {
        TextView t = sidebarItem(label);
        t.setOnClickListener(act);
        sidebarView.addView(t);
    }

    private void toggleSidebar() {
        if (sidebarOpen) closeSidebar(); else openSidebar();
    }

    private void openSidebar() {
        if (sidebarView == null) return;
        sidebarOpen = true;
        if (scrimView != null) scrimView.setVisibility(View.VISIBLE);
        sidebarView.animate().translationX(0).setDuration(220).start();
    }

    private void closeSidebar() {
        if (sidebarView == null || !sidebarOpen) return;
        sidebarOpen = false;
        if (scrimView != null) scrimView.setVisibility(View.GONE);
        sidebarView.animate().translationX(-sidebarView.getWidth())
                .setDuration(220).start();
    }

    private final List<TextView> navItemViews = new ArrayList<>();
    private final List<String> navItemSecs = new ArrayList<>();

    private void addSidebarNav(String label, final String sec, View.OnClickListener act) {
        TextView t = sidebarItem(label);
        t.setOnClickListener(act);
        sidebarView.addView(t);
        navItemViews.add(t);
        navItemSecs.add(sec);
    }

    private void markNavActive(String sec) {
        for (int i = 0; i < navItemViews.size(); i++) {
            boolean on = navItemSecs.get(i).equals(sec);
            navItemViews.get(i).setBackground(on ? rounded(0xFF2A3F7F, 9) : null);
        }
    }

    /** v2.3: navigasi per section seperti dashboard web - satu menu, satu layar. */
    private void showSection(String sec) {
        showMainScreen();
        if (cardConn != null) cardConn.setVisibility("ringkasan".equals(sec) ? View.VISIBLE : View.GONE);
        if (cardWqi != null) cardWqi.setVisibility("ringkasan".equals(sec) ? View.VISIBLE : View.GONE);
        if (cardCam != null) cardCam.setVisibility("kamera".equals(sec) ? View.VISIBLE : View.GONE);
        if (cardAi != null) cardAi.setVisibility("ai".equals(sec) ? View.VISIBLE : View.GONE);
        if (cardRiwayat != null) cardRiwayat.setVisibility("riwayat".equals(sec) ? View.VISIBLE : View.GONE);
        if (cardSensor != null) cardSensor.setVisibility("sensor".equals(sec) ? View.VISIBLE : View.GONE);
        if (cardMotor != null) cardMotor.setVisibility("motor".equals(sec) ? View.VISIBLE : View.GONE);
        if (cardTren != null) cardTren.setVisibility("tren".equals(sec) ? View.VISIBLE : View.GONE);
        if (mainScroll != null) mainScroll.scrollTo(0, 0);
        markNavActive(sec);
    }

    private void showMainScreen() {
        filesVisible = false;
        wifiVisible = false;
        videoVisible = false;
        stopVideo();
        if (filesScreen != null) filesScreen.setVisibility(View.GONE);
        if (wifiScreen != null) wifiScreen.setVisibility(View.GONE);
        if (videoScreen != null) videoScreen.setVisibility(View.GONE);
        if (mainScroll != null) mainScroll.setVisibility(View.VISIBLE);
    }

    private void showFilesScreen() {
        filesVisible = true;
        wifiVisible = false;
        videoVisible = false;
        stopVideo();
        if (wifiScreen != null) wifiScreen.setVisibility(View.GONE);
        if (videoScreen != null) videoScreen.setVisibility(View.GONE);
        if (mainScroll != null) mainScroll.setVisibility(View.GONE);
        if (filesScreen != null) filesScreen.setVisibility(View.VISIBLE);
        refreshFilesGate();
        if (transport == Transport.WIFI) loadFiles(filesDir);
    }

    private void showVideoScreen() {
        videoVisible = true;
        filesVisible = false;
        wifiVisible = false;
        if (mainScroll != null) mainScroll.setVisibility(View.GONE);
        if (filesScreen != null) filesScreen.setVisibility(View.GONE);
        if (wifiScreen != null) wifiScreen.setVisibility(View.GONE);
        if (videoScreen != null) videoScreen.setVisibility(View.VISIBLE);
        if (transport == Transport.WIFI) {
            showVideoMsg(null);
            startVideo();
        } else {
            if (videoStatusTxt != null) videoStatusTxt.setText("Tidak terhubung ke robot.");
            showVideoMsg("Sambungkan dulu ke robot lewat WiFi untuk menonton video live.");
        }
    }

    private void showWifiScreen() {
        wifiVisible = true;
        filesVisible = false;
        videoVisible = false;
        stopVideo();
        if (videoScreen != null) videoScreen.setVisibility(View.GONE);
        if (mainScroll != null) mainScroll.setVisibility(View.GONE);
        if (filesScreen != null) filesScreen.setVisibility(View.GONE);
        if (wifiScreen != null) wifiScreen.setVisibility(View.VISIBLE);
        if (transport == Transport.WIFI) {
            showWifiMsg(null, C_RED);
            loadWifiStatus(true);
        } else {
            if (wifiStatusTxt != null) wifiStatusTxt.setText("Tidak terhubung ke robot.");
            showWifiMsg("Sambungkan dulu ke robot lewat WiFi untuk melihat dan "
                    + "mengganti hotspot tujuannya.", C_RED);
        }
    }

    // ============================ WIFI ROBOT (v1.7) ============================
    // Tab sidebar "WiFi Robot": melihat robot sedang terhubung ke
    // hotspot mana dan mengganti hotspot tujuan STA dari aplikasi.
    // Dukungan firmware v7.3.2: GET /wifi (status) dan POST
    // /wifi-set (simpan kredensial ke LittleFS lalu robot restart;
    // reset=1 kembali ke bawaan). Fail-safe-nya di firmware: STA
    // gagal -> AP darurat "FIR-Robot" menyala, tab ini tetap bisa
    // dipakai dari AP itu untuk memperbaiki kredensial.

    private EditText inputField(String hint, boolean password) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setTextSize(14);
        e.setTextColor(C_TEXT);
        e.setSingleLine(true);
        if (password) {
            e.setInputType(InputType.TYPE_CLASS_TEXT
                    | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        }
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, dp(4), 0, dp(8));
        e.setLayoutParams(lp);
        return e;
    }

    private void buildWifiScreen(FrameLayout frame) {
        wifiScreen = new LinearLayout(this);
        wifiScreen.setOrientation(LinearLayout.VERTICAL);
        wifiScreen.setBackgroundColor(C_BG);
        wifiScreen.setPadding(dp(12), dp(12), dp(12), dp(12));
        wifiScreen.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        wifiScreen.setVisibility(View.GONE);

        // Kepala: tombol Kembali + judul
        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        Button btnBack = btn("\u2039 Kembali", C_TEAL);
        btnBack.setOnClickListener(v -> showMainScreen());
        head.addView(btnBack);
        TextView title = tv("WiFi Robot", 17, C_TEXT, true);
        LinearLayout.LayoutParams tlp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        tlp.setMargins(dp(12), 0, 0, 0);
        title.setLayoutParams(tlp);
        head.addView(title);
        wifiScreen.addView(head);

        // Pesan galat
        wifiMsg = tv("", 13, C_RED, false);
        wifiMsg.setPadding(0, dp(8), 0, dp(4));
        wifiMsg.setVisibility(View.GONE);
        wifiScreen.addView(wifiMsg);

        // Badan bergulir: kartu status + kartu formulir + kartu info
        ScrollView ws = new ScrollView(this);
        ws.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(0, dp(8), 0, 0);
        ws.addView(content);
        wifiScreen.addView(ws);

        LinearLayout cardStatus = card(content, "Status Koneksi Robot");
        wifiStatusTxt = tv("Memuat\u2026", 14, C_TEXT, false);
        wifiStatusTxt.setLineSpacing(0, 1.25f);
        cardStatus.addView(wifiStatusTxt);
        btnWifiRefresh = btn("Muat Ulang Status", C_TEAL);
        LinearLayout.LayoutParams rlp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        rlp.setMargins(0, dp(10), 0, 0);
        btnWifiRefresh.setLayoutParams(rlp);
        cardStatus.addView(btnWifiRefresh);
        btnWifiRefresh.setOnClickListener(v -> loadWifiStatus(false));

        LinearLayout cardForm = card(content, "Ganti Hotspot Tujuan");
        cardForm.addView(tv("Nama hotspot (SSID)", 12, C_MUTED, true));
        editSsid = inputField("mis. KANTOR / hotspot rumah", false);
        cardForm.addView(editSsid);
        cardForm.addView(tv("Kata sandi", 12, C_MUTED, true));
        editWifiPass = inputField("kata sandi hotspot (kosongkan bila tanpa sandi)", true);
        cardForm.addView(editWifiPass);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, dp(4), 0, 0);
        btnWifiSave = btn("Simpan & Sambungkan", C_TEAL);
        LinearLayout.LayoutParams s1 = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        s1.setMargins(0, 0, dp(6), 0);
        btnWifiSave.setLayoutParams(s1);
        btnWifiResetBtn = btn("Reset Bawaan", C_TEAL);
        btnWifiResetBtn.setLayoutParams(new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        row.addView(btnWifiSave);
        row.addView(btnWifiResetBtn);
        cardForm.addView(row);
        btnWifiSave.setOnClickListener(v -> confirmSaveWifi());
        btnWifiResetBtn.setOnClickListener(v -> confirmResetWifi());

        LinearLayout cardInfo = card(content, "Cara Kerja");
        TextView info = tv("Robot menyimpan nama + sandi hotspot di "
                + "memorinya, lalu restart dan mencoba menyambung ke "
                + "hotspot itu (mode STA). Bila berhasil, robot "
                + "mendapat IP baru dari hotspot tersebut \u2014 "
                + "sambungkan HP ke hotspot yang sama, lalu isi IP "
                + "robot yang baru di kartu Sambungan dan tekan "
                + "Coba WiFi.\n\nBila hotspot tidak ditemukan atau "
                + "sandinya salah, robot menyalakan AP darurat "
                + "\"FIR-Robot\" (192.168.4.1) seperti biasa \u2014 "
                + "sambungkan HP ke AP itu dan perbaiki dari tab "
                + "ini. Reset Bawaan mengembalikan tujuan ke "
                + "hotspot bawaan firmware.", 13, C_TEXT, false);
        info.setLineSpacing(0, 1.3f);
        cardInfo.addView(info);

        frame.addView(wifiScreen);
    }

    private void showWifiMsg(String msg, int color) {
        if (wifiMsg == null) return;
        if (msg == null) {
            wifiMsg.setVisibility(View.GONE);
            return;
        }
        wifiMsg.setTextColor(color);
        wifiMsg.setText(msg);
        wifiMsg.setVisibility(View.VISIBLE);
    }

    /** POST formulir url-encoded; pasangan kunci-nilai bergantian. */
    private String httpPostForm(String urlStr, String[][] kv,
                                int connectMs, int readMs) throws Exception {
        HttpURLConnection c = null;
        try {
            StringBuilder body = new StringBuilder();
            for (int i = 0; i < kv.length; i++) {
                if (i > 0) body.append('&');
                body.append(URLEncoder.encode(kv[i][0], "UTF-8")).append('=')
                        .append(URLEncoder.encode(kv[i][1], "UTF-8"));
            }
            byte[] data = body.toString().getBytes(StandardCharsets.UTF_8);
            c = (HttpURLConnection) openConn(new URL(urlStr));
            c.setConnectTimeout(connectMs);
            c.setReadTimeout(readMs);
            c.setRequestMethod("POST");
            c.setDoOutput(true);
            c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
            c.setRequestProperty("Cache-Control", "no-cache");
            OutputStream os = c.getOutputStream();
            os.write(data);
            os.flush();
            os.close();
            int code = c.getResponseCode();
            InputStream in = (code >= 200 && code < 300) ? c.getInputStream() : c.getErrorStream();
            if (in == null) throw new Exception("HTTP " + code);
            BufferedReader r = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null) sb.append(line);
            r.close();
            if (code < 200 || code >= 300) throw new Exception("HTTP " + code + " " + sb);
            return sb.toString();
        } finally {
            if (c != null) c.disconnect();
        }
    }

    private void loadWifiStatus(final boolean prefill) {
        if (transport != Transport.WIFI || wifiStatusTxt == null) return;
        wifiStatusTxt.setText("Memuat status\u2026");
        net.execute(() -> {
            try {
                String body = httpGet(baseUrl() + "/wifi", 2000, 3000);
                JSONObject o = new JSONObject(body);
                final String mode = o.optString("mode", "?");
                final boolean sta = o.optBoolean("sta_connected", false);
                final String target = o.optString("target_ssid", "");
                final String cur = o.optString("sta_ssid", "");
                final String ip = sta ? o.optString("sta_ip", "")
                                      : o.optString("ap_ip", "");
                final int rssi = o.optInt("rssi_dbm", 0);
                final boolean custom = o.optBoolean("custom", false);
                ui.post(() -> {
                    if (!activityAlive || !wifiVisible) return;
                    StringBuilder s = new StringBuilder();
                    if ("AP".equals(mode)) {
                        s.append("Mode: AP darurat \u2014 robot menyalakan hotspot sendiri");
                    } else if (sta) {
                        s.append("Mode: STA \u2014 terhubung ke hotspot");
                    } else {
                        s.append("Mode: STA \u2014 tidak terhubung");
                    }
                    s.append("\nHotspot tujuan: ").append(target);
                    if (sta && cur.length() > 0) s.append("\nTerhubung ke: ").append(cur);
                    s.append("\nIP robot: ").append(ip);
                    if (sta) s.append("\nSinyal: ").append(rssi).append(" dBm");
                    s.append("\nSumber kredensial: ").append(custom
                            ? "tersimpan dari aplikasi" : "bawaan firmware");
                    wifiStatusTxt.setText(s.toString());
                    if (prefill && editSsid != null) editSsid.setText(target);
                    showWifiMsg(null, C_RED);
                });
            } catch (Exception e) {
                final String m = shortReason(e);
                ui.post(() -> {
                    if (!activityAlive || !wifiVisible) return;
                    wifiStatusTxt.setText("Gagal memuat status.");
                    showWifiMsg("Gagal: " + m, C_RED);
                });
            }
        });
    }

    private void confirmSaveWifi() {
        if (transport != Transport.WIFI) {
            showWifiMsg("Tidak terhubung ke robot.", C_RED);
            return;
        }
        final String ssid = editSsid.getText().toString().trim();
        final String pass = editWifiPass.getText().toString();
        if (ssid.length() == 0) {
            toast("Nama hotspot (SSID) tidak boleh kosong.");
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle("Ganti hotspot robot?")
                .setMessage("Robot akan restart dan mencoba menyambung ke \""
                        + ssid + "\".\n\nBila gagal, robot menyalakan AP "
                        + "darurat \"FIR-Robot\" (192.168.4.1) \u2014 "
                        + "sambungkan HP ke AP itu untuk memperbaikinya "
                        + "dari tab ini juga.")
                .setNegativeButton("Batal", null)
                .setPositiveButton("Simpan & Restart", (d, w) -> {
                    net.execute(() -> {
                        try {
                            httpPostForm(baseUrl() + "/wifi-set",
                                    new String[][]{{"ssid", ssid}, {"pass", pass}},
                                    2000, 3000);
                            ui.post(() -> {
                                if (!activityAlive) return;
                                toast("Kredensial tersimpan. Robot sedang "
                                        + "restart \u2014 sambungkan HP ke "
                                        + "hotspot \"" + ssid + "\", isi IP "
                                        + "robot yang baru, lalu Coba WiFi.");
                                showMainScreen();
                            });
                        } catch (Exception e) {
                            final String m = shortReason(e);
                            ui.post(() -> {
                                if (activityAlive) {
                                    showWifiMsg("Gagal menyimpan: " + m, C_RED);
                                }
                            });
                        }
                    });
                })
                .show();
    }

    private void confirmResetWifi() {
        if (transport != Transport.WIFI) {
            showWifiMsg("Tidak terhubung ke robot.", C_RED);
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle("Reset hotspot bawaan?")
                .setMessage("Kredensial tersimpan dihapus. Robot restart "
                        + "dan kembali menyambung ke hotspot bawaan "
                        + "firmware.")
                .setNegativeButton("Batal", null)
                .setPositiveButton("Reset & Restart", (d, w) -> {
                    net.execute(() -> {
                        try {
                            httpPostForm(baseUrl() + "/wifi-set",
                                    new String[][]{{"reset", "1"}}, 2000, 3000);
                            ui.post(() -> {
                                if (!activityAlive) return;
                                toast("Kredensial direset. Robot sedang restart.");
                                showMainScreen();
                            });
                        } catch (Exception e) {
                            final String m = shortReason(e);
                            ui.post(() -> {
                                if (activityAlive) {
                                    showWifiMsg("Gagal reset: " + m, C_RED);
                                }
                            });
                        }
                    });
                })
                .show();
    }

    // ============================ VIDEO LIVE (v1.8) ============================
    // Memutar stream MJPEG robot (GET /stream) di ImageView.
    // Stream diuraikan dengan memindai penanda JPEG SOI (FF D8)
    // dan EOI (FF D9) pada aliran byte mentah - tahan terhadap
    // variasi header multipart antar firmware. Selama stream
    // jalan, server web robot tertahan di handler stream
    // (satu antrean), jadi polling telemetri + foto berkala
    // dijeda (lihat pollRunnable/cameraRunnable) dan status
    // koneksi TIDAK dinilai dari kegagalan selama video aktif.

    private void buildVideoScreen(FrameLayout frame) {
        videoScreen = new LinearLayout(this);
        videoScreen.setOrientation(LinearLayout.VERTICAL);
        videoScreen.setBackgroundColor(C_BG);
        videoScreen.setPadding(dp(12), dp(12), dp(12), dp(12));
        videoScreen.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        videoScreen.setVisibility(View.GONE);

        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        Button btnBack = btn("\u2039 Kembali", C_TEAL);
        btnBack.setOnClickListener(v -> showMainScreen());
        head.addView(btnBack);
        TextView title = tv("Video Live", 17, C_TEXT, true);
        LinearLayout.LayoutParams tlp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        tlp.setMargins(dp(12), 0, 0, 0);
        title.setLayoutParams(tlp);
        head.addView(title);
        videoScreen.addView(head);

        videoMsg = tv("", 13, C_RED, false);
        videoMsg.setPadding(0, dp(8), 0, dp(4));
        videoMsg.setVisibility(View.GONE);
        videoScreen.addView(videoMsg);

        // Panggung video: hitam, gambar di tengah.
        FrameLayout stage = new FrameLayout(this);
        stage.setBackgroundColor(0xFF000000);
        stage.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        imgVideo = new ImageView(this);
        imgVideo.setScaleType(ImageView.ScaleType.FIT_CENTER);
        imgVideo.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        stage.addView(imgVideo);
        videoScreen.addView(stage);

        videoStatusTxt = tv("Belum mulai.", 13, C_MUTED, false);
        videoStatusTxt.setPadding(0, dp(10), 0, dp(2));
        videoScreen.addView(videoStatusTxt);
        TextView note = tv("Selama video jalan, telemetri & kendali dijeda "
                + "(server robot satu antrean). Tekan Kembali untuk "
                + "memutus stream dan melanjutkan semuanya.", 12, C_MUTED, false);
        videoScreen.addView(note);

        frame.addView(videoScreen);
    }

    private void showVideoMsg(String msg) {
        if (videoMsg == null) return;
        if (msg == null) {
            videoMsg.setVisibility(View.GONE);
            return;
        }
        videoMsg.setText(msg);
        videoMsg.setVisibility(View.VISIBLE);
    }

    private void startVideo() {
        if (videoActive) return;
        videoActive = true;
        videoFrames = 0;
        videoStartMs = System.currentTimeMillis();
        if (videoStatusTxt != null) videoStatusTxt.setText("Menghubungkan ke stream robot\u2026");
        videoThread = new Thread(this::runVideoStream, "fir-video");
        videoThread.start();
    }

    private void stopVideo() {
        videoActive = false;
        HttpURLConnection c = videoConn;
        videoConn = null;
        if (c != null) c.disconnect();   // memutus read() yang tertahan
        videoThread = null;
    }

    private void runVideoStream() {
        String err = null;
        try {
            HttpURLConnection c = (HttpURLConnection) openConn(new URL(baseUrl() + "/stream"));
            videoConn = c;
            c.setConnectTimeout(3000);
            c.setReadTimeout(15000);
            c.setRequestProperty("Cache-Control", "no-cache");
            int code = c.getResponseCode();
            if (code != 200) throw new Exception("HTTP " + code);
            InputStream in = new BufferedInputStream(c.getInputStream(), 16384);
            ByteArrayOutputStream frame = new ByteArrayOutputStream(65536);
            int state = 0;   // 0 = cari SOI, 1 = di dalam frame
            int prev = -1;
            while (videoActive && activityAlive) {
                int b = in.read();
                if (b < 0) break;
                if (state == 0) {
                    if (prev == 0xFF && b == 0xD8) {
                        frame.reset();
                        frame.write(0xFF);
                        frame.write(0xD8);
                        state = 1;
                    }
                } else {
                    frame.write(b);
                    if (prev == 0xFF && b == 0xD9) {
                        byte[] jpg = frame.toByteArray();
                        frame.reset();
                        state = 0;
                        Bitmap bmp = BitmapFactory.decodeByteArray(jpg, 0, jpg.length);
                        if (bmp != null) {
                            videoFrames++;
                            ui.post(() -> showVideoFrame(bmp));
                        }
                    } else if (frame.size() > 1024 * 1024) {
                        frame.reset();   // frame rusak/terlalu besar -> sinkron ulang
                        state = 0;
                    }
                }
                prev = b;
            }
        } catch (Exception e) {
            if (videoActive) err = shortReason(e);
        } finally {
            HttpURLConnection c = videoConn;
            videoConn = null;
            if (c != null) c.disconnect();
            final String m = err;
            final boolean wasActive = videoActive;
            videoActive = false;
            ui.post(() -> {
                if (!activityAlive || !videoVisible) return;
                if (wasActive) {
                    if (videoStatusTxt != null) videoStatusTxt.setText("Stream berhenti.");
                    showVideoMsg(m != null ? ("Stream terputus: " + m
                            + " \u2014 tekan Kembali lalu buka lagi Video Live.")
                            : "Stream berakhir dari sisi robot.");
                }
            });
        }
    }

    private void showVideoFrame(Bitmap bmp) {
        if (!activityAlive || !videoVisible || imgVideo == null) {
            bmp.recycle();
            return;
        }
        imgVideo.setImageBitmap(bmp);
        if (lastVideoBitmap != null && lastVideoBitmap != bmp) lastVideoBitmap.recycle();
        lastVideoBitmap = bmp;
        long el = Math.max(1, System.currentTimeMillis() - videoStartMs);
        double fps = videoFrames * 1000.0 / el;
        if (videoStatusTxt != null) {
            videoStatusTxt.setText(String.format(Locale.US,
                    "Live \u2022 %d frame \u2022 \u00B1%.1f fps", videoFrames, fps));
        }
    }

    // ============================ BERKAS SD (v1.1) ============================
    // Pengelola berkas SD robot lewat endpoint firmware: /files-json (daftar),
    // /file-get (unduh), /file-del (hapus), /file-mkdir (folder baru),
    // /file-upload (unggah multipart). Hanya aktif lewat WiFi.

    private void buildFilesScreen(FrameLayout frame) {
        filesScreen = new LinearLayout(this);
        filesScreen.setOrientation(LinearLayout.VERTICAL);
        filesScreen.setBackgroundColor(C_BG);
        filesScreen.setPadding(dp(12), dp(12), dp(12), dp(12));
        filesScreen.setLayoutParams(new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        filesScreen.setVisibility(View.GONE);

        // Kepala: tombol Kembali + judul
        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        Button btnBack = btn("\u2039 Kembali", C_TEAL);
        btnBack.setOnClickListener(v -> showMainScreen());
        head.addView(btnBack);
        TextView title = tv("Berkas SD", 17, C_TEXT, true);
        LinearLayout.LayoutParams tlp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        tlp.setMargins(dp(12), 0, 0, 0);
        title.setLayoutParams(tlp);
        head.addView(title);
        filesScreen.addView(head);

        // Breadcrumb path saat ini
        filesCrumb = tv("/", 14, C_TEXT, true);
        filesCrumb.setPadding(0, dp(12), 0, dp(4));
        filesScreen.addView(filesCrumb);

        // Pesan status/galat + tombol coba lagi
        filesMsg = tv("", 13, C_RED, false);
        filesMsg.setPadding(0, dp(2), 0, dp(6));
        filesMsg.setVisibility(View.GONE);
        filesScreen.addView(filesMsg);
        btnFilesRetry = btn("Coba Lagi", C_ORANGE);
        btnFilesRetry.setVisibility(View.GONE);
        btnFilesRetry.setOnClickListener(v -> loadFiles(filesDir));
        filesScreen.addView(btnFilesRetry);

        // Baris aksi: Folder Baru + Unggah Berkas
        LinearLayout acts = new LinearLayout(this);
        acts.setOrientation(LinearLayout.HORIZONTAL);
        acts.setPadding(0, dp(6), 0, dp(10));
        btnFilesNew = btn("Folder Baru", C_TEAL);
        btnFilesUpload = btn("Unggah Berkas", C_TEAL);
        LinearLayout.LayoutParams a1 = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        a1.setMargins(0, 0, dp(6), 0);
        btnFilesNew.setLayoutParams(a1);
        btnFilesUpload.setLayoutParams(new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        acts.addView(btnFilesNew);
        acts.addView(btnFilesUpload);
        filesScreen.addView(acts);
        btnFilesNew.setOnClickListener(v -> promptNewFolder());
        btnFilesUpload.setOnClickListener(v -> pickFileForUpload());

        // Daftar isi folder
        ScrollView fs = new ScrollView(this);
        fs.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        filesList = new LinearLayout(this);
        filesList.setOrientation(LinearLayout.VERTICAL);
        fs.addView(filesList);
        filesScreen.addView(fs);

        frame.addView(filesScreen);
    }

    /** Gerbang transportasi layar berkas: hanya WiFi yang boleh beraksi. */
    private void refreshFilesGate() {
        if (filesScreen == null) return;
        boolean wifi = transport == Transport.WIFI;
        if (btnFilesNew != null) btnFilesNew.setEnabled(wifi);
        if (btnFilesUpload != null) btnFilesUpload.setEnabled(wifi);
        if (!filesVisible) return;
        if (transport == Transport.NONE) {
            showFilesMsg("Tidak terhubung ke robot. Berkas SD hanya tersedia lewat WiFi.",
                    C_RED, false);
        } else {
            loadFiles(filesDir);
        }
    }

    private void showFilesMsg(String msg, int color, boolean withRetry) {
        if (filesMsg == null) return;
        if (msg == null) {
            filesMsg.setVisibility(View.GONE);
            if (btnFilesRetry != null) btnFilesRetry.setVisibility(View.GONE);
            return;
        }
        filesMsg.setTextColor(color);
        filesMsg.setText(msg);
        filesMsg.setVisibility(View.VISIBLE);
        if (btnFilesRetry != null) {
            btnFilesRetry.setVisibility(withRetry ? View.VISIBLE : View.GONE);
        }
    }

    /** Pengodean URL meniru urlEncodeSd firmware: '/' dipertahankan. */
    private String urlEncSd(String s) {
        StringBuilder out = new StringBuilder();
        final String hex = "0123456789ABCDEF";
        for (byte b : s.getBytes(StandardCharsets.UTF_8)) {
            int c = b & 0xFF;
            boolean keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                    || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_'
                    || c == '~' || c == '/';
            if (keep) {
                out.append((char) c);
            } else {
                out.append('%').append(hex.charAt((c >> 4) & 0xF))
                        .append(hex.charAt(c & 0xF));
            }
        }
        return out.toString();
    }

    private String crumbText(String dir) {
        if (dir == null || dir.equals("/")) return "/";
        StringBuilder sb = new StringBuilder("/");
        for (String seg : dir.split("/")) {
            if (seg.isEmpty()) continue;
            sb.append(" > ").append(seg);
        }
        return sb.toString();
    }

    private String fmtSize(long bytes) {
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024L * 1024L) {
            return String.format(Locale.US, "%.1f KB", bytes / 1024.0);
        }
        return String.format(Locale.US, "%.2f MB", bytes / (1024.0 * 1024.0));
    }

    private String parentDirOf(String path) {
        String p = path;
        while (p.length() > 1 && p.endsWith("/")) p = p.substring(0, p.length() - 1);
        int slash = p.lastIndexOf('/');
        return slash <= 0 ? "/" : p.substring(0, slash);
    }

    private String childPath(String dir, String name) {
        return dir.equals("/") ? "/" + name : dir + "/" + name;
    }

    private void loadFiles(final String dir) {
        if (transport != Transport.WIFI) {
            refreshFilesGate();
            return;
        }
        showFilesMsg("Memuat isi folder...", C_MUTED, false);
        net.execute(() -> {
            try {
                String body = httpGet(baseUrl() + "/files-json?dir=" + urlEncSd(dir),
                        2000, 8000);
                JSONObject o = new JSONObject(body);
                if (!o.optBoolean("sd", false)) {
                    ui.post(() -> {
                        if (!activityAlive) return;
                        filesDir = dir;
                        filesCrumb.setText(crumbText(dir));
                        filesList.removeAllViews();
                        showFilesMsg("SD card tidak terpasang / tidak terbaca di robot.",
                                C_RED, true);
                    });
                    return;
                }
                final String cur = o.optString("dir", dir);
                final List<SdEntry> entries = new ArrayList<>();
                JSONArray arr = o.optJSONArray("entries");
                if (arr != null) {
                    for (int i = 0; i < arr.length(); i++) {
                        JSONObject e = arr.optJSONObject(i);
                        if (e == null) continue;
                        SdEntry se = new SdEntry();
                        se.name = e.optString("name", "");
                        se.dir = e.optBoolean("dir", false);
                        se.size = e.optLong("size", 0);
                        if (!se.name.isEmpty()) entries.add(se);
                    }
                }
                ui.post(() -> {
                    if (!activityAlive) return;
                    filesDir = cur;
                    renderFiles(entries);
                    showFilesMsg(null, C_RED, false);
                });
            } catch (Exception e) {
                ui.post(() -> showFilesMsg(
                        "Gagal memuat daftar berkas: " + e.getMessage(), C_RED, true));
            }
        });
    }

    private void renderFiles(List<SdEntry> entries) {
        filesCrumb.setText(crumbText(filesDir));
        filesList.removeAllViews();
        if (!filesDir.equals("/")) {
            filesList.addView(fileRow("\u2B06", "..", "folder induk", true,
                    v -> loadFiles(parentDirOf(filesDir))));
        }
        if (entries.isEmpty()) {
            TextView empty = tv("(folder kosong)", 13, C_MUTED, false);
            empty.setPadding(dp(6), dp(10), dp(6), dp(10));
            filesList.addView(empty);
        }
        for (final SdEntry e : entries) {
            String icon = e.dir ? "\uD83D\uDCC1" : "\uD83D\uDCC4";
            String right = e.dir ? "folder" : fmtSize(e.size);
            filesList.addView(fileRow(icon, e.name, right, false, v -> {
                if (e.dir) {
                    loadFiles(childPath(filesDir, e.name));
                } else {
                    showFileActions(e);
                }
            }));
        }
    }

    private View fileRow(String icon, String name, String right, boolean bold,
                         View.OnClickListener act) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setBackground(rounded(C_CARD, 10));
        row.setPadding(dp(12), dp(11), dp(12), dp(11));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, 0, 0, dp(8));
        row.setLayoutParams(lp);
        row.setClickable(true);
        row.setOnClickListener(act);
        TextView ic = tv(icon, 17, C_TEXT, false);
        ic.setPadding(0, 0, dp(10), 0);
        row.addView(ic);
        TextView nm = tv(name, 14, C_TEXT, bold);
        nm.setLayoutParams(new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        row.addView(nm);
        TextView rt = tv(right, 12, C_MUTED, false);
        row.addView(rt);
        return row;
    }

    private void showFileActions(final SdEntry e) {
        final String full = childPath(filesDir, e.name);
        new AlertDialog.Builder(this)
                .setTitle(e.name)
                .setItems(new CharSequence[]{"Unduh", "Hapus"}, (dialog, which) -> {
                    if (which == 0) downloadFile(full, e.name);
                    else confirmDelete(full, e.name);
                })
                .setNegativeButton("Batal", null)
                .show();
    }

    private void confirmDelete(final String full, final String name) {
        new AlertDialog.Builder(this)
                .setTitle("Hapus berkas")
                .setMessage("Hapus berkas '" + name + "' secara permanen dari SD robot?")
                .setPositiveButton("Hapus", (dialog, which) -> deletePath(full))
                .setNegativeButton("Batal", null)
                .show();
    }

    private void deletePath(final String full) {
        net.execute(() -> {
            try {
                // Firmware membalas 303 ke halaman /files; ikuti saja sebagai sukses.
                httpGet(baseUrl() + "/file-del?path=" + urlEncSd(full), 2000, 8000);
                toast("Berkas dihapus: " + full);
            } catch (Exception e) {
                toast("Gagal menghapus: " + e.getMessage());
            }
            ui.post(() -> { if (filesVisible) loadFiles(filesDir); });
        });
    }

    /** Tren & Log (v2.3): ringkasan riwayat telemetri sesi ini dari /history. */
    private void loadTren() {
        if (txtTren == null) return;
        if (transport != Transport.WIFI) {
            txtTren.setText("Tidak terhubung ke robot.");
            return;
        }
        txtTren.setText("Memuat tren...");
        net.execute(() -> {
            try {
                String body = httpGet(baseUrl() + "/history", 5000, 10000);
                org.json.JSONArray recs = new JSONObject(body).optJSONArray("records");
                if (recs == null || recs.length() == 0) {
                    ui.post(() -> { if (txtTren != null) txtTren.setText(
                            "Belum ada data tren sesi ini (log tercatat tiap 60 detik)."); });
                    return;
                }
                int n = recs.length();
                double mn = 1e9, mx = -1e9, sum = 0;
                for (int i = 0; i < n; i++) {
                    double t = recs.getJSONObject(i).optDouble("t", 0);
                    if (t < mn) mn = t;
                    if (t > mx) mx = t;
                    sum += t;
                }
                StringBuilder last = new StringBuilder();
                int from = Math.max(0, n - 10);
                for (int i = from; i < n; i++) {
                    if (i > from) last.append(" \u2192 ");
                    last.append(String.format(Locale.US, "%.1f",
                            recs.getJSONObject(i).optDouble("t", 0)));
                }
                final String msg = n + " catatan sesi ini\nSuhu air: min "
                        + String.format(Locale.US, "%.1f", mn) + " \u00B0C \u2022 maks "
                        + String.format(Locale.US, "%.1f", mx) + " \u00B0C \u2022 rata-rata "
                        + String.format(Locale.US, "%.1f", sum / n) + " \u00B0C\nTerakhir: "
                        + last + " \u00B0C";
                ui.post(() -> { if (txtTren != null) txtTren.setText(msg); });
            } catch (Exception e) {
                ui.post(() -> { if (txtTren != null)
                        txtTren.setText("Gagal memuat tren: " + shortReason(e)); });
            }
        });
    }

    private void downloadFile(final String full, final String name) {
        downloadUrl(baseUrl() + "/file-get?path=" + urlEncSd(full) + "&dl=1", name);
    }

    /** Unduh satu URL robot ke folder Download aplikasi (v2.3: juga untuk log). */
    private void downloadUrl(final String urlStr, final String name) {
        if (transport != Transport.WIFI) { toast("Tidak terhubung ke robot."); return; }
        toast("Mengunduh " + name + "...");
        net.execute(() -> {
            try {
                File dir = FirFileProvider.downloadDir(this);
                String safe = name.replace("/", "_").replace("\\", "_");
                final File out = new File(dir, safe);
                HttpURLConnection c = null;
                try {
                    c = (HttpURLConnection) openConn(new URL(urlStr));
                    c.setConnectTimeout(3000);
                    c.setReadTimeout(30000);
                    c.setRequestMethod("GET");
                    int code = c.getResponseCode();
                    if (code < 200 || code >= 300) throw new Exception("HTTP " + code);
                    InputStream in = c.getInputStream();
                    FileOutputStream fos = new FileOutputStream(out);
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = in.read(buf)) > 0) fos.write(buf, 0, n);
                    fos.close();
                    in.close();
                } finally {
                    if (c != null) c.disconnect();
                }
                final long sz = out.length();
                ui.post(() -> {
                    if (!activityAlive) return;
                    new AlertDialog.Builder(this)
                            .setTitle("Unduhan selesai")
                            .setMessage(name + " (" + fmtSize(sz) + ") tersimpan di folder "
                                    + "Download aplikasi.")
                            .setPositiveButton("Buka", (d, w) -> openDownloaded(out))
                            .setNegativeButton("Tutup", null)
                            .show();
                });
            } catch (Exception e) {
                toast("Unduhan gagal: " + e.getMessage());
            }
        });
    }

    private void openDownloaded(File f) {
        try {
            Uri uri = Uri.parse("content://id.fir.robot.files/" + Uri.encode(f.getName()));
            Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.setDataAndType(uri, mimeOf(f.getName()));
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(intent);
        } catch (Exception e) {
            toast("Tidak ada aplikasi untuk membuka berkas ini.");
        }
    }

    /** Pemetaan MIME meniru handleFileGet firmware. */
    private String mimeOf(String name) {
        String lower = name.toLowerCase(Locale.US);
        if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
        if (lower.endsWith(".png")) return "image/png";
        if (lower.endsWith(".txt") || lower.endsWith(".csv")) return "text/plain";
        return "application/octet-stream";
    }

    private void promptNewFolder() {
        if (transport != Transport.WIFI) { refreshFilesGate(); return; }
        final EditText input = new EditText(this);
        input.setHint("Nama folder baru");
        input.setSingleLine(true);
        new AlertDialog.Builder(this)
                .setTitle("Folder Baru di " + filesDir)
                .setView(input)
                .setPositiveButton("Buat", (d, w) -> {
                    String name = input.getText().toString().trim();
                    if (name.isEmpty()) {
                        toast("Nama folder kosong.");
                        return;
                    }
                    createFolder(name);
                })
                .setNegativeButton("Batal", null)
                .show();
    }

    private void createFolder(final String name) {
        net.execute(() -> {
            try {
                String body = "dir=" + URLEncoder.encode(filesDir, "UTF-8")
                        + "&name=" + URLEncoder.encode(name, "UTF-8");
                int code = httpPostForm(baseUrl() + "/file-mkdir", body);
                if (code == 200 || code == 303) {
                    toast("Folder '" + name + "' dibuat.");
                } else {
                    toast("Buat folder gagal (HTTP " + code + ").");
                }
            } catch (Exception e) {
                toast("Buat folder gagal: " + e.getMessage());
            }
            ui.post(() -> { if (filesVisible) loadFiles(filesDir); });
        });
    }

    /** POST form-urlencoded tanpa mengikuti redirect; kembalikan kode HTTP. */
    private int httpPostForm(String urlStr, String body) throws Exception {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) openConn(new URL(urlStr));
            c.setConnectTimeout(2000);
            c.setReadTimeout(8000);
            c.setRequestMethod("POST");
            c.setDoOutput(true);
            c.setInstanceFollowRedirects(false);
            c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
            byte[] data = body.getBytes(StandardCharsets.UTF_8);
            c.setRequestProperty("Content-Length", String.valueOf(data.length));
            OutputStream out = c.getOutputStream();
            out.write(data);
            out.close();
            int code = c.getResponseCode();
            InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
            if (in != null) {
                byte[] b = new byte[1024];
                while (in.read(b) > 0) { }
                in.close();
            }
            return code;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    private void pickFileForUpload() {
        if (transport != Transport.WIFI) { refreshFilesGate(); return; }
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, REQ_PICK_FILE);
        } catch (Exception e) {
            toast("Pemilih berkas tidak tersedia.");
        }
    }

    private void uploadPicked(final Uri uri) {
        String name = queryDisplayName(uri);
        if (name == null || name.isEmpty()) name = "unggahan.bin";
        final String fileName = name.replace("\"", "_");
        final String targetDir = filesDir;
        toast("Mengunggah " + fileName + " ke " + targetDir + "...");
        net.execute(() -> {
            try {
                String boundary = "FIRRobot-" + System.currentTimeMillis();
                HttpURLConnection c = null;
                try {
                    c = (HttpURLConnection) openConn(new URL(baseUrl() + "/file-upload"));
                    c.setConnectTimeout(3000);
                    c.setReadTimeout(120000);
                    c.setRequestMethod("POST");
                    c.setDoOutput(true);
                    c.setInstanceFollowRedirects(false);
                    c.setRequestProperty("Content-Type",
                            "multipart/form-data; boundary=" + boundary);
                    c.setChunkedStreamingMode(8192);
                    OutputStream out = new BufferedOutputStream(c.getOutputStream());
                    // PENTING (kontrak firmware): field "dir" HARUS mendahului
                    // field berkas — server membacanya saat UPLOAD_FILE_START.
                    out.write(("--" + boundary + "\r\n").getBytes(StandardCharsets.UTF_8));
                    out.write("Content-Disposition: form-data; name=\"dir\"\r\n\r\n"
                            .getBytes(StandardCharsets.UTF_8));
                    out.write(targetDir.getBytes(StandardCharsets.UTF_8));
                    out.write("\r\n".getBytes(StandardCharsets.UTF_8));
                    out.write(("--" + boundary + "\r\n").getBytes(StandardCharsets.UTF_8));
                    out.write(("Content-Disposition: form-data; name=\"file\"; "
                            + "filename=\"" + fileName + "\"\r\n")
                            .getBytes(StandardCharsets.UTF_8));
                    out.write("Content-Type: application/octet-stream\r\n\r\n"
                            .getBytes(StandardCharsets.UTF_8));
                    InputStream in = getContentResolver().openInputStream(uri);
                    if (in == null) throw new Exception("Berkas tidak dapat dibaca.");
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                    in.close();
                    out.write(("\r\n--" + boundary + "--\r\n")
                            .getBytes(StandardCharsets.UTF_8));
                    out.flush();
                    out.close();
                    int code = c.getResponseCode();
                    InputStream rin = code >= 400 ? c.getErrorStream() : c.getInputStream();
                    if (rin != null) {
                        byte[] b = new byte[1024];
                        while (rin.read(b) > 0) { }
                        rin.close();
                    }
                    if (code == 200 || code == 303) {
                        toast("Unggahan selesai: " + fileName);
                    } else {
                        toast("Unggahan gagal (HTTP " + code + ").");
                    }
                } finally {
                    if (c != null) c.disconnect();
                }
            } catch (Exception e) {
                toast("Unggahan gagal: " + e.getMessage());
            }
            ui.post(() -> { if (filesVisible) loadFiles(filesDir); });
        });
    }

    private String queryDisplayName(Uri uri) {
        Cursor cur = null;
        try {
            cur = getContentResolver().query(uri,
                    new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null);
            if (cur != null && cur.moveToFirst()) {
                int idx = cur.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) return cur.getString(idx);
            }
        } catch (Exception ignored) {
        } finally {
            if (cur != null) cur.close();
        }
        return uri.getLastPathSegment();
    }
}
