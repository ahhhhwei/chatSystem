CREATE TABLE IF NOT EXISTS users (
    user_id VARCHAR(128) NOT NULL,
    nickname VARCHAR(128) NOT NULL,
    phone VARCHAR(32) NULL,
    record MEDIUMBLOB NOT NULL,
    updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (user_id),
    UNIQUE KEY uk_users_nickname (nickname),
    UNIQUE KEY uk_users_phone (phone),
    KEY idx_users_updated_at (updated_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS friend_applications (
    event_id VARCHAR(128) NOT NULL,
    applicant_id VARCHAR(128) NOT NULL,
    respondent_id VARCHAR(128) NOT NULL,
    pair_key VARCHAR(257) NOT NULL,
    created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (event_id),
    UNIQUE KEY uk_friend_application_pair (pair_key),
    KEY idx_friend_application_respondent (respondent_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS friend_relations (
    first_user_id VARCHAR(128) NOT NULL,
    second_user_id VARCHAR(128) NOT NULL,
    created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (first_user_id, second_user_id),
    KEY idx_friend_relation_second (second_user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS chat_sessions (
    session_id VARCHAR(128) NOT NULL,
    session_name VARCHAR(255) NOT NULL DEFAULT '',
    session_type TINYINT UNSIGNED NOT NULL,
    created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (session_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS chat_session_members (
    session_id VARCHAR(128) NOT NULL,
    user_id VARCHAR(128) NOT NULL,
    PRIMARY KEY (session_id, user_id),
    KEY idx_chat_member_user (user_id, session_id),
    CONSTRAINT fk_chat_member_session FOREIGN KEY (session_id)
        REFERENCES chat_sessions(session_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS messages (
    message_id VARCHAR(128) NOT NULL,
    session_id VARCHAR(128) NOT NULL,
    sender_id VARCHAR(128) NOT NULL,
    message_time BIGINT NOT NULL,
    text_content TEXT NULL,
    record MEDIUMBLOB NOT NULL,
    created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (message_id),
    KEY idx_message_session_time (session_id, message_time, message_id),
    FULLTEXT KEY ft_message_text (text_content)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
