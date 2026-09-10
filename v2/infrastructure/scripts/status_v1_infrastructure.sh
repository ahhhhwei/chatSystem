#!/usr/bin/env bash
set -euo pipefail

check() {
    local name=$1
    shift
    if "$@" >/dev/null 2>&1; then
        printf '%-16s %s\n' "${name}" ready
    else
        printf '%-16s %s\n' "${name}" unavailable
        return 1
    fi
}

status=0
check MySQL mysqladmin --protocol=tcp -h127.0.0.1 -uchat \
    -p"${CHAT_MYSQL_PASSWORD:-chat_dev_only}" ping || status=1
check Redis redis-cli -h 127.0.0.1 ping || status=1
check etcd env ETCDCTL_API=3 etcdctl \
    --endpoints=http://127.0.0.1:2379 endpoint health || status=1
check RabbitMQ rabbitmq-diagnostics -q ping || status=1
check Elasticsearch curl --noproxy 127.0.0.1 --fail --silent \
    http://127.0.0.1:9200/ || status=1
exit "${status}"
