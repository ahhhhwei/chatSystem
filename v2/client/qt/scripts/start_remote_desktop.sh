#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../../../.." && pwd)"

display_id="${CHAT_QT_DISPLAY:-:99}"
screen_spec="${CHAT_QT_SCREEN:-1440x900x24}"
vnc_port="${CHAT_QT_VNC_PORT:-5901}"
novnc_port="${CHAT_QT_NOVNC_PORT:-6080}"
client_binary="${CHAT_QT_BINARY:-${repo_root}/v2/build/client/qt/chat_desktop}"

if [[ ! "${display_id}" =~ ^:[0-9]+([.][0-9]+)?$ ]]; then
    echo "CHAT_QT_DISPLAY 必须类似 :99" >&2
    exit 2
fi
if [[ ! "${vnc_port}" =~ ^[0-9]+$ ]]; then
    echo "CHAT_QT_VNC_PORT 必须是数字" >&2
    exit 2
fi
if [[ ! "${novnc_port}" =~ ^[0-9]+$ ]]; then
    echo "CHAT_QT_NOVNC_PORT 必须是数字" >&2
    exit 2
fi
if [[ ! -x "${client_binary}" ]]; then
    echo "没有找到 Qt 客户端：${client_binary}" >&2
    echo "请先执行 cmake --build ${repo_root}/v2/build -j2 --target chat_desktop" >&2
    exit 1
fi

display_number="${display_id#:}"
display_socket_number="${display_number%%.*}"
runtime_dir="/tmp/ahwei-chat-v2-display-${display_number}"
mkdir -p "${runtime_dir}"

process_is_alive() {
    local pid_file="$1"
    local marker="$2"
    local pid
    [[ -s "${pid_file}" ]] || return 1
    pid="$(<"${pid_file}")"
    [[ "${pid}" =~ ^[0-9]+$ ]] || return 1
    kill -0 "${pid}" 2>/dev/null || return 1
    [[ -r "/proc/${pid}/cmdline" ]] || return 1
    tr '\0' ' ' < "/proc/${pid}/cmdline" | grep -Fq -- "${marker}"
}

start_background() {
    local name="$1"
    local marker="$2"
    shift 2
    local pid_file="${runtime_dir}/${name}.pid"
    local log_file="${runtime_dir}/${name}.log"
    if process_is_alive "${pid_file}" "${marker}"; then
        return
    fi
    rm -f -- "${pid_file}"
    nohup "$@" >"${log_file}" 2>&1 &
    echo "$!" >"${pid_file}"
}

if ! DISPLAY="${display_id}" xdpyinfo >/dev/null 2>&1; then
    x_lock="/tmp/.X${display_socket_number}-lock"
    x_socket="/tmp/.X11-unix/X${display_socket_number}"
    if [[ -r "${x_lock}" ]]; then
        read -r stale_pid < "${x_lock}" || true
        stale_pid="${stale_pid//[[:space:]]/}"
        if [[ "${stale_pid}" =~ ^[0-9]+$ ]] && ! kill -0 "${stale_pid}" 2>/dev/null; then
            rm -f -- "${x_lock}" "${x_socket}"
        fi
    fi
    start_background xvfb "Xvfb ${display_id}" \
        Xvfb "${display_id}" -screen 0 "${screen_spec}" -nolisten tcp -ac
    for _ in $(seq 1 50); do
        if DISPLAY="${display_id}" xdpyinfo >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done
fi

if ! DISPLAY="${display_id}" xdpyinfo >/dev/null 2>&1; then
    echo "虚拟显示器 ${display_id} 启动失败，请查看 ${runtime_dir}/xvfb.log" >&2
    exit 1
fi

start_background openbox "openbox" \
    env DISPLAY="${display_id}" openbox-session
start_background x11vnc "x11vnc" \
    x11vnc -display "${display_id}" -localhost -rfbport "${vnc_port}" \
        -forever -shared -nopw -noxdamage
if command -v websockify >/dev/null 2>&1 && [[ -d /usr/share/novnc ]]; then
    start_background novnc "websockify" \
        websockify --web=/usr/share/novnc \
            "127.0.0.1:${novnc_port}" "127.0.0.1:${vnc_port}"
fi
start_background chat_desktop "chat_desktop" \
    env DISPLAY="${display_id}" QT_X11_NO_MITSHM=1 "${client_binary}" "$@"

echo "Ahwei Chat 已启动在 ${display_id}（${screen_spec}）"
echo "VNC 只监听服务器本机 127.0.0.1:${vnc_port}"
echo "本地建立隧道：ssh -L ${vnc_port}:127.0.0.1:${vnc_port} <服务器用户>@<服务器地址>"
echo "然后用 VNC Viewer 打开 127.0.0.1:${vnc_port}"
if process_is_alive "${runtime_dir}/novnc.pid" "websockify"; then
    echo "也可以转发端口 ${novnc_port}，浏览器打开 http://127.0.0.1:${novnc_port}/vnc.html?autoconnect=1&resize=scale"
fi
echo "运行日志：${runtime_dir}"
