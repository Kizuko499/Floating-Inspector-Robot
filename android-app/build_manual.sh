#!/bin/bash
# Build APK debug FIR Robot tanpa Gradle (dipakai di VM ini karena proksi
# egress memblokir unduhan dependensi Java/Gradle; proyek Gradle tetap valid
# dan dapat dibangun dengan ./gradlew assembleDebug di jaringan normal).
set -e
export JAVA_HOME=${JAVA_HOME:-$HOME/tools/jdk17}
export PATH="$JAVA_HOME/bin:$PATH"
BT=$HOME/android-sdk/build-tools/34.0.0
AJ=$HOME/android-sdk/platforms/android-34/android.jar
PROJ=$(cd "$(dirname "$0")" && pwd)
WORK=$PROJ/app/build/manual
FINAL=$PROJ/app/build/outputs/apk/debug/app-debug.apk

rm -rf "$WORK"
mkdir -p "$WORK/gen" "$WORK/classes" "$WORK/dex" "$(dirname "$FINAL")"

echo "[1/6] aapt2 compile + link (manifes + sumber daya)"
"$BT/aapt2" compile --dir "$PROJ/app/src/main/res" -o "$WORK/res.zip"
"$BT/aapt2" link -o "$WORK/base.apk" -I "$AJ" "$WORK/res.zip" \
    --manifest "$PROJ/app/src/main/AndroidManifest.xml" \
    --rename-manifest-package id.fir.robot \
    --min-sdk-version 26 --target-sdk-version 34 \
    --version-code 16 --version-name 2.5 \
    --java "$WORK/gen"

echo "[2/6] javac"
find "$PROJ/app/src/main/java" "$WORK/gen" -name '*.java' > "$WORK/sources.txt"
# Catatan: android.jar di -cp (bukan -bootclasspath) agar LambdaMetafactory
# JDK dapat dipakai javac; lambda kemudian di-desugar oleh d8.
"$JAVA_HOME/bin/javac" -encoding UTF-8 -source 8 -target 8 \
    -cp "$AJ" -d "$WORK/classes" @"$WORK/sources.txt"

echo "[3/6] d8 (dex)"
"$JAVA_HOME/bin/jar" cf "$WORK/classes.jar" -C "$WORK/classes" .
"$BT/d8" --min-api 26 --lib "$AJ" --output "$WORK/dex" "$WORK/classes.jar"

echo "[4/6] gabung dex ke APK"
cp "$WORK/base.apk" "$WORK/app-unsigned.apk"
python3 - "$WORK/app-unsigned.apk" "$WORK/dex/classes.dex" <<'PYEOF'
import sys, zipfile
apk, dex = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(apk, 'a') as z:
    z.write(dex, 'classes.dex', zipfile.ZIP_DEFLATED)
PYEOF

echo "[5/6] zipalign"
"$BT/zipalign" -f -p 4 "$WORK/app-unsigned.apk" "$WORK/app-aligned.apk"

echo "[6/6] apksigner (keystore debug)"
"$BT/apksigner" sign \
    --ks "$HOME/.android/debug.keystore" \
    --ks-key-alias androiddebugkey \
    --ks-pass pass:android --key-pass pass:android \
    --out "$FINAL" "$WORK/app-aligned.apk"

echo "APK jadi: $FINAL"
ls -la "$FINAL"
