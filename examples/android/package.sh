#!/usr/bin/env bash
set -euo pipefail

: "${ANDROID_HOME:?Set ANDROID_HOME to your Android SDK directory}"
: "${JAVA_HOME:?Set JAVA_HOME to a JDK}"
: "${SDL3_SOURCE_DIR:?Set SDL3_SOURCE_DIR to the source used by your SDL3 build}"
project=$(cd "$(dirname "$0")/../.." && pwd)
build=$(cd "${1:-$project/build/android}" && pwd)
native="$build/examples/android/libmain.so"
tools="$ANDROID_HOME/build-tools/35.0.0"
platform="$ANDROID_HOME/platforms/android-35/android.jar"
java_sources="$SDL3_SOURCE_DIR/android-project/app/src/main/java"
out="$build/nova-triangle-debug.apk"

test -f "$native"
test -f "$platform"
test -f "$java_sources/org/libsdl/app/SDLActivity.java"
work=$(mktemp -d "$build/apk.XXXXXX")
trap 'rm -r "$work"' EXIT
mkdir -p "$work/classes" "$work/lib/arm64-v8a"

find "$java_sources" -type f -name '*.java' -print0 |
    xargs -0 "$JAVA_HOME/bin/javac" --release 8 -classpath "$platform" -d "$work/classes" \
        "$project/examples/android/NovaActivity.java"
(cd "$work/classes" && zip -qr "$work/classes.jar" .)
"$tools/d8" --min-api 28 --lib "$platform" --output "$work" "$work/classes.jar"
cp "$native" "$work/lib/arm64-v8a/libmain.so"
"$tools/aapt2" link -I "$platform" --manifest "$project/examples/android/AndroidManifest.xml" -o "$work/unsigned.apk"
(cd "$work" && zip -q -0 unsigned.apk classes.dex lib/arm64-v8a/libmain.so)
"$tools/zipalign" -P 16 -f 4 "$work/unsigned.apk" "$work/aligned.apk"

keystore="$build/nova-debug.jks"
if [[ ! -e "$keystore" ]]; then
    "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$keystore" -storepass android \
        -keypass android -alias androiddebugkey -dname 'CN=Nova Debug' -keyalg RSA -validity 10000
fi
"$tools/apksigner" sign --ks "$keystore" --ks-key-alias androiddebugkey --ks-pass pass:android \
    --out "$out" "$work/aligned.apk"
"$tools/apksigner" verify "$out"
printf '%s\n' "$out"
