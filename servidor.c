#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#define BUFFER_SIZE 4096
#define PORT 8080
#define MAX_PATH_LENGTH 1024

// Função para obter o tipo MIME baseado na extensão do arquivo
const char* get_mime_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (ext == NULL) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "text/html";
    else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    else if (strcmp(ext, ".png") == 0)
        return "image/png";
    else if (strcmp(ext, ".css") == 0)
        return "text/css";
    else if (strcmp(ext, ".js") == 0)
        return "application/javascript";
    else if (strcmp(ext, ".txt") == 0)
        return "text/plain";
    else if (strcmp(ext, ".pdf") == 0)
        return "application/pdf";
    else
        return "application/octet-stream";
}

// Função para enviar resposta de erro HTTP
void send_error(int client_sock, int status_code, const char *status_msg, const char *message) {
    char body[BUFFER_SIZE];
    int body_len = snprintf(body, sizeof(body),
        "<html><body><h1>%d %s</h1><p>%s</p></body></html>",
        status_code, status_msg, message);
    if (body_len < 0) return;

    char header[BUFFER_SIZE];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_msg, body_len);
    if (header_len < 0) return;

    send(client_sock, header, header_len, 0);
    send(client_sock, body, body_len, 0);
}

// Função para enviar arquivo
void send_file(int client_sock, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (file == NULL) {
        send_error(client_sock, 404, "Not Found", "Arquivo não encontrado.");
        return;
    }

    // Obter tamanho do arquivo
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        send_error(client_sock, 500, "Internal Server Error", "Erro ao ler o arquivo.");
        return;
    }
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    const char *mime_type = get_mime_type(filepath);

    // Enviar cabeçalho HTTP
    char header[BUFFER_SIZE];
    int header_length = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime_type, filesize);

    if (header_length < 0) {
        fclose(file);
        return;
    }

    send(client_sock, header, header_length, 0);

    // Enviar conteúdo do arquivo
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        ssize_t sent = send(client_sock, buffer, bytes_read, 0);
        if (sent < 0) {
            break; // erro de envio, fecha e retorna
        }
    }

    fclose(file);
}

// Função para gerar listagem de diretório em HTML
void send_directory_listing(int client_sock, const char *base_path, const char *request_path) {
    DIR *dir = opendir(base_path);
    if (dir == NULL) {
        send_error(client_sock, 403, "Forbidden", "Acesso ao diretório negado.");
        return;
    }

    // Iniciar resposta HTTP
    char header[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n";
    send(client_sock, header, strlen(header), 0);

    // Enviar inicio do HTML
    char html_start[BUFFER_SIZE];
    int html_len = snprintf(html_start, sizeof(html_start),
        "<html><head><title>Listagem de diretorio</title></head>\n"
        "<body><h1>Listagem de diretorio para /%s</h1>\n"
        "<table border='1' style='border-collapse: collapse;'>\n"
        "<tr><th>Nome</th><th>Tamanho (bytes)</th><th>Ultima Modificacao</th></tr>\n",
        request_path[0] == '\0' ? "" : request_path);
    if (html_len > 0) send(client_sock, html_start, html_len, 0);

    // Listar arquivos e diretórios
    struct dirent *entry;
    struct stat file_stat;
    char full_path[MAX_PATH_LENGTH];
    char time_str[64];

    while ((entry = readdir(dir)) != NULL) {
        // Ignorar "." e ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Construir caminho completo
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);

        if (stat(full_path, &file_stat) != 0) {
            continue; // não conseguiu ler info do arquivo, pula
        }

        // formatar data e hora da última modificação
        struct tm *tm_info = localtime(&file_stat.st_mtime);
        if (tm_info != NULL) {
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        } else {
            strncpy(time_str, "-", sizeof(time_str));
            time_str[sizeof(time_str)-1] = '\0';
        }

        char row[BUFFER_SIZE];
        char link_path[MAX_PATH_LENGTH];

        // Construir link relativo para o arquivo/diretório
        if (request_path[0] == '\0') {
            snprintf(link_path, sizeof(link_path), "/%s", entry->d_name);
        } else {
            snprintf(link_path, sizeof(link_path), "/%s/%s", request_path, entry->d_name);
        }

        if (S_ISDIR(file_stat.st_mode)) {
            // Diretório: mostrar "DIR" na coluna de tamanho
            int row_len = snprintf(row, sizeof(row),
                "<tr><td><a href=\"%s\">%s/</a></td><td>%s</td><td>%s</td></tr>\n",
                link_path, entry->d_name, "DIR", time_str);
            if (row_len > 0) send(client_sock, row, row_len, 0);
        } else {
            // Arquivo: mostrar tamanho em bytes
            int row_len = snprintf(row, sizeof(row),
                "<tr><td><a href=\"%s\">%s</a></td><td>%lld</td><td>%s</td></tr>\n",
                link_path, entry->d_name, (long long)file_stat.st_size, time_str);
            if (row_len > 0) send(client_sock, row, row_len, 0);
        }
    }
    closedir(dir);

    // Enviar fim do HTML
    char html_end[] = "</table></body></html>";
    send(client_sock, html_end, strlen(html_end), 0);
}

// Função para processar requisição HTTP
void handle_request(int client_sock, const char *base_directory) {
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        close(client_sock);
        return;
    }

    buffer[bytes_received] = '\0';

    // Parsear a requisição - apenas GET suportado
    if (strncmp(buffer, "GET ", 4) != 0) {
        send_error(client_sock, 405, "Method Not Allowed", "Apenas o método GET é suportado.");
        close(client_sock);
        return;
    }

    // Extrair o path do recurso solicitado
    char *path_start = buffer + 4;
    char *path_end = strchr(path_start, ' ');
    if (path_end == NULL) {
        send_error(client_sock, 400, "Bad Request", "Requisição inválida.");
        close(client_sock);
        return;
    }

    *path_end = '\0';
    char requested_path[MAX_PATH_LENGTH];

    // Decodificar URL (aqui apenas copia, sem decode %xx)
    strncpy(requested_path, path_start, sizeof(requested_path) - 1);
    requested_path[sizeof(requested_path) - 1] = '\0';

    // Normalizar path: remover '/' inicial se existir
    if (strcmp(requested_path, "/") == 0) {
        requested_path[0] = '\0';
    } else if (requested_path[0] == '/') {
        size_t len = strlen(requested_path);
        memmove(requested_path, requested_path + 1, len); // inclui o '\0'
    }

    // Construir o caminho completo do arquivo ou diretório
    char full_path[MAX_PATH_LENGTH];
    if (requested_path[0] == '\0')
        snprintf(full_path, sizeof(full_path), "%s", base_directory);
    else
        snprintf(full_path, sizeof(full_path), "%s/%s", base_directory, requested_path);

    // Verificar se é um diretório ou arquivo
    struct stat path_stat;
    if (stat(full_path, &path_stat) != 0) {
        send_error(client_sock, 404, "Not Found", "Recurso não encontrado.");
        close(client_sock);
        return;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        // É um diretório - verificar se existe index.html
        char index_path[MAX_PATH_LENGTH];
        snprintf(index_path, sizeof(index_path), "%s/index.html", full_path);

        if (access(index_path, F_OK) == 0) {
            // index.html existe - enviar o arquivo
            send_file(client_sock, index_path);
        } else {
            // Gerar listagem do diretório
            // request_path passado sem barra inicial (p.ex. "subdir" ou "")
            send_directory_listing(client_sock, full_path, requested_path);
        }
    } else {
        // É um arquivo - enviar o arquivo
        send_file(client_sock, full_path);
    }

    close(client_sock);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <diretorio>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s /home/usuario/arquivos\n", argv[0]);
        return 1;
    }

    const char *base_directory = argv[1];

    // Verificar se o diretório existe
    struct stat dir_stat;
    if (stat(base_directory, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode)) {
        fprintf(stderr, "Erro: O diretório '%s' não existe ou não é um diretório válido.\n", base_directory);
        return 1;
    }

    // Criar socket do servidor
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Erro ao criar socket do servidor");
        return 1;
    }

    // Configurar opções do socket
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Erro ao configurar opções do socket");
        close(server_sock);
        return 1;
    }

    // Configurar endereço do servidor
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Vincular o socket
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro ao vincular o socket");
        close(server_sock);
        return 1;
    }

    // Ouvir por conexões
    if (listen(server_sock, 5) < 0) {
        perror("Erro ao ouvir por conexões");
        close(server_sock);
        return 1;
    }

    printf("Servidor HTTP rodando em http://localhost:%d/\n", PORT);
    printf("Servindo arquivos do diretório: %s\n", base_directory);

    // Loop principal para aceitar conexões (serial, não-threaded)
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Erro ao aceitar conexão");
            continue;
        }

        printf("Conexão aceita de %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Processar a requisição do cliente
        handle_request(client_sock, base_directory);
    }

    close(server_sock);
    return 0;
}
        send_error(client_sock, 404, "Not Found", "Recurso não encontrado.");
        close(client_sock);
        return;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        // É um diretório - verificar se existe index.html
        char index_path[MAX_PATH_LENGTH];
        snprintf(index_path, sizeof(index_path), "%s/index.html", full_path);

        if (access(index_path, F_OK) == 0) {
            // index.html existe - enviar o arquivo
            send_file(client_sock, index_path);
        } else {
            // Gerar listagem do diretório
            // request_path passado sem barra inicial (p.ex. "subdir" ou "")
            send_directory_listing(client_sock, full_path, requested_path);
        }
    } else {
        // É um arquivo - enviar o arquivo
        send_file(client_sock, full_path);
    }

    close(client_sock);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <diretorio>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s /home/usuario/arquivos\n", argv[0]);
        return 1;
    }

    const char *base_directory = argv[1];

    // Verificar se o diretório existe
    struct stat dir_stat;
    if (stat(base_directory, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode)) {
        fprintf(stderr, "Erro: O diretório '%s' não existe ou não é um diretório válido.\n", base_directory);
        return 1;
    }

    // Criar socket do servidor
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Erro ao criar socket do servidor");
        return 1;
    }

    // Configurar opções do socket
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Erro ao configurar opções do socket");
        close(server_sock);
        return 1;
    }

    // Configurar endereço do servidor
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Vincular o socket
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro ao vincular o socket");
        close(server_sock);
        return 1;
    }

    // Ouvir por conexões
    if (listen(server_sock, 5) < 0) {
        perror("Erro ao ouvir por conexões");
        close(server_sock);
        return 1;
    }

    printf("Servidor HTTP rodando em http://localhost:%d/\n", PORT);
    printf("Servindo arquivos do diretório: %s\n", base_directory);

    // Loop principal para aceitar conexões (serial, não-threaded)
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Erro ao aceitar conexão");
            continue;
        }

        printf("Conexão aceita de %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Processar a requisição do cliente
        handle_request(client_sock, base_directory);
    }

    close(server_sock);
    return 0;
}