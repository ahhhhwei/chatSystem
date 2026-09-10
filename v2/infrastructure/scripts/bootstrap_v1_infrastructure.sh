#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
schema_file="${script_dir}/../sql/schema.sql"
db_name="${CHAT_MYSQL_DATABASE:-ahwei_chat}"
db_user="${CHAT_MYSQL_USER:-chat}"
db_password="${CHAT_MYSQL_PASSWORD:-chat_dev_only}"

if [[ ! "${db_name}" =~ ^[A-Za-z0-9_]+$ ]] ||
   [[ ! "${db_user}" =~ ^[A-Za-z0-9_]+$ ]]; then
    echo "database and user names may only contain letters, digits, and underscore" >&2
    exit 2
fi
if [[ ! "${db_password}" =~ ^[A-Za-z0-9_.!@%-]+$ ]]; then
    echo "CHAT_MYSQL_PASSWORD contains unsupported characters" >&2
    exit 2
fi

mysql --protocol=socket -uroot <<SQL
CREATE DATABASE IF NOT EXISTS \`${db_name}\`
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS '${db_user}'@'127.0.0.1'
  IDENTIFIED BY '${db_password}';
ALTER USER '${db_user}'@'127.0.0.1' IDENTIFIED BY '${db_password}';
GRANT ALL PRIVILEGES ON \`${db_name}\`.* TO '${db_user}'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL

mysql --protocol=tcp -h127.0.0.1 -u"${db_user}" \
  -p"${db_password}" "${db_name}" < "${schema_file}"

redis-cli -h 127.0.0.1 ping | grep -qx PONG
ETCDCTL_API=3 etcdctl --endpoints=http://127.0.0.1:2379 endpoint health
rabbitmq-diagnostics -q ping

echo "V1-compatible infrastructure is ready."
echo "MySQL database: ${db_name}; application user: ${db_user}"
