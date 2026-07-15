#ifndef DB_H
#define DB_H

// Creates the database file and tables if they do not exist.
// Returns 0 on success, 1 on error.
int db_init(const char *db_path);

// Inserts a new asset or updates an existing one.
// If it already exists and the 'meta_payload' differs, saves the previous version in 'assets_history'.
// Returns 0 on success, 1 on error.
int db_upsert_asset(const char *db_path, const char *asset_uri, const char *title, const char *entity, const char *meta_payload);

#endif
