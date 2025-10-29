#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <limits.h>

#define PORT 5050
#define BUFFER_SIZE 4096
#define MAX_PATH_LENGTH 2048

void url_decode(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (src[0] == '%' && src[1] && src[2]) {
            int hex_val;
            sscanf(src + 1, "%2x", &hex_val);
            *dst++ = hex_val;
            src += 3;
        } else if (src[0] == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

const char* get_mime_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (ext == NULL) return "application/octet-stream";
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "text/html";
    else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    else if (strcmp(ext, ".png") == 0)
        return "image/png";
    else if (strcmp(ext, ".gif") == 0)
        return "image/gif";
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

int is_safe_path(const char *base_path, const char *requested_path) {
    char full_path[MAX_PATH_LENGTH];
    if (snprintf(full_path, sizeof(full_path), "%s/%s", base_path, requested_path) >= (int)sizeof(full_path)) {
        return 0;
    }
    
    char resolved_base[MAX_PATH_LENGTH];
    char resolved_full[MAX_PATH_LENGTH];
    
    if (realpath(base_path, resolved_base) == NULL) return 0;
    if (realpath(full_path, resolved_full) == NULL) return 0;
    
    return strncmp(resolved_full, resolved_base, strlen(resolved_base)) == 0;
}

void send_error(int client_sock, int status_code, const char *status_msg, const char *message) {
    char response[BUFFER_SIZE];
    int length = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><head><title>%d %s</title></head>\n"
        "<body><h1>%d %s</h1><p>%s</p></body></html>\n",
        status_code, status_msg, status_code, status_msg, status_code, status_msg, message);
    
    send(client_sock, response, length, 0);
}

void send_file(int client_sock, const char *full_path, const char *filename) {
    FILE *file = fopen(full_path, "rb");
    if (file == NULL) {
        send_error(client_sock, 404, "Not Found", "Arquivo não encontrado");
        return;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    const char *mime_type = get_mime_type(filename);
    
    char header[BUFFER_SIZE];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime_type, file_size);
    
    send(client_sock, header, header_len, 0);
    
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(client_sock, buffer, bytes_read, 0);
    }
    
    fclose(file);
}

void send_directory_listing(int client_sock, const char *base_path, const char *request_path) {
    DIR *dir = opendir(base_path);
    if (dir == NULL) {
        send_error(client_sock, 403, "Forbidden", "Acesso negado ao diretório");
        return;
    }
    
    char header[] = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n";
    
    send(client_sock, header, strlen(header), 0);
    
    char html_start[BUFFER_SIZE];
    int html_len = snprintf(html_start, sizeof(html_start),
        "<html><head><title>Listagem do Diretório</title></head>\n"
        "<body><h1>Listagem do Diretório: %s</h1>\n"
        "<table border='1' style='border-collapse: collapse;'>\n"
        "<tr><th>Nome</th><th>Tipo</th><th>Tamanho</th><th>Modificado</th></tr>\n",
        request_path);
    
    send(client_sock, html_start, html_len, 0);
    
    struct dirent *entry;
    struct stat file_stat;
    char full_path[MAX_PATH_LENGTH];
    char time_str[64];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        if (snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name) >= (int)sizeof(full_path)) {
            continue;
        }
        
        if (stat(full_path, &file_stat) == 0) {
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", 
                     localtime(&file_stat.st_mtime));
            
            char row[BUFFER_SIZE];
            const char *type = S_ISDIR(file_stat.st_mode) ? "DIR" : "FILE";
            off_t file_size = file_stat.st_size;
            
            char link_path[MAX_PATH_LENGTH];
            if (snprintf(link_path, sizeof(link_path), "%s/%s", 
                         strcmp(request_path, "/") == 0 ? "" : request_path, 
                         entry->d_name) >= (int)sizeof(link_path)) {
                continue;
            }
            
            int row_len;
            if (S_ISDIR(file_stat.st_mode)) {
                row_len = snprintf(row, sizeof(row),
                    "<tr><td><a href='%s/'>%s/</a></td><td>%s</td><td>-</td><td>%s</td></tr>\n",
                    link_path, entry->d_name, type, time_str);
            } else {
                char size_str[30];
                if (file_size < 1024) {
                    snprintf(size_str, sizeof(size_str), "%ld bytes", file_size);
                } else if (file_size < 1024 * 1024) {
                    snprintf(size_str, sizeof(size_str), "%.1f KB", file_size / 1024.0);
                } else {
                    snprintf(size_str, sizeof(size_str), "%.1f MB", file_size / (1024.0 * 1024.0));
                }
                row_len = snprintf(row, sizeof(row),
                    "<tr><td><a href='%s'>%s</a></td><td>%s</td><td>%s</td><td>%s</td></tr>\n",
                    link_path, entry->d_name, type, size_str, time_str);
            }
            
            if (row_len < (int)sizeof(row)) {
                send(client_sock, row, row_len, 0);
            }
        }
    }
    
    closedir(dir);
    
    char html_end[] = "</table></body></html>\n";
    send(client_sock, html_end, strlen(html_end), 0);
}

void handle_request(int client_sock, const char *base_directory) {
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received <= 0) {
        close(client_sock);
        return;
    }
    
    buffer[bytes_received] = '\0';
    
    if (strncmp(buffer, "GET ", 4) != 0) {
        send_error(client_sock, 405, "Method Not Allowed", "Apenas método GET é suportado");
        close(client_sock);
        return;
    }
    
    char *path_start = buffer + 4;
    char *path_end = strchr(path_start, ' ');
    if (path_end == NULL) {
        send_error(client_sock, 400, "Bad Request", "Requisição malformada");
        close(client_sock);
        return;
    }
    
    *path_end = '\0';
    char requested_path[MAX_PATH_LENGTH];
    
    strncpy(requested_path, path_start, sizeof(requested_path) - 1);
    requested_path[sizeof(requested_path) - 1] = '\0';
    url_decode(requested_path);
    
    if (strcmp(requested_path, "/") == 0) {
        strcpy(requested_path, "");
    } else {
        memmove(requested_path, requested_path + 1, strlen(requested_path));
    }
    
    char full_path[MAX_PATH_LENGTH];
    if (snprintf(full_path, sizeof(full_path), "%s/%s", base_directory, requested_path) >= (int)sizeof(full_path)) {
        send_error(client_sock, 414, "URI Too Long", "Caminho muito longo");
        close(client_sock);
        return;
    }
    
    if (!is_safe_path(base_directory, requested_path)) {
        send_error(client_sock, 403, "Forbidden", "Acesso ao caminho negado");
        close(client_sock);
        return;
    }
    
    struct stat path_stat;
    if (stat(full_path, &path_stat) != 0) {
        send_error(client_sock, 404, "Not Found", "Arquivo ou diretório não encontrado");
        close(client_sock);
        return;
    }
    
    if (S_ISDIR(path_stat.st_mode)) {
        char index_path[MAX_PATH_LENGTH];
        if (snprintf(index_path, sizeof(index_path), "%s/index.html", full_path) >= (int)sizeof(index_path)) {
            send_error(client_sock, 500, "Internal Server Error", "Caminho muito longo");
            close(client_sock);
            return;
        }
        
        if (access(index_path, F_OK) == 0) {
            send_file(client_sock, index_path, "index.html");
        } else {
            send_directory_listing(client_sock, full_path, requested_path);
        }
    } else {
        send_file(client_sock, full_path, requested_path);
    }
    
    close(client_sock);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <diretório>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s /home/flavio/meusite\n", argv[0]);
        return 1;
    }
    
    const char *base_directory = argv[1];
    
    struct stat dir_stat;
    if (stat(base_directory, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode)) {
        fprintf(stderr, "Erro: Diretório '%s' não existe ou não é acessível\n", base_directory);
        return 1;
    }
    
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Erro ao criar socket");
        return 1;
    }
    
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Erro ao configurar socket");
        close(server_sock);
        return 1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro no bind");
        close(server_sock);
        return 1;
    }
    
    if (listen(server_sock, 10) < 0) {
        perror("Erro no listen");
        close(server_sock);
        return 1;
    }
    
    printf("Servidor HTTP rodando em http://localhost:%d\n", PORT);
    printf("Servindo arquivos do diretório: %s\n", base_directory);
    printf("Pressione Ctrl+C para parar o servidor\n");
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("Erro ao aceitar conexão");
            continue;
        }
        
        printf("Conexão aceita de %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        handle_request(client_sock, base_directory);
    }
    
    close(server_sock);
    return 0;
}