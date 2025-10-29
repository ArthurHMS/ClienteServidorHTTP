CC=gcc
CFLAGS=-Wall -Wextra
SERVIDOR_SRC=servidor.c
CLIENTE_SRC=cliente.c
SERVIDOR_BIN=servidor
CLIENTE_BIN=cliente
TEST_DIR=test_site

all: $(SERVIDOR_BIN) $(CLIENTE_BIN)

$(SERVIDOR_BIN): $(SERVIDOR_SRC)
	$(CC) $(CFLAGS) -o $@ $<

$(CLIENTE_BIN): $(CLIENTE_SRC)
	$(CC) $(CFLAGS) -o $@ $<

# Criar diretório de teste e arquivos necessários
setup-test:
	mkdir -p $(TEST_DIR)
	echo "<html><body><h1>Página de Teste</h1></body></html>" > $(TEST_DIR)/index.html
	echo "Arquivo de teste" > $(TEST_DIR)/test.txt
	# Criar um arquivo PDF simples
	echo "%PDF-1.1" > $(TEST_DIR)/test.pdf
	echo "1 0 obj" >> $(TEST_DIR)/test.pdf
	echo "<< /Type /Catalog /Pages 2 0 R >>" >> $(TEST_DIR)/test.pdf
	echo "endobj" >> $(TEST_DIR)/test.pdf
	echo "2 0 obj" >> $(TEST_DIR)/test.pdf
	echo "<< /Type /Pages /Kids [3 0 R] /Count 1 >>" >> $(TEST_DIR)/test.pdf
	echo "endobj" >> $(TEST_DIR)/test.pdf
	echo "3 0 obj" >> $(TEST_DIR)/test.pdf
	echo "<< /Type /Page /Parent 2 0 R >>" >> $(TEST_DIR)/test.pdf
	echo "endobj" >> $(TEST_DIR)/test.pdf
	echo "xref" >> $(TEST_DIR)/test.pdf
	echo "trailer" >> $(TEST_DIR)/test.pdf
	echo "<< /Root 1 0 R >>" >> $(TEST_DIR)/test.pdf
	echo "%%EOF" >> $(TEST_DIR)/test.pdf
	# Criar uma imagem PPM simples e converter para JPG
	( echo "P6" && echo "32 32" && echo "255" && head -c 3072 /dev/urandom ) > $(TEST_DIR)/temp.ppm
	convert $(TEST_DIR)/temp.ppm $(TEST_DIR)/test.jpg 2>/dev/null || \
	    dd if=/dev/urandom of=$(TEST_DIR)/test.jpg bs=1024 count=10 2>/dev/null
	rm -f $(TEST_DIR)/temp.ppm
	@echo "Ambiente de teste configurado em $(TEST_DIR)"

# Executar testes básicos
test: all setup-test
	@echo "Iniciando testes..."
	@echo "\n=== Testando servidor e cliente ==="
	@echo "1. Inicie o servidor em outro terminal com: ./$(SERVIDOR_BIN) $(TEST_DIR)"
	@echo "2. Execute os seguintes testes:"
	@echo "   a) Teste de arquivo HTML:"
	@echo "      ./$(CLIENTE_BIN) http://localhost:5050/"
	@echo "   b) Teste de arquivo texto:"
	@echo "      ./$(CLIENTE_BIN) http://localhost:5050/test.txt"
	@echo "   c) Teste de arquivo inexistente:"
	@echo "      ./$(CLIENTE_BIN) http://localhost:5050/naoexiste.txt"
	@echo "   d) Teste de diretório:"
	@echo "      ./$(CLIENTE_BIN) http://localhost:5050/"
	@echo "\nVerifique se:"
	@echo "- O servidor mostra logs de conexão"
	@echo "- Os arquivos são baixados corretamente"
	@echo "- Os erros são tratados adequadamente"
	@echo "- A listagem de diretório funciona"

# Limpar arquivos compilados e de teste
clean:
	rm -f $(SERVIDOR_BIN) $(CLIENTE_BIN)
	rm -f index.html test.txt test.pdf test.jpg
	@echo "Arquivos compilados removidos"

# Limpar completamente (incluindo diretório de teste)
cleanall: clean
	rm -rf $(TEST_DIR)
	@echo "Diretório de teste removido"

.PHONY: all setup-test test clean cleanall