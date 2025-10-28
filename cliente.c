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

// Estrutura para armazenar informações da URL
typedef struct {
    char protocol[10];
    char host[256];
    char path[1024];
    int port;  
} url_info_t;

// Função para analisar a URL
int parse_url(const char *url, url_info_t *url_info) {
    char temp[2048];
    strncpy(temp, url, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    // Verifica se a URL começa com "http://"
    if(strstr(temp, "http://")!= temp){
        fprintf(stderr, "Erro: URL deve começar com http://\n");
        return -1; 
    }

    // Define o protocolo como HTTP
    strcpy(url_info->protocol, "http");

    // Pular "http://"
    char *host_start = temp + strlen("http://");

    // Encontrar o início do path
    char *path_start = strchr(host_start, '/');
    
    // Se não houver path, define como "/"
    if (path_start == NULL) {
        strcpy(url_info->path, "/");
        strncpy(url_info->host, host_start, sizeof(url_info->host) - 1);
        url_info->host[sizeof(url_info->host) - 1] = '\0';
    } else { // Separa host e path
        strncpy(url_info->host, host_start, path_start - host_start);
        url_info->host[path_start - host_start] = '\0';
        strncpy(url_info->path, path_start, sizeof(url_info->path) - 1);
        url_info->path[sizeof(url_info->path) - 1] = '\0';
    }

    // Verifica se há uma porta especificada
    char *port_start = strchr(url_info->host, ':');
    if (port_start != NULL) {
        *port_start = '\0';
        url_info->port = atoi(port_start + 1);
    } else {
        url_info->port = 80;
    }
    return 0;    
}

// Função para extrair o nome do arquivo do path
char *get_filename(const char *path) {
    const char *filename = strrchr(path, '/');
    if (filename == NULL || strlen(filename) == 1) {
        return strdup("index.html");
    }
    return strdup(filename + 1);
}

// Função para criar conexão socket
int create_conection(const char *host, int port){
    struct hostent *server;
    struct sockaddr_in server_addr;
    
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

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro ao conectar ao servidor");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// Função para enviar requisição HTTP
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

// Função para processar a resposta HTTP
int process_http_response(int sockfd, const char *filename) {
    char buffer[BUFFER_SIZE];
    FILE *file = NULL;
    int content_started = 0;
    int content_length = -1;
    int bytes_received = 0;
    int header_ended = 0;

    // Ler o cabeçalho da resposta
    while (!header_ended) {
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes < 0) {
            perror("Erro ao receber resposta");
            return -1;
        }   
        buffer[bytes] = '\0';
    
        // Verifica o fim do cabeçalho
        char *header_end = strstr(buffer, "\r\n\r\n");
        if (header_end != NULL) {
            header_ended = 1;
            int header_size = header_end - buffer + 4;
            
            // Verifica o código de status HTTP
            if(strstr(buffer, "2020 OK") == NULL){
                fprintf(stderr, "Erro: Servidor retornou codigo de erro\n");
                printf("Resposta do servidor:\n%s\n", buffer);
                return -1;  
            }
            
            // Extrai o Content-Length, se disponível
            char *cl_header = strstr(buffer, "Content-Length:");
            if (cl_header != NULL) {
                content_length = atoi(cl_header + 16);
            }
            
            // Abre o arquivo para escrita
            file = fopen(filename, "wb");
            if (file == NULL) {
                perror("Erro ao abrir arquivo para escrita");
                return -1;
            }
            
            // Escreve o conteúdo que já foi recebido após o cabeçalho
            int content_bytes = bytes - header_size;
            if (content_bytes > 0) {
                fwrite(buffer + header_size, 1, content_bytes, file);
                bytes_received += content_bytes;
            }

        } else if(!content_started){
            if(strstr(buffer, "2020 OK") == NULL){ // Verifica o código de status HTTP
                fprintf(stderr, "Erro: Servidor retornou codigo de erro\n");
                printf("Resposta do servidor:\n%s\n", buffer);
                return -1;  
            }
        }
    } 
    
    // Continua recebendo o restante do conteúdo
    if (!header_ended){
        file = fopen(filename, "wb");
        if (file == NULL) {
            perror("Erro ao abrir arquivo para escrita");
            return -1;
        }
        
    }

    // Recebe o restante dos dados
    while (1) {
        int bytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (bytes <= 0) {
            break;  
        }

        fwrite(buffer, 1, bytes, file);
        bytes_received += bytes;

        // Para se atingir o Content-Length
        if (content_length > 0 && bytes_received >= content_length) {
            break;  
        }
    }
    
    if (file != NULL) {
        fclose(file);
    }

    printf("Arquivo '%s' baixado com sucesso (%d bytes).\n", filename, bytes_received);
    return 0;   
}

// Função principal do navegador
int meu_navegador(const char *url) {
    url_info_t url_info;
    
    // Analisar a URL
    if (parse_url(url, &url_info) != 0) {
        return -1;
    }

    // Obter o nome do arquivo
    char *filename = get_filename(url_info.path);
    if (filename == NULL) {
        fprintf(stderr, "Erro ao determinar o nome do arquivo\n");
        return -1;
    }

    printf("Conectando a %s:%d...\n", url_info.host, url_info.port);
    printf("Baixando %s...\n", url_info.path);
    printf("Salvando como %s\n", filename);

    // Criar conexão socket
    int sockfd = create_conection(url_info.host, url_info.port);
    if (sockfd < 0) {
        free(filename);
        return -1; 
    }

    // Enviar requisição HTTP
    if (send_http_request(sockfd, &url_info) != 0) {
        close(sockfd);
        free(filename);
        return -1;
    }

    // Processar a resposta HTTP
    int result = process_http_response(sockfd, filename);
    close(sockfd);
    free(filename);

    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <URL>\n", argv[0]);
        fprintf(stderr, "Exemplo: %s http://www.usj.edu.br/teste/imagem.jpg\n", argv[0]);
        return 1;
    }

    const char *url = argv[1];

    if(meu_navegador(url) != 0) {
        fprintf(stderr, "Falha ao baixar o arquivo.\n");
        return 1;
    }
    return 0;
}   