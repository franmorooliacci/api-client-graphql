#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "db.h"
#include "api.h"

// Función auxiliar para leer las variables de entorno
void load_env(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("No se encontro el archivo %s.\n", filename);
        return;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Ignora comentarios o líneas vacías
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Limpia el salto de línea al final
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        // Separa clave y valor por el signo '='
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *value = eq + 1;
            setenv(line, value, 1); // 1 = Sobrescribe si ya existe
        }
    }
    fclose(file);
}

int main() {
    printf("Iniciando api-client-github-graphql...\n");
    
    load_env(".env");
    
    const char *db_path = getenv("DB_PATH");
    if (!db_path) {
        db_path = "./db.sqlite3";
    }

    const char *port_str = getenv("PORT");
    int port = port_str ? atoi(port_str) : 8080;

    const char *github_token = getenv("GITHUB_TOKEN");
    if (!github_token || strlen(github_token) == 0) {
        fprintf(stderr, "Error: GITHUB_TOKEN no esta definido en .env\n");
        return 1;
    }

    // Inicializa la base de datos
    printf("Inicializando SQLite en: %s\n", db_path);
    if (db_init(db_path) != 0) {
        fprintf(stderr, "Error al inicializar la base de datos.\n");
        return 1;
    }
    printf("Base de datos OK.\n");

    // Inicia el servidor web (bloquea el hilo principal)
    if (start_server(port, github_token, db_path) != 0) {
        return 1;
    }

    return 0;
}
