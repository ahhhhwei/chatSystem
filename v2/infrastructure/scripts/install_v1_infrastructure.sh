#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "run this installer as root (or with sudo)" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
config_dir="${script_dir}/../config"
es_version="7.17.21"
es_deb="/tmp/elasticsearch-${es_version}-amd64.deb"
es_mirror="${CHAT_ELASTICSEARCH_MIRROR:-http://mirrors.aliyun.com/elasticstack/apt/7.x/pool/main/e/elasticsearch/elasticsearch-${es_version}-amd64.deb}"
es_official="https://artifacts.elastic.co/downloads/elasticsearch/elasticsearch-${es_version}-amd64.deb"

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    mysql-server libmysqlclient-dev \
    redis-server libhiredis-dev \
    rabbitmq-server librabbitmq-dev \
    etcd-server etcd-client \
    libcurl4-openssl-dev nlohmann-json3-dev \
    default-jre-headless wget aria2 ca-certificates

if ! dpkg-query -W -f='${Status}' elasticsearch 2>/dev/null |
     grep -q 'install ok installed'; then
    aria2c --header='User-Agent: Mozilla/5.0' --continue=true \
        --max-connection-per-server=8 --split=8 \
        --min-split-size=1M --file-allocation=none --dir=/tmp \
        --out="$(basename "${es_deb}")" \
        "${es_mirror}" || \
    aria2c --continue=true --max-connection-per-server=8 --split=8 \
        --min-split-size=1M --file-allocation=none --dir=/tmp \
        --out="$(basename "${es_deb}")" "${es_official}"
    wget -O "${es_deb}.sha512" \
        "https://artifacts.elastic.co/downloads/elasticsearch/elasticsearch-${es_version}-amd64.deb.sha512"
    (cd /tmp && sha512sum -c "$(basename "${es_deb}.sha512")")
    dpkg -i "${es_deb}"
fi

install -m 0644 "${config_dir}/rabbitmq.conf" /etc/rabbitmq/rabbitmq.conf
install -m 0644 "${config_dir}/elasticsearch.yml" /etc/elasticsearch/elasticsearch.yml
install -d -m 0755 /etc/elasticsearch/jvm.options.d
install -m 0644 "${config_dir}/chat-jvm.options" \
    /etc/elasticsearch/jvm.options.d/chat.options

systemctl daemon-reload
systemctl enable --now mysql redis-server etcd rabbitmq-server elasticsearch
systemctl restart rabbitmq-server elasticsearch

"${script_dir}/bootstrap_v1_infrastructure.sh"
echo "All V1-compatible middleware has been installed on loopback interfaces."
