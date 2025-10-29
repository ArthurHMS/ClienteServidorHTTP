# Cliente e Servidor HTTP Simples

Este é um projeto de implementação de um cliente e servidor HTTP simples para a disciplina de Redes de Computadores.

## Estrutura do Projeto

```
.
├── cliente.c          # Código fonte do cliente HTTP
├── servidor.c         # Código fonte do servidor HTTP
├── Makefile          # Arquivo de compilação e testes
├── test_site/        # Diretório com arquivos de teste
│   ├── index.html    # Página HTML de teste
│   ├── test.pdf      # PDF de teste
│   └── test.jpg      # Imagem de teste
└── README.md         # Este arquivo
```

## Compilação

Para compilar o projeto, use o comando:

```bash
make
```

Isto irá gerar os executáveis `cliente` e `servidor`.

## Execução

### Servidor

Para iniciar o servidor:

```bash
./servidor <diretório>
```

Exemplo:
```bash
./servidor test_site
```

O servidor irá iniciar na porta 5050 e servirá os arquivos do diretório especificado.

### Cliente

Para usar o cliente:

```bash
./cliente <URL>
```

Exemplo:
```bash
./cliente http://localhost:5050/test.pdf
```

## Testes

O projeto inclui um conjunto de testes automatizados. Para executar:

1. Configure o ambiente de teste:
```bash
make setup-test
```

2. Execute os testes:
```bash
make test
```

### Casos de Teste

1. Download de arquivos:
   - HTML: `./cliente http://localhost:5050/`
   - PDF: `./cliente http://localhost:5050/test.pdf`
   - JPG: `./cliente http://localhost:5050/test.jpg`
   - TXT: `./cliente http://localhost:5050/test.txt`

2. Listagem de diretório:
   - `./cliente http://localhost:5050/`

3. Tratamento de erros:
   - Arquivo inexistente: `./cliente http://localhost:5050/naoexiste.txt`
   - URL inválida: `./cliente badurl`
   - Acesso negado: `./cliente http://localhost:5050/../fora.txt`

## Limpeza

Para limpar os arquivos compilados:
```bash
make clean
```

Para limpar tudo (incluindo diretório de teste):
```bash
make cleanall
```

## Funcionalidades

### Servidor
- Servir arquivos de qualquer tipo
- Listar conteúdo de diretórios
- Suporte a diferentes tipos MIME
- Tratamento de erros HTTP
- Proteção contra directory traversal

### Cliente
- Download de arquivos
- Suporte a redirecionamentos HTTP
- Tratamento de diferentes tipos de arquivos
- Exibição de progresso do download
- Tratamento de erros

## Limitações

- Suporte apenas ao método GET
- Sem suporte a HTTPS
- Sem conexões persistentes
- Sem suporte a compressão