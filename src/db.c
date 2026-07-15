#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "db.h"

int db_init(const char *db_path) {
    sqlite3 *db;
    char *err_msg = 0;
    
    // Initializes the database
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error opening DB: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    const char *sql = 
        "CREATE TABLE IF NOT EXISTS assets ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "asset_uri TEXT UNIQUE NOT NULL, "
        "title TEXT, "
        "entity TEXT NOT NULL, "
        "provider TEXT NOT NULL, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "meta_payload TEXT);"
        "CREATE TABLE IF NOT EXISTS assets_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "asset_id INTEGER NOT NULL, "
        "changed_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "history_payload TEXT NOT NULL, "
        "FOREIGN KEY (asset_id) REFERENCES assets (id) ON DELETE CASCADE);";

    // Creates the tables
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        fprintf(stderr, "SQL error creating tables: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);
    return 0;
}

int db_upsert_asset(const char *db_path, const char *asset_uri, const char *title, const char *entity, const char *meta_payload) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return 1;
    }

    // Check if the asset exists
    const char *check_sql = "SELECT id, meta_payload FROM assets WHERE asset_uri = ?";
    sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, asset_uri, -1, SQLITE_STATIC);

    int exists = 0;
    int asset_id = 0;
    char *old_payload = NULL;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = 1;
        asset_id = sqlite3_column_int(stmt, 0);
        const unsigned char *payload_text = sqlite3_column_text(stmt, 1);
        if (payload_text) {
            old_payload = strdup((const char*)payload_text);
        }
    }
    sqlite3_finalize(stmt);

    if (exists) {
        // If it exists, compares the new JSON with the old one
        if (old_payload && strcmp(old_payload, meta_payload) != 0) {
            // If there were changes, saves the old payload in assets_history
            const char *hist_sql = "INSERT INTO assets_history (asset_id, history_payload) VALUES (?, ?)";
            sqlite3_prepare_v2(db, hist_sql, -1, &stmt, NULL);
            sqlite3_bind_int(stmt, 1, asset_id);
            sqlite3_bind_text(stmt, 2, old_payload, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            // Updates the assets table with the new payload
            const char *upd_sql = "UPDATE assets SET title = ?, meta_payload = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?";
            sqlite3_prepare_v2(db, upd_sql, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, title, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, meta_payload, -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 3, asset_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else {
        // If it does not exist, creates it
        const char *ins_sql = "INSERT INTO assets (asset_uri, title, entity, provider, meta_payload) VALUES (?, ?, ?, 'github', ?)";
        sqlite3_prepare_v2(db, ins_sql, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, asset_uri, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, title, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, entity, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, meta_payload, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    if (old_payload) free(old_payload);
    sqlite3_close(db);
    return 0;
}
