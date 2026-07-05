#!/usr/bin/env bash
# Called by meson uninstall. Removes the Ctrl+Pause -> window-toggle-viewer
# dconf binding registered by meson-viewer-install.sh.
set -e

BINDING_NAME="window-toggle-viewer"
DCONF_BASE="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"

if ! command -v dconf >/dev/null 2>&1; then
    echo "meson-viewer-uninstall: dconf not found; skipping" >&2
    exit 0
fi

for i in $(seq 0 99); do
    name=$(dconf read "${DCONF_BASE}/custom${i}/name" 2>/dev/null || true)
    if [ "${name}" != "'${BINDING_NAME}'" ]; then
        continue
    fi
    SLOT_PATH="${DCONF_BASE}/custom${i}"
    dconf reset -f "${SLOT_PATH}/" 2>/dev/null || true
    LIST=$(dconf read "${DCONF_BASE}" 2>/dev/null || echo "@as []")
    NEW=$(echo "${LIST}" | python3 -c "
import sys, re
s = sys.stdin.read().strip()
slot = '${SLOT_PATH}/'
m = re.findall(r\"'([^']+)'\", s)
out = [p for p in m if p != slot]
if not out:
    print('@as []')
else:
    print('[' + ', '.join(\"'\" + p + \"'\" for p in out) + ']')
")
    dconf write "${DCONF_BASE}" "${NEW}"
    echo "meson-viewer-uninstall: removed ${BINDING_NAME} (slot custom${i})"
done
