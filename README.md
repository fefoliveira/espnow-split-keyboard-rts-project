# ESP-NOW Split Keyboard Project (ESP-IDF)

Projeto base de teclado split com ESP32 usando ESP-IDF.

Nesta iteracao, o foco e validar apenas a comunicacao entre as duas metades via ESP-NOW:

- Left Half (ESP1): transmissor (envia ping periodico)
- Right Half (ESP2): receptor (recebe e loga pacotes)

## Arquivos principais e funcao de cada um

### Raiz do projeto

- `CMakeLists.txt`
  - Arquivo principal de build do projeto ESP-IDF.
  - Carrega o sistema CMake do IDF e declara o nome do projeto.

- `sdkconfig`
  - Configuracao ativa do projeto gerada pelo menuconfig.
  - Define opcoes de compilacao e, neste projeto, a escolha do dispositivo alvo (LEFT ou RIGHT).

- `sdkconfig.old`
  - Backup automatico da configuracao anterior do `sdkconfig`.

### Pasta `main/`

- `main/CMakeLists.txt`
  - Define o componente `main`.
  - Lista os arquivos fonte compilados (`main.c`, `left_node.c`, `right_node.c`).
  - Declara dependencias essenciais (`esp_event`, `esp_netif`, `esp_wifi`, `nvs_flash`).

- `main/Kconfig.projbuild`
  - Adiciona o menu `Keyboard Configuration` no menuconfig.
  - Contem o `choice` `Target Device` com as opcoes:
    - `Left Half (ESP1)`
    - `Right Half (ESP2)`
  - Essa escolha gera as macros de compilacao usadas no codigo:
    - `CONFIG_KEYBOARD_HALF_LEFT`
    - `CONFIG_KEYBOARD_HALF_RIGHT`

- `main/main.c`
  - Ponto de entrada da aplicacao (`app_main`).
  - Inicializa NVS (requisito da stack de Wi-Fi), `esp_netif` e event loop padrao.
  - Seleciona em compile-time qual no iniciar com base nas macros do Kconfig:
    - LEFT -> `left_node_start()`
    - RIGHT -> `right_node_start()`

- `main/left_node.h`
  - Interface publica do no esquerdo.
  - Declara `left_node_start()`.

- `main/left_node.c`
  - Implementacao do no esquerdo (transmissor ESP-NOW).
  - Inicializa Wi-Fi em modo Station.
  - Inicializa ESP-NOW e registra callback de envio.
  - Envia uma struct de ping em broadcast (`FF:FF:FF:FF:FF:FF`) a cada 2 segundos.

- `main/right_node.h`
  - Interface publica do no direito.
  - Declara `right_node_start()`.

- `main/right_node.c`
  - Implementacao do no direito (receptor ESP-NOW).
  - Inicializa Wi-Fi em modo Station.
  - Inicializa ESP-NOW e registra callback de recepcao.
  - Loga MAC de origem e dados recebidos (contador/uptime quando payload esperado).

## Fluxo de funcionamento (alto nivel)

1. Definir no menuconfig qual metade sera compilada (LEFT ou RIGHT).
2. Compilar e gravar firmware na placa correspondente.
3. LEFT envia ping periodico via ESP-NOW broadcast.
4. RIGHT recebe e imprime os pacotes no log.

## Observacoes

- Este repositorio esta na fase de teste de enlace ESP-NOW.
- Nao ha logica de matriz de teclado, debounce, keymap ou protocolo de sincronizacao de teclas ainda.
- O proximo passo natural e evoluir de broadcast para pareamento unicast com MAC conhecido e ACK/retransmissao.
