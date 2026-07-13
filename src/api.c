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

// Variable global para controlar el ciclo de vida del servidor
volatile sig_atomic_t keep_running = 1;

// Manejador de la señal
void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

// Estructura para guardar el body del POST temporalmente en memoria
struct connection_info_struct {
    char *data;
    size_t size;
};

// Callback de libmicrohttpd para procesar el request
static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                            const char *url, const char *method,
                                            const char *version, const char *upload_data,
                                            size_t *upload_data_size, void **con_cls) {
    (void)cls; (void)version;

    if (strcmp(method, "POST") != 0 || strcmp(url, "/api/assets/github-graphql") != 0) {
        const char *error = "{\"error\": \"Not Found. Verifica la URL y usa el metodo POST.\"}";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(error), (void *)error, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, 404, response);
        MHD_destroy_response(response);
        return ret;
    }

    // Inicializa la estructura para recibir los datos
    if (NULL == *con_cls) {
        struct connection_info_struct *con_info = malloc(sizeof(struct connection_info_struct));
        if (NULL == con_info) return MHD_NO;
        con_info->data = NULL;
        con_info->size = 0;
        *con_cls = (void *)con_info;
        return MHD_YES;
    }

    struct connection_info_struct *con_info = *con_cls;

    // Concatena los datos a medida que llegan del cliente
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

    // Procesa el JSON una vez que terminó la carga del body
    if (con_info->data != NULL) {
        cJSON *json = cJSON_Parse(con_info->data);
        if (json) {
            cJSON *target_type = cJSON_GetObjectItem(json, "target_type");
            cJSON *identifier = cJSON_GetObjectItem(json, "identifier");

            // Valida que los campos existan y sean strings
            if (cJSON_IsString(target_type) && cJSON_IsString(identifier)) {
                printf("\n--- Request ---\nTipo: %s\nID: %s\n", target_type->valuestring, identifier->valuestring);
                
                // Ejecuta el fetch hacia GitHub y guardado en SQLite
                int res = fetch_and_store_github_data(target_type->valuestring, identifier->valuestring, g_github_token, g_db_path);
                
                // Responde al cliente HTTP
                const char *reply = (res == 0) ? "{\"status\": \"success\", \"message\": \"Asset guardado con éxito.\"}" 
                                               : "{\"status\": \"error\", \"message\": \"Fallo al obtener o guardar datos de GitHub.\"}";
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

    // Manejo de error en caso de body inválido
    const char *bad_req = "{\"error\": \"Bad Request. Se requiere JSON con target_type e identifier válidos.\"}";
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(bad_req), (void *)bad_req, MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, 400, response);
    MHD_destroy_response(response);
    
    return ret;
}

// Limpia la memoria cuando se cierra una petición HTTP
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
        fprintf(stderr, "Error al iniciar el servidor en el puerto %d\n", port);
        return 1;
    }

    printf("Servidor HTTP levantado exitosamente.\n");
    printf("Escuchando peticiones POST en http://localhost:%d/api/assets/github-graphql\n", port);

    // Capturar las señales de terminación para apagado limpio
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Mantener el hilo bloqueado de forma eficiente
    while (keep_running) {
        pause(); 
    }

    printf("\nSeñal recibida. Deteniendo servidor de forma segura...\n");
    MHD_stop_daemon(daemon);
    printf("Servidor detenido.\n");
    return 0;
}
