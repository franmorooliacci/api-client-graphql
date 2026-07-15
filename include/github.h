#ifndef GITHUB_H
#define GITHUB_H

// Sends the query to the GitHub GraphQL API, parses the result, and saves it to SQLite.
// Returns 0 on success, 1 on error
int fetch_and_store_github_data(const char *target_type, const char *identifier, const char *token, const char *db_path);

#endif
