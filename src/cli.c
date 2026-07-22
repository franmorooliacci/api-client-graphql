#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <sqlite3.h>

void load_env(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return;
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            setenv(line, eq + 1, 1);
        }
    }
    fclose(file);
}

void trim_whitespace(char *str) {
    if (!str) return;
    
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    
    if (*start == 0) { 
        *str = '\0';
        return;
    }
    
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    
    memmove(str, start, end - start + 2);
}

// Structure to temporarily store the CURL response in memory
struct MemoryStruct {
    char *memory;
    size_t size;
};

// libcurl callback to process the HTTP response chunks
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

// Queries SQLite and print the JSON payload
void fetch_and_print_result(const char *db_path, const char *target_type, const char *identifier) {
    sqlite3 *db;
    
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        printf("  [!] Error opening DB to fetch results.\n");
        return;
    }

    char asset_uri[256];
    snprintf(asset_uri, sizeof(asset_uri), "github:%s:%s", target_type, identifier);
    
    const char *sql = "SELECT meta_payload FROM assets WHERE asset_uri = ?";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, asset_uri, -1, SQLITE_STATIC);
        
        // Executes the query and checks if the row exists
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *payload = sqlite3_column_text(stmt, 0);
            if (payload) {
                // Parses the raw string to format it properly
                cJSON *parsed = cJSON_Parse((const char *)payload);
                if (parsed) {
                    char *pretty = cJSON_Print(parsed);
                    printf("\n%s\n", pretty);
                    free(pretty);
                    cJSON_Delete(parsed);
                } else {
                    printf("\n%s\n", payload);
                }
            }
        } else {
            printf("  [!] Data not found in database for %s.\n", asset_uri);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

// Handles the HTTP request formatting and execution
void process_request(const char *target_in, const char *identifier, const char *db_path, int port) {
    char target_type[256];
    char api_url[256];
    
    strncpy(target_type, target_in, sizeof(target_type) - 1);
    target_type[sizeof(target_type) - 1] = '\0';
    snprintf(api_url, sizeof(api_url), "http://localhost:%d/api/assets/github-graphql", port);

    if (strcmp(target_type, "profile") != 0 && 
        strcmp(target_type, "repository") != 0 && 
        strcmp(target_type, "repo") != 0) {
        printf("  [!] Invalid target. Use 'profile' or 'repo'.\n");
        return;
    }

    if (strcmp(target_type, "repo") == 0) {
        strcpy(target_type, "repository");
    }

    cJSON *json_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(json_payload, "target_type", target_type);
    cJSON_AddStringToObject(json_payload, "identifier", identifier);
    char *post_data = cJSON_PrintUnformatted(json_payload);
    cJSON_Delete(json_payload);

    CURL *curl = curl_easy_init();
    if (curl) {
        struct MemoryStruct chunk;
        chunk.memory = malloc(1);
        chunk.size = 0;

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        // Configures and executes the cURL request to the local API
        curl_easy_setopt(curl, CURLOPT_URL, api_url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        // Validates the network response and fetches the DB result on success
        if (res != CURLE_OK) {
            printf("  [!] Connection error: %s\n", curl_easy_strerror(res));
        } else if (http_code == 200) {
            fetch_and_print_result(db_path, target_type, identifier);
        } else {
            printf("  [!] API Error (HTTP %ld): %s\n", http_code, chunk.memory);
        }

        // Memory cleanup for the current request
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(chunk.memory);
    }
    free(post_data);
}

// Main execution loop for user interaction
void run_interactive_loop(const char *db_path, int port) {
    char target_type[256];
    char identifier[256];
    
    printf("\nServer ready. Type 'exit' at any prompt to quit.\n");
    printf("--------------------------------------------------\n");
    printf("Formats expected:\n");
    printf("  Profile : username     (e.g., torvalds)\n");
    printf("  Repo    : owner/name   (e.g., torvalds/linux)\n");
    printf("--------------------------------------------------\n");
    
    while (1) {
        printf("\n> Target (profile/repo): ");
        if (!fgets(target_type, sizeof(target_type), stdin)) break;
        target_type[strcspn(target_type, "\n")] = 0;
        trim_whitespace(target_type);

        if (strlen(target_type) == 0) continue;
        if (strcmp(target_type, "exit") == 0) break;

        printf("> Identifier: ");
        if (!fgets(identifier, sizeof(identifier), stdin)) break;
        identifier[strcspn(identifier, "\n")] = 0;
        trim_whitespace(identifier);

        if (strlen(identifier) == 0) continue;
        if (strcmp(identifier, "exit") == 0) break;

        // Delegates execution to the isolated request function
        process_request(target_type, identifier, db_path, port);
    }
}

int main(int argc, char *argv[]) {
    // Validates arguments for batch vs interactive execution mode
    if (argc != 1 && argc != 3) {
        printf("Usage:\n");
        printf("  Interactive mode : %s\n", argv[0]);
        printf("  Single request   : %s <profile|repo> <identifier>\n", argv[0]);
        return 1;
    }

    load_env(".env");
    
    const char *db_path = getenv("DB_PATH");
    if (!db_path) db_path = "./db.sqlite3";
    
    const char *port_str = getenv("PORT");
    int port = port_str ? atoi(port_str) : 8080;

    curl_global_init(CURL_GLOBAL_ALL);

    // Forks the main process to run the server concurrently
    pid_t server_pid = fork();

    if (server_pid < 0) {
        perror("Fork failed");
        return 1;
    }

    if (server_pid == 0) {
        // --- CHILD PROCESS (SERVER) ---
        // Redirects stdout and stderr to /dev/null to hide background server logs
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        
        char *args[] = {"./github_server", NULL};
        execvp(args[0], args);
        exit(1);
    }

    // --- PARENT PROCESS (CLI) ---
    sleep(1); 

    if (argc == 3) {
        trim_whitespace(argv[1]);
        trim_whitespace(argv[2]);
        process_request(argv[1], argv[2], db_path, port);
    } else {
        printf("Starting background server (PID: %d)...\n", server_pid);
        printf("Server listening on http://localhost:%d/api/assets/github-graphql\n", port);
        run_interactive_loop(db_path, port);
    }

    // Send termination signal to the child process once execution concludes
    if (argc == 1) {
        printf("\nShutting down server (PID: %d)...\n", server_pid);
    }
    kill(server_pid, SIGTERM);
    
    // Wait for the child process to finish
    waitpid(server_pid, NULL, 0);
    curl_global_cleanup();
    
    return 0;
}
