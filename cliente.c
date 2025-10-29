#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096
#define MAX_REDIRECTS 5

typedef struct {
    char protocol[10];
    char host[256];
    char path[1024];
    int port;
} url_info_t;

int meu_navegador_redirect(const char *url, int redirect_count);

int parse_url(const char *url, url_info_t *url_info) {
    char temp[2048];
    strncpy(temp, url, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    if (strstr(temp, "http://") != temp) {
        fprintf(stderr, "Erro: URL deve começar com http://\n");
        return -1;
    }

    strcpy(url_info->protocol, "http");
    
    char *host_start = temp + 7;
    char *path_start = strchr(host_start, '/');
    
    if (path_start == NULL) {
        strcpy(url_info->path, "/");
        strncpy(url_info->host, host_start, sizeof(url_info->host) - 1);
        url_info->host[sizeof(url_info->host) - 1] = '\0';
    } else {
        strncpy(url_info->host, host_start, path_start - host_start);
        url_info->host[path_start - host_start] = '\0';
        strncpy(url_info->path, path_start, sizeof(url_info->path) - 1);
        url_info->path[sizeof(url_info->path) - 1] = '\0';
    }

    char *port_start = strchr(url_info->host, ':');
    if (port_start != NULL) {
        *port_start = '\0';
        url_info->port = atoi(port_start + 1);
    } else {
        url_info->port = 80;
    }

    return 0;
}

char* get_filename(const char *path) {
    const char *filename = strrchr(path, '/');
    if (filename == NULL || strlen(filename) == 1) {
        return strdup("index.html");
    }
    return strdup(filename + 1);
}

int create_connection(const char *host, int port) {
    struct hostent *server;
    struct sockaddr_in serv_addr;
    
    server = gethostbyname(host);
    if (server == NULL) {
        fprintf(stderr, "Erro: Não foi possível resolver o host %s\n", host);
        return -1;
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        return -1;
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erro ao conectar");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

int send_http_request(int sockfd, const url_info_t *url_info) {
    char request[2048];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: MeuNavegador/1.0\r\n"
             "Connection: close\r\n"
             "\r\n",
             url_info->path, url_info->host);
    
    int total_sent = 0;
    int request_len = strlen(request);
    
    while (total_sent < request_len) {
        int sent = send(sockfd, request + total_sent, request_len - total_sent, 0);
        if (sent < 0) {
            perror("Erro ao enviar requisição");
            return -1;
        }
        total_sent += sent;
    }
    
    return 0;
}

int get_http_status_code(const char *response) {
    int status;
    if (sscanf(response, "HTTP/1.1 %d", &status) != 1) {
        if (sscanf(response, "HTTP/1.0 %d", &status) != 1) {
            return -1;
        }
    }
    return status;
}

char* get_redirect_location(const char *response, int response_len) {
    char *location_start = strstr(response, "Location: ");
    if (location_start == NULL) {
        return NULL;
    }
    
    location_start += 10;
    char *location_end = strstr(location_start, "\r\n");
    if (location_end == NULL) {
        return NULL;
    }
    
    int location_len = location_end - location_start;
    char *location = malloc(location_len + 1);
    strncpy(location, location_start, location_len);
    location[location_len] = '\0';
    
    return location;
}

int process_http_response(int sockfd, const char *filename, int redirect_count) {
    if (redirect_count > MAX_REDIRECTS) {
        fprintf(stderr, "Erro: Muitos redirecionamentos\n");
        return -1;
    }

    char buffer[BUFFER_SIZE];
    FILE *file = NULL;
    int content_started = 0;
    int content_length = -1;
    int bytes_received = 0;
    int header_ended = 0;
    int status_code;
    
    while (!header_ended) {
        int bytes = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            fprintf(stderr, "Erro ao receber resposta\n");
            return -1;
        }
        
        buffer[bytes] = '\0';
        
        char *header_end = strstr(buffer, "\r\n\r\n");
        if (header_end != NULL) {
            header_ended = 1;
            int header_size = header_end - buffer + 4;
            
            status_code = get_http_status_code(buffer);
            if (status_code == -1) {
                fprintf(stderr, "Erro: Não foi possível obter código de status\n");
                return -1;
            }
            
            if (status_code == 301 || status_code == 302) {
                char *new_location = get_redirect_location(buffer, bytes);
                if (new_location == NULL) {
                    fprintf(stderr, "Erro: Redirecionamento sem cabeçalho Location\n");
                    return -1;
                }
                printf("Redirecionando para: %s\n", new_location);
                close(sockfd);
                int result = meu_navegador_redirect(new_location, redirect_count + 1);
                free(new_location);
                return result;
            }
            
            if (status_code != 200) {
                printf("Erro: Servidor retornou código %d\n", status_code);
                return -1;
            }
            
            char *cl_header = strstr(buffer, "Content-Length: ");
            if (cl_header != NULL) {
                content_length = atoi(cl_header + 16);
            }
            
            file = fopen(filename, "wb");
            if (file == NULL) {
                perror("Erro ao criar arquivo");
                return -1;
            }
            
            int content_bytes = bytes - header_size;
            if (content_bytes > 0) {
                fwrite(buffer + header_size, 1, content_bytes, file);
                bytes_received += content_bytes;
            }
            
        } else if (!content_started) {
            status_code = get_http_status_code(buffer);
            if (status_code == -1) {
                fprintf(stderr, "Erro: Não foi possível obter código de status\n");
                return -1;
            }
            
            if (status_code == 301 || status_code == 302) {
                continue;
            }
            
            if (status_code != 200) {
                printf("Erro: Servidor retornou código %d\n", status_code);
                return -1;
            }
        }
    }
    
    if (!header_ended) {
        file = fopen(filename, "wb");
        if (file == NULL) {
            perror("Erro ao criar arquivo");
            return -1;
        }
    }
    
    while (1) {
        int bytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (bytes <= 0) {
            break;
        }
        
        fwrite(buffer, 1, bytes, file);
        bytes_received += bytes;
        
        if (content_length > 0 && bytes_received >= content_length) {
            break;
        }
    }
    
    if (file != NULL) {
        fclose(file);
    }
    
    printf("Arquivo '%s' baixado com sucesso (%d bytes)\n", filename, bytes_received);
    return 0;
}

int meu_navegador_redirect(const char *url, int redirect_count) {
    url_info_t url_info;
    
    if (parse_url(url, &url_info) != 0) {
        return -1;
    }
    
    char *filename = get_filename(url_info.path);
    if (filename == NULL) {
        fprintf(stderr, "Erro ao obter nome do arquivo\n");
        return -1;
    }
    
    printf("Conectando a %s:%d...\n", url_info.host, url_info.port);
    printf("Baixando: %s\n", url_info.path);
    printf("Salvando como: %s\n", filename);
    
    int sockfd = create_connection(url_info.host, url_info.port);
    if (sockfd < 0) {
        free(filename);
        return -1;
    }
    
    if (send_http_request(sockfd, &url_info) != 0) {
        close(sockfd);
        free(filename);
        return -1;
    }
    
    int result = process_http_response(sockfd, filename, redirect_count);
    
    close(sockfd);
    free(filename);
    
    return result;
}

int meu_navegador(const char *url) {
    return meu_navegador_redirect(url, 0);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <URL>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s http://www.ufsj.edu.br/teste/imagem.jpg\n", argv[0]);
        return 1;
    }
    
    const char *url = argv[1];
    
    if (meu_navegador(url) != 0) {
        fprintf(stderr, "Erro ao baixar o arquivo\n");
        return 1;
    }
    
    return 0;
}