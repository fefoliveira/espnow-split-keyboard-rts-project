# Teclado Split com ESP-NOW

## 1. O que e o projeto

Este projeto foi desenvolvido para a disciplina de **Sistemas de Tempo Real** do curso **Engenharia de Computação** da **Universidade Estadual do Rio Grande do Sul (UERGS)** e
tem como objetivo criar um prototipo de teclado dividido (_split keyboard_)
utilizando duas placas ESP32.

Cada placa representa uma metade do teclado. Ao longo do desenvolvimento, a
comunicacao sem fio entre os dois nos e implementada com o protocolo ESP-NOW:
o no esquerdo identifica o acionamento das teclas e envia os eventos ao no
direito, responsavel por recebe-los e processa-los.

O firmware e desenvolvido com o framework ESP-IDF e utiliza recursos do
FreeRTOS, como tarefas e temporizacao.

## 2. Etapas de desenvolvimento e branches

O historico do projeto foi organizado em branches que representam diferentes
etapas de experimentacao e desenvolvimento:

| Branch              | Objetivo                                                                                                                                            |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| `main`              | Disponibilizar a base do projeto ESP-IDF, os arquivos de configuracao e um programa simples para validar as placas e o ambiente de desenvolvimento. |
| `feature/esp-now`   | Experimentar e validar a comunicacao ESP-NOW entre os dois ESP32, com o no esquerdo enviando mensagens e o no direito recebendo-as.                 |
| `feature/left-node` | Testar a leitura dos primeiros botoes do no esquerdo e o envio dos eventos de tecla para o no direito.                                              |

Para acessar uma etapa especifica, utilize:

```bash
git switch <nome-da-branch>
```

Por exemplo:

```bash
git switch feature/esp-now
```

## 3. Objetivo da branch `main`

Esta e a branch base do projeto. Ela contem a estrutura minima de uma aplicacao
ESP-IDF e serve como ponto de partida para as implementacoes presentes nas
demais branches.

Seus principais elementos sao:

- `CMakeLists.txt`: define o projeto e integra o sistema de build do ESP-IDF;
- `main/CMakeLists.txt`: registra o componente principal e o arquivo
  `main/main.c`;
- `main/main.c`: executa uma tarefa simples que imprime uma mensagem e um
  contador no monitor serial a cada segundo;
- `sdkconfig.defaults.esp1` e `sdkconfig.defaults.esp2`: mantem configuracoes
  separadas para as duas metades do teclado;
- `Makefile`: fornece comandos para compilar, gravar e monitorar cada ESP32 de
  forma independente.

Nesta etapa ainda nao existe comunicacao ESP-NOW nem leitura de botoes. A
execucao prevista e gravar o mesmo programa de Hello World nas duas placas. Os
comandos `make esp1` e `make esp2` utilizam o mesmo `main.c`, portanto ambas as
ESPs executam o mesmo loop, imprimindo uma mensagem e um contador a cada
segundo.

O foco desta branch nao e diferenciar o comportamento das placas, mas validar
a estrutura de monorepo: a partir do mesmo codigo fonte, o projeto mantem
configuracoes, builds, portas seriais e comandos independentes para ESP1 e
ESP2. Assim, os dois firmwares podem ser compilados e executados em paralelo
sem que os artefatos de uma placa sobrescrevam os da outra.

### Estrutura do projeto

A estrutura principal encontrada nesta branch e:

```text
.
|-- CMakeLists.txt              # Configuracao principal de build do projeto ESP-IDF
|-- Makefile                    # Comandos para compilar, gravar e monitorar cada ESP32
|-- README.md                   # Documentacao geral do projeto e desta branch
|-- main/                       # Componente principal da aplicacao
|   |-- CMakeLists.txt          # Registra os arquivos fonte do componente main
|   `-- main.c                  # Ponto de entrada; imprime um contador no monitor serial
|-- sdkconfig.defaults.esp1     # Configuracoes iniciais do ESP1, a metade esquerda
|-- sdkconfig.defaults.esp2     # Configuracoes iniciais do ESP2, a metade direita
|-- sdkconfig.esp1              # Configuracao completa gerada pelo ESP-IDF para o ESP1
|-- sdkconfig.esp2              # Configuracao completa gerada pelo ESP-IDF para o ESP2
|-- build/                      # Build padrao gerado ao executar idf.py diretamente
|-- build-esp1/                 # Artefatos de compilacao exclusivos do ESP1
`-- build-esp2/                 # Artefatos de compilacao exclusivos do ESP2
```

Os diretorios de build e os arquivos `sdkconfig` completos sao gerados pelo
ESP-IDF. Por isso, eles podem nao existir logo apos clonar o repositorio e
aparecem conforme os comandos de compilacao sao executados.

O `Makefile` mantem configuracoes e builds separados para impedir que os
artefatos de uma placa sobrescrevam os da outra. Os diretorios de build nao
contem codigo fonte e podem ser recriados a qualquer momento.

## 4. Como executar esta branch

Por padrao, o `Makefile` considera:

- ESP-IDF em `~/esp/v5.5-rc1/esp-idf/export.sh`;
- ESP1 na porta `/dev/ttyUSB0`;
- ESP2 na porta `/dev/ttyUSB1`;
- alvo de compilacao `esp32`.

Caso sua instalacao esteja em outro local, informe o caminho ao executar o
comando:

```bash
make esp1 IDF_EXPORT="$HOME/esp/esp-idf/export.sh"
```

### Compilar, gravar e monitorar as duas placas

Os comandos abaixo compilam e gravam o mesmo programa de Hello World nas duas
ESPs. Cada comando utiliza um diretorio de build e um arquivo `sdkconfig`
proprio.

Em um terminal, execute para a primeira placa:

```bash
make esp1
```

Em outro terminal, execute para a segunda placa:

```bash
make esp2
```

Os dois comandos podem permanecer em execucao simultaneamente, cada um com seu
monitor serial. Isso demonstra a utilidade do monorepo para desenvolver as duas
metades do teclado: o codigo e mantido em um unico projeto, enquanto os builds
`build-esp1/` e `build-esp2/` permanecem isolados.

Nesta branch, os dois monitores exibem a mesma mensagem, pois o texto esta
definido uma unica vez em `main/main.c`:

```text
Hello World, ESP1... 0!
Hello World, ESP1... 1!
Hello World, ESP1... 2!
```

O identificador `ESP1` na mensagem nao significa que o comando `make esp2`
falhou. Ele aparece nas duas placas porque ambas executam exatamente o mesmo
codigo nesta etapa. Nas branches seguintes, os arquivos de configuracao
permitem selecionar comportamentos diferentes para os nos esquerdo e direito.

Se a porta serial for diferente da configuracao padrao:

```bash
make esp1 ESP1_PORT=/dev/ttyACM0
```

Para sair do monitor serial do ESP-IDF, pressione `Ctrl+]`.

Tambem e possivel executar cada etapa separadamente:

```bash
make esp1-build
make esp1-flash
make esp1-monitor
```

Os mesmos comandos estao disponiveis para a segunda placa, substituindo
`esp1` por `esp2`.
