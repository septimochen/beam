#!/bin/bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
root=$(mktemp -d "${TMPDIR:-/tmp}/beam-release-signing.XXXXXXXX")
trap 'rm -rf -- "$root"' EXIT
export BEAM_ANDROID_SIGNING_PROPERTIES="$root/signing.properties"
gradle_args=("$@")
gradle() { "$source_dir/android/gradlew" -p "$source_dir/android" "${gradle_args[@]}" "$@"; }
reject() {
    local expected=$1
    shift
    if gradle "$@" > "$root/output" 2>&1; then
        echo 'Release unexpectedly accepted invalid signing configuration' >&2
        exit 1
    fi
    if ! grep -Fq "$expected" "$root/output"; then
        cat "$root/output" >&2
        exit 1
    fi
}

# Signed assembly must fail rather than silently producing an unsigned APK.
reject 'Release signing file is missing:' :app:assembleRelease -PbeamUnsignedRelease=false
cat > "$BEAM_ANDROID_SIGNING_PROPERTIES" <<EOF
storeFile=missing.p12
keyAlias=fixture
EOF
reject 'Release signing file is missing fields:' :app:verifyReleaseSigning -PbeamUnsignedRelease=false
cat >> "$BEAM_ANDROID_SIGNING_PROPERTIES" <<EOF
storePassword=unused-test-value
keyPassword=unused-test-value
EOF
reject 'Release keystore is missing:' :app:verifyReleaseSigning -PbeamUnsignedRelease=false

# Neither explicit unsigned validation nor debug builds need release credentials.
gradle :app:verifyReleaseSigning -PbeamUnsignedRelease=true > "$root/output" 2>&1
rm -- "$BEAM_ANDROID_SIGNING_PROPERTIES"
gradle :app:assembleDebug > "$root/output" 2>&1
echo 'Release signing validation and debug independence passed.'
