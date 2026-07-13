#ifndef DB_H
#define DB_H

// Crea el archivo y las tablas si no existen.
// Retorna 0 en caso de éxito, 1 si hay error.
int db_init(const char *db_path);

// Inserta un nuevo recurso o actualiza uno existente.
// Si ya existe y 'meta_payload' es distinto, guarda la versión anterior en 'assets_history'.
// Retorna 0 en caso de éxito, 1 si hay error.
int db_upsert_asset(const char *db_path, const char *asset_uri, const char *title, const char *entity, const char *meta_payload);

#endif
