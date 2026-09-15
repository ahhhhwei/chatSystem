#!/usr/bin/env bash
set -euo pipefail

display_id="${CHAT_QT_DISPLAY:-:99}"
if [[ ! "${display_id}" =~ ^:[0-9]+([.][0-9]+)?$ ]]; then
    echo "CHAT_QT_DISPLAY 必须类似 :99" >&2
    exit 2
fi

display_number="${display_id#:}"
runtime_dir="/tmp/ahwei-chat-v2-display-${display_number}"

stop_one() {
    local name="$1"
    local marker="$2"
    local pid_file="${runtime_dir}/${name}.pid"
    local pid
    [[ -s "${pid_file}" ]] || return
    pid="$(<"${pid_file}")"
    if [[ "${pid}" =~ ^[0-9]+$ ]] && kill -0 "${pid}" 2>/dev/null \
        && [[ -r "/proc/${pid}/cmdline" ]] \
        && tr '\0' ' ' < "/proc/${pid}/cmdline" | grep -Fq -- "${marker}"; then
        kill "${pid}"
        for _ in $(seq 1 30); do
            kill -0 "${pid}" 2>/dev/null || break
            sleep 0.1
        done
    fi
    rm -f -- "${pid_file}"
}

stop_one chat_desktop "chat_desktop"
stop_one novnc "websockify"
stop_one x11vnc "x11vnc"
stop_one openbox "openbox"
stop_one xvfb "Xvfb ${display_id}"
echo "聊天客户端虚拟桌面 ${display_id} 已停止"
