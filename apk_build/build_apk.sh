#!/bin/bash
# AI Hear APK 一键构建 (对齐→签名 顺序正确)
set -e
cd "$(dirname "$0")"

JHOME="C:/Program Files/Android/Android Studio/jbr"
SDK="C:/Users/jeveux/AppData/Local/Android/Sdk"
BT="$SDK/build-tools/37.0.0"
BT34="$SDK/build-tools/34.0.0"
JAR="$SDK/platforms/android-34/android.jar"
JAVAC="$JHOME/bin/javac.exe"
JAVA="$JHOME/bin/java.exe"
DESKTOP="/c/Users/jeveux/Desktop"
export JAVA_HOME="$JHOME"

echo "🧹 清理" && rm -rf obj tmp res app-unsigned.apk app-aligned.apk app-signed.apk
mkdir -p obj tmp res/values
cat > res/values/strings.xml <<< '<?xml version="1.0" encoding="utf-8"?><resources><string name="app_name">AI Hear</string></resources>'

echo "🔨 javac"    && "$JAVAC" -source 1.8 -target 1.8 -cp "$JAR" -d obj *.java
echo "📦 d8"       && "$JAVA" -Xmx1024M -Xss1m -cp "$BT/lib/d8.jar" com.android.tools.r8.D8 \
                       --release --min-api 26 --output tmp obj/com/aihear/app/*.class
echo "📁 aapt"      && "$BT34/aapt.exe" package -f --target-sdk-version 34 -M AndroidManifest.xml -S res -A assets -I "$JAR" -F app-unsigned.apk tmp/
echo "📐 zipalign"  && "$BT34/zipalign.exe" -f -p 4 app-unsigned.apk app-aligned.apk
echo "🔏 sign v2"  && "$JAVA" -Xmx1024M -Xss1m -jar "$BT34/lib/apksigner.jar" sign \
                       --ks debug.keystore --ks-pass pass:android --ks-key-alias debug \
                       --v1-signing-enabled true --v2-signing-enabled true --out app-signed.apk app-aligned.apk
echo "✔ verify"    && "$JAVA" -jar "$BT34/lib/apksigner.jar" verify --min-sdk-version 26 app-signed.apk

cp app-signed.apk "$DESKTOP/AI_Hear.apk"
echo ""
echo "✅ 完成 → $DESKTOP/AI_Hear.apk ($(du -h app-signed.apk | cut -f1))"
