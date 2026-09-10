#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
v2_dir="$(cd "${script_dir}/../.." && pwd)"
run_dir="${v2_dir}/run/v1"

for name in gateway_server transmit_server friend_server message_server user_server file_server; do
    pid_file="${run_dir}/${name}.pid"
    [[ -f "${pid_file}" ]] || continue
    pid="$(<"${pid_file}")"
    if [[ "${pid}" =~ ^[0-9]+$ ]] && kill -0 "${pid}" 2>/dev/null; then
        command_line="$(tr '\0' ' ' <"/proc/${pid}/cmdline" 2>/dev/null || true)"
        if [[ "${command_line}" == *"/v2/build/services/"*"/${name}"* ]]; then
            kill -TERM "${pid}"
            echo "stopped ${name} (PID ${pid})"
        else
            echo "ignored stale PID ${pid} for ${name}" >&2
        fi
    fi
    rm -f "${pid_file}"
done
