# Teclado Split com ESP-NOW

## 1. O que e o projeto

Este projeto foi desenvolvido para a disciplina de **Sistemas de Tempo Real** e
tem como objetivo criar um prototipo de teclado dividido (*split keyboard*)
utilizando duas placas ESP32.

Cada placa representa uma metade do teclado. O no esquerdo realiza a leitura
das teclas e envia os eventos ao no direito por meio do protocolo ESP-NOW. O no
direito recebe esses eventos e, nas proximas etapas, sera responsavel por
processa-los como entradas de um teclado.

O firmware e desenvolvido com o framework ESP-IDF e utiliza recursos do
FreeRTOS, como tarefas periodicas, temporizacao e callbacks.

## 2. Etapas de desenvolvimento e branches

O historico do projeto foi organizado em branches que representam diferentes
etapas de experimentacao e desenvolvimento:

| Branch | Objetivo |
| --- | --- |
| `main` | Disponibilizar a base do projeto ESP-IDF e validar builds independentes para as duas placas dentro do mesmo repositorio. |
| `feature/esp-now` | Experimentar e validar a comunicacao ESP-NOW entre os dois ESP32 com mensagens periodicas. |
| `feature/left-node` | Ler os primeiros botoes do no esquerdo e enviar eventos reais de pressionamento e soltura ao no direito. |

Para acessar uma etapa especifica, utilize:

```bash
git switch <nome-da-branch>
```

Por exemplo:

```bash
git switch feature/left-node
```

## 3. Objetivo da branch `feature/left-node`

Esta branch implementa a primeira leitura de teclas fisicas do teclado. O ESP1
atua como no esquerdo, monitora dois botoes e envia um evento ESP-NOW sempre que
uma tecla e pressionada ou solta.

O mapeamento atual e:

| Tecla | GPIO |
| --- | --- |
| A | GPIO25 |
| B | GPIO26 |

Os GPIOs utilizam resistores de pull-up internos. Cada botao deve ser conectado
entre o respectivo GPIO e o GND:

```text
GPIO25 --- botao A --- GND
GPIO26 --- botao B --- GND
```

Com essa ligacao, o GPIO permanece em nivel alto enquanto o botao esta solto e
passa para nivel baixo quando ele e pressionado.

O no esquerdo executa uma tarefa FreeRTOS que verifica os botoes a cada 1 ms,
resultando em uma frequencia de polling de 1000 Hz. Uma mudanca precisa
permanecer estavel por 5 ms para ser aceita, reduzindo eventos incorretos
causados pelo efeito mecanico de *bounce* dos botoes.

Quando uma mudanca valida e detectada, o ESP1 envia uma estrutura `key_event_t`
em broadcast:

```c
typedef struct {
    keyboard_key_id_t key_id;
    bool is_pressed;
} key_event_t;
```

O evento informa qual tecla mudou e se ela foi pressionada ou solta. Os
identificadores `KEY_A` e `KEY_B` usam os valores correspondentes aos codigos
de teclado USB HID, preparando o protocolo para etapas posteriores.

O ESP2 atua como no direito e receptor. Ele valida o tamanho do pacote,
interpreta o evento compartilhado e registra no monitor serial o identificador,
o nome e o novo estado da tecla.

As duas placas utilizam Wi-Fi no modo Station e canal 1. O envio continua sendo
feito para o endereco broadcast `FF:FF:FF:FF:FF:FF`, sem criptografia ou
pareamento unicast.

### Estrutura do projeto

A estrutura principal encontrada nesta branch e:

```text
.
|-- CMakeLists.txt                       # Configuracao principal de build do ESP-IDF
|-- Makefile                             # Comandos de build, gravacao e monitor das duas placas
|-- README.md                            # Documentacao geral do projeto e desta branch
|-- components/
|   `-- shared_protocol/                 # Componente compartilhado pelos dois firmwares
|       |-- CMakeLists.txt               # Exporta o diretorio de cabecalhos do protocolo
|       `-- include/
|           `-- protocol.h               # IDs das teclas e formato do evento enviado
|-- main/                                # Componente principal da aplicacao
|   |-- CMakeLists.txt                   # Registra fontes, dependencias e protocolo compartilhado
|   |-- Kconfig.projbuild                # Permite selecionar o no esquerdo ou direito
|   |-- main.c                           # Inicializa o sistema e inicia o no configurado
|   |-- left_node/
|   |   |-- left_node.c                  # Le GPIO25/GPIO26, aplica debounce e envia eventos
|   |   `-- left_node.h                  # Interface publica do no esquerdo
|   `-- right_node/
|       |-- right_node.c                 # Recebe, interpreta e registra os eventos de tecla
|       `-- right_node.h                 # Interface publica do no direito
|-- sdkconfig.defaults.esp1              # Seleciona o no esquerdo e tick do FreeRTOS em 1000 Hz
|-- sdkconfig.defaults.esp2              # Seleciona o no direito e tick do FreeRTOS em 1000 Hz
|-- sdkconfig.esp1                       # Configuracao completa gerada para o ESP1
|-- sdkconfig.esp2                       # Configuracao completa gerada para o ESP2
|-- build/                               # Build padrao criado pelo idf.py, quando utilizado
|-- build-esp1/                          # Artefatos de compilacao exclusivos do no esquerdo
`-- build-esp2/                          # Artefatos de compilacao exclusivos do no direito
```

Os diretorios de build e os arquivos `sdkconfig` completos sao gerados pelo
ESP-IDF. Por isso, eles podem nao existir logo apos clonar o repositorio e
aparecem conforme os comandos de compilacao sao executados.

O componente `shared_protocol` garante que transmissor e receptor utilizem a
mesma definicao de teclas e o mesmo formato de pacote. O `Makefile`, por sua
vez, mantem configuracoes e builds separados para os dois firmwares dentro do
mesmo repositorio.

## 4. Como executar esta branch

Por padrao, o `Makefile` considera:

- ESP-IDF em `~/esp/v5.5-rc1/esp-idf/export.sh`;
- ESP1, no esquerdo, na porta `/dev/ttyUSB0`;
- ESP2, no direito, na porta `/dev/ttyUSB1`;
- alvo de compilacao `esp32`.

Caso sua instalacao do ESP-IDF esteja em outro local, informe o caminho ao
executar o comando:

```bash
make esp1 IDF_EXPORT="$HOME/esp/esp-idf/export.sh"
```

### Montar os botoes

Com as placas desligadas, conecte:

```text
ESP1 GPIO25 --- botao A --- GND
ESP1 GPIO26 --- botao B --- GND
```

Nao e necessario adicionar resistores externos para este teste, pois o firmware
habilita os resistores de pull-up internos do ESP32.

### Compilar, gravar e monitorar as duas placas

Conecte as duas placas ao computador. Em um terminal, compile e grave o
firmware do no esquerdo no ESP1:

```bash
make esp1
```

Em outro terminal, compile e grave o firmware receptor no ESP2:

```bash
make esp2
```

Os comandos tambem podem ser executados pelos aliases:

```bash
make left
make right
```

Cada comando utiliza seu proprio `sdkconfig`, diretorio de build e porta serial.
Os dois monitores podem permanecer abertos simultaneamente.

Ao iniciar o ESP1, a saida esperada inclui:

```text
I (...) APP_MAIN: Configured as LEFT half
I (...) LEFT_NODE: Starting LEFT scanner: KEY_A=GPIO25, KEY_B=GPIO26, polling=1000 Hz
I (...) main_task: Returned from app_main()
```

O retorno de `app_main()` e normal. A tarefa `left_button_scan_task` continua
executando e monitorando os GPIOs.

Ao iniciar o ESP2, a saida esperada inclui:

```text
I (...) APP_MAIN: Configured as RIGHT half
I (...) RIGHT_NODE: Starting RIGHT half (ESP-NOW key event receiver)
I (...) RIGHT_NODE: Receiver ready. Waiting for key events...
I (...) main_task: Returned from app_main()
```

O ESP2 continua ativo por meio do callback de recepcao do ESP-NOW mesmo depois
do retorno de `app_main()`.

Ao pressionar e soltar o botao A, o monitor do ESP2 deve exibir:

```text
I (...) RIGHT_NODE: Key event: id=0x04 (KEY_A), state=PRESSED
I (...) RIGHT_NODE: Key event: id=0x04 (KEY_A), state=RELEASED
```

Para o botao B, a saida esperada e:

```text
I (...) RIGHT_NODE: Key event: id=0x05 (KEY_B), state=PRESSED
I (...) RIGHT_NODE: Key event: id=0x05 (KEY_B), state=RELEASED
```

Se as portas seriais forem diferentes das configuracoes padrao:

```bash
make esp1 ESP1_PORT=/dev/ttyACM0
make esp2 ESP2_PORT=/dev/ttyACM1
```

Para sair do monitor serial do ESP-IDF, pressione `Ctrl+]`.

Tambem e possivel executar cada etapa separadamente:

```bash
make esp1-build
make esp1-flash
make esp1-monitor

make esp2-build
make esp2-flash
make esp2-monitor
```

Nesta etapa, o teste e considerado bem-sucedido quando cada pressionamento e
soltura dos botoes A e B gera exatamente um evento correspondente no monitor
serial do ESP2.
