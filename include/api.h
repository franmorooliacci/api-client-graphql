#ifndef API_H
#define API_H

// Starts the HTTP server on the defined port.
// Returns 0 if shut down gracefully, 1 if an error occurred during startup.
int start_server(int port, const char *github_token, const char *db_path);

#endif
