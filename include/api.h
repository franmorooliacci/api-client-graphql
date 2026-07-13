#ifndef API_H
#define API_H

// Inicia el servidor HTTP en el puerto definido.
// Retorna 0 si se cerró correctamente, 1 si hubo un error al iniciar.
int start_server(int port, const char *github_token, const char *db_path);

#endif
