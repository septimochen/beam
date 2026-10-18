# lsregister is internal; parse only complete bundle records with exact IDs.
function flush() {
    if (bundle && (id == "org.beam.desktop" || id == "org.beam.desktop.dev") && path ~ /^\//)
        print id "\t" path
    bundle = 0; id = ""; path = ""
}
/^-+$/ { flush(); next }
/^bundle id:/ { flush(); bundle = 1; next }
bundle && /^path:/ {
    path = $0; sub(/^path:[[:space:]]*/, "", path); sub(/ \(0x[[:xdigit:]]+\)$/, "", path)
}
bundle && /^identifier:/ {
    id = $0; sub(/^identifier:[[:space:]]*/, "", id); sub(/ \(0x[[:xdigit:]]+\)$/, "", id)
}
END { flush() }
