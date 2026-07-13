#ifndef GITHUB_H
#define GITHUB_H

// Envia la query a GitHub GraphQL API, parsea el resultado y lo guarda en SQLite.
// Retorna 0 en caso de éxito, 1 si hay error.
int fetch_and_store_github_data(const char *target_type, const char *identifier, const char *token, const char *db_path);

#endif
