#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
v2_dir="$(cd "${script_dir}/../.." && pwd)"
repo_dir="$(cd "${v2_dir}/.." && pwd)"
build_dir="${v2_dir}/build"
run_dir="${v2_dir}/run/v1"
log_dir="${run_dir}/logs"
mkdir -p "${log_dir}"

# VS Code Remote and corporate shells often inject HTTP(S)_PROXY.  All V1
# dependencies used by this launcher are loopback services and must not be
# routed through that proxy.  Export the bypass so the child services inherit
# it as well (notably their Elasticsearch HTTP clients).
export NO_PROXY="127.0.0.1,localhost${NO_PROXY:+,${NO_PROXY}}"
export no_proxy="127.0.0.1,localhost${no_proxy:+,${no_proxy}}"

"${script_dir}/bootstrap_v1_infrastructure.sh"

wait_for_elasticsearch() {
    local endpoint="http://127.0.0.1:9200/"
    local attempt
    for attempt in $(seq 1 60); do
        if curl --noproxy 127.0.0.1 --fail --silent --max-time 2 \
            "${endpoint}" >/dev/null 2>&1; then
            echo "Elasticsearch is ready."
            return 0
        fi
        sleep 1
    done
    echo "Elasticsearch did not become ready at ${endpoint} within 60 seconds." >&2
    echo "Check it with: systemctl status elasticsearch --no-pager" >&2
    return 1
}

wait_for_elasticsearch

start_one() {
    local name=$1
    local executable=$2
    local port=$3
    shift 3
    local pid_file="${run_dir}/${name}.pid"
    if [[ -f "${pid_file}" ]] && kill -0 "$(<"${pid_file}")" 2>/dev/null; then
        echo "${name} is already running (PID $(<"${pid_file}"))"
        return
    fi
    nohup "${executable}" "$@" >"${log_dir}/${name}.log" 2>&1 &
    local pid=$!
    echo "${pid}" >"${pid_file}"
    local ready=false
    for _ in $(seq 1 200); do
        if ! kill -0 "${pid}" 2>/dev/null; then break; fi
        if ss -ltn | grep -qE ":${port}[[:space:]]"; then
            ready=true
            break
        fi
        sleep 0.1
    done
    if [[ "${ready}" != true ]] || ! kill -0 "${pid}" 2>/dev/null; then
        echo "${name} failed to become ready; see ${log_dir}/${name}.log" >&2
        tail -n 30 "${log_dir}/${name}.log" >&2 || true
        exit 1
    fi
    echo "started ${name} (PID ${pid})"
}

common=(--infrastructure_mode=v1 --advertise_host=127.0.0.1 \
        --etcd_endpoint=http://127.0.0.1:2379)

cd "${repo_dir}"
start_one file_server "${build_dir}/services/file/file_server" 10002 "${common[@]}"
start_one user_server "${build_dir}/services/user/user_server" 10003 "${common[@]}"
start_one message_server "${build_dir}/services/message/message_server" 10005 "${common[@]}"
start_one friend_server "${build_dir}/services/friend/friend_server" 10006 "${common[@]}"
start_one transmit_server "${build_dir}/services/transmit/transmit_server" 10004 "${common[@]}"
start_one gateway_server "${build_dir}/services/gateway/gateway_server" \
    9000 \
    --infrastructure_mode=v1 --etcd_endpoint=http://127.0.0.1:2379 \
    --listen_address=127.0.0.1

echo "V2 full stack is running."
echo "Gateway HTTP:      http://127.0.0.1:9000"
echo "Gateway WebSocket: ws://127.0.0.1:9001"
echo "Logs: ${log_dir}"
