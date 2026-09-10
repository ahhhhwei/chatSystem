find_package(CURL REQUIRED)

find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h REQUIRED)
find_library(HIREDIS_LIBRARY NAMES hiredis REQUIRED)
find_path(MYSQLCLIENT_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql REQUIRED)
find_library(MYSQLCLIENT_LIBRARY NAMES mysqlclient REQUIRED)
find_path(RABBITMQ_INCLUDE_DIR amqp.h REQUIRED)
find_library(RABBITMQ_LIBRARY NAMES rabbitmq REQUIRED)

add_library(chat_infrastructure
    src/elasticsearch.cc
    src/etcd.cc
    src/http.cc
    src/mysql.cc
    src/rabbitmq.cc
    src/redis.cc
)
target_include_directories(chat_infrastructure
    PUBLIC include
    PRIVATE
        ${HIREDIS_INCLUDE_DIR}
        ${MYSQLCLIENT_INCLUDE_DIR}
        ${RABBITMQ_INCLUDE_DIR}
)
target_link_libraries(chat_infrastructure
    PUBLIC
        CURL::libcurl
        OpenSSL::Crypto
        Threads::Threads
        nlohmann_json::nlohmann_json
    PRIVATE
        ${HIREDIS_LIBRARY}
        ${MYSQLCLIENT_LIBRARY}
        ${RABBITMQ_LIBRARY}
)
