#!/usr/bin/env bash
# Called by meson install. Registers Ctrl+Pause -> window-toggle-viewer
# in dconf, reusing an existing slot if present.
#
# The viewer is installed to ${prefix}/bin/window-toggle-viewer (default
# /usr/local/bin). DESTDIR is respected for staged installs; in that
# case dconf registration is skipped (the staged install will not hit
# the host's user dconf database anyway).
set -e

BINDING_KEY="<Primary>Pause"
BINDING_NAME="window-toggle-viewer"
DCONF_BASE="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"

# Default prefix: /usr/local matches the meson default.
PREFIX="/usr/local"
VIEWER="${PREFIX}/bin/window-toggle-viewer"

if [ -n "${DESTDIR}" ]; then
    echo "meson-viewer-install: DESTDIR=${DESTDIR} set; skipping dconf registration" >&2
    exit 0
fi

if ! command -v dconf >/dev/null 2>&1; then
    echo "meson-viewer-install: dconf not found; skipping registration" >&2
    echo "  Run ${VIEWER} manually to view bindings." >&2
    exit 0
fi

# If the viewer binary was not actually installed, skip.
if [ ! -x "${VIEWER}" ]; then
    echo "meson-viewer-install: ${VIEWER} not present; skipping" >&2
    exit 0
fi

# 三条 binding (Ctrl+Pause / Super+Pause / Alt+Pause) 让任意一个 modifier 都能弹 viewer。
# 每条用独立 slot, 在 custom-keybindings 列表里加一次。如果已有同名 slot,
# 直接写 binding 字段 (复用, 不浪费槽)。
declare -a KEYS=("<Primary>Pause" "<Super>Pause" "<Alt>Pause")
declare -a SLOTS=()

for KEY in "${KEYS[@]}"; do
    SLOT=""
    # 找一个 name=viewer 且 cmd=viewer 且 binding=KEY 的现成 slot, 直接复用
    for i in $(seq 0 99); do
        name=$(dconf read "${DCONF_BASE}/custom${i}/name" 2>/dev/null || true)
        binding=$(dconf read "${DCONF_BASE}/custom${i}/binding" 2>/dev/null || true)
        if [ "${name}" = "'${BINDING_NAME}'" ] && [ "${binding}" = "'${KEY}'" ]; then
            SLOT="${i}"
            break
        fi
    done
    # 没有现成 viewer slot 就开个新空槽
    if [ -z "${SLOT}" ]; then
        for i in $(seq 0 99); do
            name=$(dconf read "${DCONF_BASE}/custom${i}/name" 2>/dev/null || true)
            if [ -z "${name}" ] || [ "${name}" = "''" ]; then
                SLOT="${i}"
                break
            fi
        done
    fi
    if [ -z "${SLOT}" ]; then
        echo "meson-viewer-install: no free dconf custom-keybinding slot for ${KEY}" >&2
        continue
    fi
    SLOTS+=("${SLOT}")
    SLOT_PATH="${DCONF_BASE}/custom${SLOT}"
    dconf write "${SLOT_PATH}/name" "'${BINDING_NAME}'"
    dconf write "${SLOT_PATH}/command" "'${VIEWER}'"
    dconf write "${SLOT_PATH}/binding" "'${KEY}'"
    LIST=$(dconf read "${DCONF_BASE}" 2>/dev/null || echo "@as []")
    case "${LIST}" in
        *"${SLOT_PATH}/"*) ;;
        "@as []"|"") dconf write "${DCONF_BASE}" "['${SLOT_PATH}/']" ;;
        *) dconf write "${DCONF_BASE}" "${LIST%]}, '${SLOT_PATH}/']" ;;
    esac
done

echo "meson-viewer-install: registered viewer for ${#SLOTS[@]} modifier(s) -> ${VIEWER}"
