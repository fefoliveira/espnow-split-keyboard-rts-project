# Teclado Split com ESP-NOW

## 1. O que e o projeto

Este projeto foi desenvolvido para a disciplina de **Sistemas de Tempo Real** e
tem como objetivo criar um prototipo de teclado dividido (*split keyboard*)
utilizando duas placas ESP32.

Cada placa representa uma metade do teclado. A comunicacao sem fio entre os
dois nos e implementada com o protocolo ESP-NOW: o no esquerdo deve identificar
o acionamento das teclas e enviar os eventos ao no direito, responsavel por
recebe-los e processa-los.

O firmware e desenvolvido com o framework ESP-IDF e utiliza recursos do
FreeRTOS, como tarefas, filas e temporizacao.

## 2. Etapas de desenvolvimento e branches

O historico do projeto foi organizado em branches que representam diferentes
etapas de experimentacao e desenvolvimento:

| Branch | Objetivo |
| --- | --- |
| `main` | Disponibilizar a base do projeto ESP-IDF e validar builds independentes para as duas placas dentro do mesmo repositorio. |
| `feature/esp-now` | Experimentar e validar a comunicacao ESP-NOW entre os dois ESP32, com o no esquerdo enviando mensagens e o no direito recebendo-as. |
| `feature/left-node` | Testar a leitura dos primeiros botoes do no esquerdo e o envio dos eventos de tecla para o no direito. |

Para acessar uma etapa especifica, utilize:

```bash
git switch <nome-da-branch>
```

Por exemplo:

```bash
git switch feature/esp-now
```

## 3. Objetivo da branch `feature/esp-now`

Esta branch implementa a primeira comunicacao sem fio entre as duas metades do
teclado. O objetivo e validar o funcionamento do ESP-NOW antes de adicionar a
leitura dos botoes e o envio de eventos reais de tecla.

Cada placa recebe um firmware diferente a partir do mesmo repositorio:

- **ESP1 - no esquerdo:** atua como transmissor e envia uma mensagem ESP-NOW em
  broadcast a cada dois segundos;
- **ESP2 - no direito:** atua como receptor, exibe os dados recebidos no monitor
  serial e pisca o LED conectado ao GPIO 2 por 100 ms a cada pacote recebido.

A mensagem enviada contem:

- um contador sequencial;
- o tempo de atividade do transmissor em milissegundos;
- um texto no formato `Mensagem ESP-NOW #N`.

As duas placas utilizam Wi-Fi no modo Station, canal 1, sem precisar se conectar
a um roteador. O envio e feito para o endereco broadcast
`FF:FF:FF:FF:FF:FF`, sem criptografia ou pareamento unicast.

Os arquivos `sdkconfig.defaults.esp1` e `sdkconfig.defaults.esp2` selecionam em
tempo de compilacao qual no sera iniciado. O mesmo `main.c` chama
`left_node_start()` para o ESP1 ou `right_node_start()` para o ESP2.

### Estrutura do projeto

A estrutura principal encontrada nesta branch e:

```text
.
|-- CMakeLists.txt                       # Configuracao principal de build do ESP-IDF
|-- Makefile                             # Comandos de build, gravacao e monitor das duas placas
|-- README.md                            # Documentacao geral do projeto e desta branch
|-- components/
|   `-- shared_protocol/
|       `-- CMakeLists.txt               # Componente reservado para o protocolo compartilhado
|-- main/                                # Componente principal da aplicacao
|   |-- CMakeLists.txt                   # Registra fontes e dependencias de Wi-Fi, ESP-NOW e GPIO
|   |-- Kconfig.projbuild                # Permite selecionar o no esquerdo ou direito
|   |-- main.c                           # Inicializa o sistema e inicia o no configurado
|   |-- left_node/
|   |   |-- left_node.c                  # Transmissor ESP-NOW e tarefa de envio periodico
|   |   `-- left_node.h                  # Interface publica do no esquerdo
|   `-- right_node/
|       |-- right_node.c                 # Receptor ESP-NOW, logs e acionamento do LED
|       `-- right_node.h                 # Interface publica do no direito
|-- sdkconfig.defaults.esp1              # Seleciona o ESP1 como no esquerdo
|-- sdkconfig.defaults.esp2              # Seleciona o ESP2 como no direito
|-- sdkconfig.esp1                       # Configuracao completa gerada para o ESP1
|-- sdkconfig.esp2                       # Configuracao completa gerada para o ESP2
|-- build/                               # Build padrao criado pelo idf.py, quando utilizado
|-- build-esp1/                          # Artefatos de compilacao exclusivos do transmissor
`-- build-esp2/                          # Artefatos de compilacao exclusivos do receptor
```

Os diretorios de build e os arquivos `sdkconfig` completos sao gerados pelo
ESP-IDF. Por isso, eles podem nao existir logo apos clonar o repositorio e
aparecem conforme os comandos de compilacao sao executados.

O `Makefile` mantem configuracoes e builds separados para impedir que o
firmware do transmissor sobrescreva o firmware do receptor. Isso permite
compilar, gravar e monitorar as duas placas em paralelo dentro do mesmo
repositorio.

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

### Compilar, gravar e monitorar as duas placas

Conecte as duas placas ao computador. Em um terminal, compile e grave o
firmware transmissor no ESP1:

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

No monitor do ESP1, a saida esperada e semelhante a:

```text
I (...) APP_MAIN: Configured as LEFT half
I (...) LEFT_NODE: Starting LEFT half (ESP-NOW transmitter)
I (...) LEFT_NODE: Sending message: "Mensagem ESP-NOW #0"
I (...) LEFT_NODE: Message sent: counter=0, uptime_ms=..., text="Mensagem ESP-NOW #0"
I (...) LEFT_NODE: Send status to FF:FF:FF:FF:FF:FF = SUCCESS
```

No monitor do ESP2, a saida esperada e semelhante a:

```text
I (...) APP_MAIN: Configured as RIGHT half
I (...) RIGHT_NODE: Starting RIGHT half (ESP-NOW receiver)
I (...) RIGHT_NODE: Receiver ready. Waiting for ESP-NOW packets...
I (...) RIGHT_NODE: Received ... bytes from XX:XX:XX:XX:XX:XX
I (...) RIGHT_NODE: Message payload: counter=0, uptime_ms=..., text="Mensagem ESP-NOW #0"
```

A cada mensagem recebida, o LED do ESP2 conectado ao GPIO 2 deve acender por
aproximadamente 100 ms. Algumas placas podem utilizar outro pino para o LED
integrado; nesse caso, altere `ONBOARD_LED_GPIO` em
`main/right_node/right_node.c`.

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

Nesta etapa, o teste e considerado bem-sucedido quando o ESP1 informa o envio
com sucesso, o ESP2 registra o mesmo contador e texto recebidos, e o LED do
receptor pisca a cada nova mensagem.
