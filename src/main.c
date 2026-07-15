#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "db.h"
#include "api.h"

// Helper function to read environment variables
void load_env(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("File %s not found.\n", filename);
        return;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Ignores comments or empty lines
        if (line[0] == '#' || line[0] == '\n') continue;
       
        // Clears the trailing newline
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
       
        // Separates key and value by the '=' sign
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *value = eq + 1;
            setenv(line, value, 1); // 1 = Overwrites if it already exists
        }
    }
    fclose(file);
}

int main() {
    printf("Starting api-client-github-graphql...\n");
    
    load_env(".env");
    
    const char *db_path = getenv("DB_PATH");
    if (!db_path) {
        db_path = "./db.sqlite3";
    }

    const char *port_str = getenv("PORT");
    int port = port_str ? atoi(port_str) : 8080;

    const char *github_token = getenv("GITHUB_TOKEN");
    if (!github_token || strlen(github_token) == 0) {
        fprintf(stderr, "Error: GITHUB_TOKEN is not defined in .env\n");
        return 1;
    }

    // Initializes the database
    printf("Initializing SQLite at: %s\n", db_path);
    if (db_init(db_path) != 0) {
        fprintf(stderr, "Error initializing the database.\n");
        return 1;
    }
    printf("Database OK.\n");

    // Starts the web server (blocks the main thread)
    if (start_server(port, github_token, db_path) != 0) {
        return 1;
    }

    return 0;
}
