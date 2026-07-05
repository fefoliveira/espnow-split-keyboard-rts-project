# Teclado Split com ESP-NOW

## 1. O que e o projeto

Este projeto foi desenvolvido para a disciplina de **Sistemas de Tempo Real** e
tem como objetivo criar um prototipo de teclado dividido (_split keyboard_)
utilizando duas placas ESP32.

Cada placa representa uma metade do teclado. O no esquerdo realiza a leitura
das teclas e envia os eventos ao no direito por meio do protocolo ESP-NOW. O no
direito recebe esses eventos, consolida o estado completo e pode atuar como um
teclado Bluetooth BLE HID para o Linux.

O firmware e desenvolvido com o framework ESP-IDF e utiliza recursos do
FreeRTOS, como tarefas periodicas, temporizacao e callbacks.

## 2. Etapas de desenvolvimento e branches

O historico do projeto foi organizado em branches que representam diferentes
etapas de experimentacao e desenvolvimento. O fluxo segue uma progressao
natural de validacao de comunicacao, leitura de botoes, consolidação de estado
e, por fim, saida HID via Bluetooth:

| Branch               | Objetivo                                                                                                                 |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `main`               | Disponibilizar a base do projeto ESP-IDF e validar builds independentes para as duas placas dentro do mesmo repositorio. |
| `feature/esp-now`    | Experimentar e validar a comunicacao ESP-NOW entre os dois ESP32 com mensagens periodicas.                               |
| `feature/left-node`  | Ler os primeiros botoes do no esquerdo e enviar eventos reais de pressionamento e soltura ao no direito.                 |
| `feature/right-node` | Integrar as setas locais, uma fila FIFO central e o espelho consolidado das seis teclas no no direito.                   |
| `feature/bluetooth`  | Adaptar a saida do no direito para anunciar um teclado BLE HID e enviar os eventos diretamente para o Linux.             |

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

| Tecla | GPIO   |
| ----- | ------ |
| A     | GPIO25 |
| B     | GPIO26 |

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
causados pelo efeito mecanico de _bounce_ dos botoes.

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

O ESP2 atua como no direito e centralizador. Alem de receber os eventos remotos
de A e B, ele monitora quatro botoes locais correspondentes as setas. Eventos
remotos e locais sao inseridos em uma unica fila FIFO do FreeRTOS.

As duas placas utilizam Wi-Fi no modo Station e canal 1. O envio continua sendo
feito para o endereco broadcast `FF:FF:FF:FF:FF:FF`, sem criptografia ou
pareamento unicast.

## 4. Fase 2: fila central e estado consolidado

### Debounce compartilhado

A leitura e o debounce dos botoes dos dois nos ficam centralizados no
componente `components/button_debounce`. O componente configura entradas
ativas em nivel baixo com pull-up interno e mantem, para cada botao, o estado
estavel, o estado candidato e a quantidade de amostras consecutivas.

Os nos chamam `button_debounce_sample()` a cada 1 ms. A funcao somente produz
um `key_event_t` depois de cinco amostras iguais e quando o estado validado
realmente mudou. O no esquerdo envia esse evento por ESP-NOW; o no direito o
insere na fila central.

O no direito mantem um espelho global do estado atual das seis teclas:

```c
typedef struct {
    bool key_a;
    bool key_b;
    bool arrow_up;
    bool arrow_down;
    bool arrow_left;
    bool arrow_right;
} keyboard_state_t;
```

Uma fila FreeRTOS com capacidade para 32 instancias de `key_event_t` funciona
como arbitro cronologico. O callback ESP-NOW publica nela os eventos remotos e
a tarefa `right_local_scan_task` publica os eventos das quatro setas locais.

Somente a tarefa `keyboard_consolidation_task` altera o espelho global. Ela
permanece bloqueada em `xQueueReceive(..., portMAX_DELAY)` quando a fila esta
vazia, consome os eventos na ordem FIFO e imprime a foto completa do teclado.
Como existe apenas um escritor do espelho, nao e necessario mutex nesta fase.

O callback ESP-NOW executa no contexto da tarefa Wi-Fi de alta prioridade do
ESP-IDF, nao em uma ISR de hardware. Por isso, a publicacao correta e
`xQueueSend(..., 0)`, sem bloqueio. A API `xQueueSendFromISR` fica reservada
para interrupcoes reais.

As prioridades utilizadas no no direito sao:

| Rotina                        | Prioridade                                  |
| ----------------------------- | ------------------------------------------- |
| Callback ESP-NOW              | Contexto interno da tarefa Wi-Fi do ESP-IDF |
| `right_local_scan_task`       | `configMAX_PRIORITIES - 2`                  |
| `keyboard_consolidation_task` | 3                                           |

O scanner local usa `xTaskDelayUntil()` com periodo de 1 ms e debounce de 5
amostras consecutivas, igual ao no esquerdo.

O mapeamento fisico do no direito e:

| Tecla              | GPIO   |
| ------------------ | ------ |
| Seta para cima     | GPIO25 |
| Seta para baixo    | GPIO33 |
| Seta para esquerda | GPIO27 |
| Seta para direita  | GPIO26 |

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

## 5. Como executar esta branch

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

ESP2 GPIO25 --- botao seta para cima --- GND
ESP2 GPIO33 --- botao seta para baixo --- GND
ESP2 GPIO27 --- botao seta para esquerda --- GND
ESP2 GPIO26 --- botao seta para direita --- GND
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
I (...) RIGHT_NODE: Starting RIGHT half: UP=GPIO25 DOWN=GPIO33 LEFT=GPIO27 RIGHT=GPIO26
I (...) KEYBOARD_HID: Initializing BLE HID keyboard output
I (...) RIGHT_NODE: Central key-event queue ready
I (...) main_task: Returned from app_main()
```

O ESP2 continua ativo por meio do callback ESP-NOW e das tarefas
`right_local_scan_task` e `keyboard_consolidation_task` mesmo depois do retorno
de `app_main()`.

Com `CONFIG_KEYBOARD_OUTPUT_BLE_HID=y`, a serial do ESP2 fica apenas para logs
de diagnostico e pareamento. A saida real das teclas vai pelo Bluetooth, sem a
ponte Python. Para testar, pare o monitor se quiser, abra as configuracoes de
Bluetooth do Linux e pareie com o dispositivo `ESP Split Keyboard`.

Ao pressionar e soltar o botao A em modo serial, o monitor do ESP2 deve exibir:

```text
I (...) RIGHT_NODE: Keyboard state: A=1 B=0 UP=0 DOWN=0 LEFT=0 RIGHT=0
I (...) RIGHT_NODE: Keyboard state: A=0 B=0 UP=0 DOWN=0 LEFT=0 RIGHT=0
```

Para o botao B, a saida esperada e:

```text
I (...) RIGHT_NODE: Keyboard state: A=0 B=1 UP=0 DOWN=0 LEFT=0 RIGHT=0
I (...) RIGHT_NODE: Keyboard state: A=0 B=0 UP=0 DOWN=0 LEFT=0 RIGHT=0
```

Ao pressionar a seta para cima no ESP2:

```text
I (...) RIGHT_NODE: Keyboard state: A=0 B=0 UP=1 DOWN=0 LEFT=0 RIGHT=0
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

Nesta etapa, o teste e considerado bem-sucedido quando eventos remotos e locais
atualizam corretamente a foto consolidada das seis teclas, respeitando a ordem
FIFO da fila central.

## 6. Saida HID: Bluetooth direto ou ponte serial

O no direito possui dois modos de saida selecionados por Kconfig:

| Macro                              | Comportamento                                                                                               |
| ---------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| `CONFIG_KEYBOARD_OUTPUT_BLE_HID=y` | A ESP2 anuncia um teclado BLE chamado `ESP Split Keyboard` e envia os reports HID diretamente para o Linux. |
| `CONFIG_KEYBOARD_OUTPUT_SERIAL=y`  | A ESP2 imprime a foto consolidada na serial, mantendo compatibilidade com a ponte Python por `uinput`.      |

O arquivo `sdkconfig.defaults.esp2` esta configurado atualmente para Bluetooth:

```text
CONFIG_KEYBOARD_OUTPUT_BLE_HID=y
# CONFIG_KEYBOARD_OUTPUT_SERIAL is not set
```

Fluxo atual recomendado:

```text
ESP1 botoes A/B
    -> ESP-NOW
ESP2 right_node
    -> fila FreeRTOS
    -> espelho consolidado
    -> BLE HID
Linux
```

Para compilar e gravar a ESP2 nesse modo:

```bash
make esp2-build-flash
```

Depois, no Linux, pareie o dispositivo Bluetooth `ESP Split Keyboard`. O
`make py` nao e necessario nesse modo.

Resumo dos comandos por modo:

| Modo             | Macro ativa na ESP2                | Comando no Linux                                 |
| ---------------- | ---------------------------------- | ------------------------------------------------ |
| Bluetooth direto | `CONFIG_KEYBOARD_OUTPUT_BLE_HID=y` | Parear `ESP Split Keyboard`; nao usar `make py`. |
| Debug serial     | `CONFIG_KEYBOARD_OUTPUT_SERIAL=y`  | Usar `make py-dry` ou `sudo make py`.            |

### Ponte HID serial para Linux

Como alternativa de depuracao, o computador Linux tambem pode receber as teclas
consolidadas pela ESP2 por meio de uma ponte em Python:

```text
ESP1 botoes A/B
    -> ESP-NOW
ESP2 right_node
    -> serial USB com "Keyboard state: ..."
Python no Linux
    -> uinput
Aplicacoes do Linux
```

Essa ponte usa a serial USB da ESP2 e cria um teclado virtual no Linux com
`uinput`. Para usa-la, selecione `CONFIG_KEYBOARD_OUTPUT_SERIAL=y` no
`sdkconfig.esp2` ou via `idf.py menuconfig`.

Esse modo continua sendo util quando o pareamento Bluetooth estiver instavel,
quando for necessario enxergar exatamente o texto emitido pela ESP2 ou quando
voce quiser testar a consolidacao das teclas sem depender da pilha BLE.

Configuracao esperada para usar `make py`:

```text
# CONFIG_KEYBOARD_OUTPUT_BLE_HID is not set
CONFIG_KEYBOARD_OUTPUT_SERIAL=y
```

Depois de alterar a macro, recompile e grave a ESP2:

```bash
make esp2-build-flash
```

### Instalar dependencias

```bash
cd tools/linux_hid_bridge
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

O usuario precisa ter permissao para ler a porta serial e escrever em
`/dev/uinput`. Em caso de duvida, teste primeiro com `sudo`.

### Rodar em modo de teste

Feche o `make esp2-monitor`, porque a porta serial so pode ser usada por um
processo de cada vez. Depois execute:

```bash
python rightnode_linux_hid_bridge.py --port /dev/ttyUSB1 --dry-run --echo-serial
```

Ou, a partir da raiz do projeto:

```bash
make py-dry
```

Ao pressionar botoes, a saida deve mostrar mudancas como:

```text
[dry-run] A pressed
[dry-run] A released
[dry-run] LEFT pressed
[dry-run] LEFT released
```

### Rodar injetando teclas reais no Linux

Com o ambiente virtual ativo:

```bash
sudo .venv/bin/python rightnode_linux_hid_bridge.py --port /dev/ttyUSB1
```

Ou, a partir da raiz do projeto:

```bash
sudo make py
```

A partir desse momento, os botoes do prototipo passam a agir como um teclado
real para o Linux:

| Estado da ESP2 | Tecla emitida no Linux         |
| -------------- | ------------------------------ |
| `A=1`          | `A` pressionado                |
| `B=1`          | `B` pressionado                |
| `UP=1`         | seta para cima pressionada     |
| `DOWN=1`       | seta para baixo pressionada    |
| `LEFT=1`       | seta para esquerda pressionada |
| `RIGHT=1`      | seta para direita pressionada  |

Use `Ctrl+C` para parar a ponte. Ao encerrar, o script solta qualquer tecla que
ainda esteja marcada como pressionada no espelho local.
