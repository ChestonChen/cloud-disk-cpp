CREATE TABLE IF NOT EXISTS users (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    username VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    display_name VARCHAR(128) NOT NULL,
    storage_used BIGINT NOT NULL DEFAULT 0,
    storage_limit BIGINT NOT NULL DEFAULT 1073741824,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS file_objects (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    sha256 CHAR(64) NOT NULL UNIQUE,
    size_bytes BIGINT NOT NULL,
    storage_path VARCHAR(512) NOT NULL,
    ref_count BIGINT NOT NULL DEFAULT 1,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_file_objects_sha256 (sha256)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS files (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    user_id BIGINT NOT NULL,
    parent_id BIGINT NOT NULL DEFAULT 0,
    object_id BIGINT NULL,
    name VARCHAR(255) NOT NULL,
    size_bytes BIGINT NOT NULL DEFAULT 0,
    is_dir BOOLEAN NOT NULL DEFAULT FALSE,
    is_deleted BOOLEAN NOT NULL DEFAULT FALSE,
    deleted_at DATETIME NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_files_user FOREIGN KEY (user_id) REFERENCES users(id),
    CONSTRAINT fk_files_object FOREIGN KEY (object_id) REFERENCES file_objects(id),
    INDEX idx_files_user_parent (user_id, parent_id, is_deleted),
    INDEX idx_files_user_name (user_id, name),
    INDEX idx_files_object (object_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS upload_sessions (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    upload_id CHAR(36) NOT NULL UNIQUE,
    user_id BIGINT NOT NULL,
    parent_id BIGINT NULL,
    filename VARCHAR(255) NOT NULL,
    sha256 CHAR(64) NOT NULL,
    size_bytes BIGINT NOT NULL,
    chunk_size BIGINT NOT NULL,
    total_chunks INT NOT NULL,
    status ENUM('uploading', 'completed', 'cancelled', 'expired') NOT NULL DEFAULT 'uploading',
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    expires_at DATETIME NOT NULL,
    CONSTRAINT fk_upload_sessions_user FOREIGN KEY (user_id) REFERENCES users(id),
    INDEX idx_upload_sessions_user_status (user_id, status),
    INDEX idx_upload_sessions_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS upload_chunks (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    upload_id CHAR(36) NOT NULL,
    chunk_index INT NOT NULL,
    size_bytes BIGINT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_upload_chunk (upload_id, chunk_index),
    INDEX idx_upload_chunks_upload_id (upload_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS shares (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    share_token CHAR(16) NOT NULL UNIQUE,
    access_code VARCHAR(32) NOT NULL DEFAULT '',
    user_id BIGINT NOT NULL,
    file_id BIGINT NOT NULL,
    expires_at DATETIME NULL,
    allow_download BOOLEAN NOT NULL DEFAULT TRUE,
    view_count BIGINT NOT NULL DEFAULT 0,
    download_count BIGINT NOT NULL DEFAULT 0,
    is_active BOOLEAN NOT NULL DEFAULT TRUE,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_shares_user FOREIGN KEY (user_id) REFERENCES users(id),
    CONSTRAINT fk_shares_file FOREIGN KEY (file_id) REFERENCES files(id),
    INDEX idx_shares_user_active (user_id, is_active),
    INDEX idx_shares_token (share_token)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

