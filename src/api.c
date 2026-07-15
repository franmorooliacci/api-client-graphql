#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <microhttpd.h>
#include <cjson/cJSON.h>
#include "api.h"
#include "github.h"

static const char *g_github_token = NULL;
static const char *g_db_path = NULL;

// Global variable to control the server's lifecycle
volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

// Structure to temporarily store the POST body in memory
struct connection_info_struct {
    char *data;
    size_t size;
};

// libmicrohttpd callback to process the request
static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                            const char *url, const char *method,
                                            const char *version, const char *upload_data,
                                            size_t *upload_data_size, void **con_cls) {
    (void)cls; (void)version;

    if (strcmp(method, "POST") != 0 || strcmp(url, "/api/assets/github-graphql") != 0) {
        const char *error = "{\"error\": \"Not Found. Verify the URL and use the POST method.\"}";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(error), (void *)error, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, 404, response);
        MHD_destroy_response(response);
        return ret;
    }

    // Initializes the structure to receive the data
    if (NULL == *con_cls) {
        struct connection_info_struct *con_info = malloc(sizeof(struct connection_info_struct));
        if (NULL == con_info) return MHD_NO;
        con_info->data = NULL;
        con_info->size = 0;
        *con_cls = (void *)con_info;
        return MHD_YES;
    }

    struct connection_info_struct *con_info = *con_cls;

    // Concatenates the data as it arrives from the client
    if (*upload_data_size != 0) {
        char *new_data = realloc(con_info->data, con_info->size + *upload_data_size + 1);
        if (new_data == NULL) return MHD_NO;
        
        memcpy(new_data + con_info->size, upload_data, *upload_data_size);
        con_info->data = new_data;
        con_info->size += *upload_data_size;
        con_info->data[con_info->size] = '\0';
        
        *upload_data_size = 0;
        return MHD_YES;
    }

    // Processes the JSON once the body upload is finished
    if (con_info->data != NULL) {
        cJSON *json = cJSON_Parse(con_info->data);
        if (json) {
            cJSON *target_type = cJSON_GetObjectItem(json, "target_type");
            cJSON *identifier = cJSON_GetObjectItem(json, "identifier");

            // Validates that the fields exist and are strings
            if (cJSON_IsString(target_type) && cJSON_IsString(identifier)) {
                printf("\n--- Request ---\nType: %s\nID: %s\n", target_type->valuestring, identifier->valuestring);
               
                // Executes the fetch to GitHub and saves to SQLite
                int res = fetch_and_store_github_data(target_type->valuestring, identifier->valuestring, g_github_token, g_db_path);
               
                // Responds to the HTTP client
                const char *reply = (res == 0) ? "{\"status\": \"success\", \"message\": \"Asset successfully saved.\"}" 
                                               : "{\"status\": \"error\", \"message\": \"Failed to fetch or save GitHub data.\"}";
                int status_code = (res == 0) ? 200 : 500;
                
                struct MHD_Response *response = MHD_create_response_from_buffer(strlen(reply), (void *)reply, MHD_RESPMEM_PERSISTENT);
                MHD_add_response_header(response, "Content-Type", "application/json");
                enum MHD_Result ret = MHD_queue_response(connection, status_code, response);
                MHD_destroy_response(response);
                
                cJSON_Delete(json);
                return ret;
            }
            cJSON_Delete(json);
        }
    }

    // Error handling in case of an invalid body
    const char *bad_req = "{\"error\": \"Bad Request. Valid JSON with target_type and identifier is required.\"}";
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(bad_req), (void *)bad_req, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, 400, response);
    MHD_destroy_response(response);
    
    return ret;
}

// Cleans up memory when an HTTP request is closed
static void request_completed(void *cls, struct MHD_Connection *connection,
                              void **con_cls, enum MHD_RequestTerminationCode toe) {
    (void)cls; (void)connection; (void)toe;
    struct connection_info_struct *con_info = *con_cls;
    if (NULL != con_info) {
        if (con_info->data) free(con_info->data);
        free(con_info);
        *con_cls = NULL;
    }
}

int start_server(int port, const char *github_token, const char *db_path) {
    g_github_token = github_token;
    g_db_path = db_path;

    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, port, NULL, NULL,
                              &answer_to_connection, NULL,
                              MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
                              MHD_OPTION_END);
    
    if (NULL == daemon) {
        fprintf(stderr, "Error starting the server on port %d\n", port);
        return 1;
    }

    printf("HTTP server started successfully.\n");
    printf("Listening for POST requests on http://localhost:%d/api/assets/github-graphql\n", port);

    // Capture termination signals for graceful shutdown
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Keep the thread blocked efficiently
    while (keep_running) {
        pause(); 
    }

    printf("\nSignal received. Stopping server gracefully...\n");
    MHD_stop_daemon(daemon);
    printf("Server stopped.\n");
    return 0;
}
