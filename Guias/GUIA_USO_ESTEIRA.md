# Guia de uso da esteira seletora por cor

Este documento reúne as informações mínimas para uso didático da esteira
seletora por cor após o retrofitting eletrônico. O objetivo é permitir que o
professor prepare uma atividade com segurança e que o aluno consiga gravar,
executar e modificar os firmwares.

Este guia é o ponto de partida operacional do repositório. Para contexto
acadêmico, arquivos de fabricação, BOMs, datasheets e imagens das placas, use
também o [`README.md`](../README.md) da raiz.

O firmware disponibilizado aqui é um firmware de teste e apoio didático. Ele
não é a aplicação final completa da esteira, nem substitui uma prática
planejada pelo professor.

## 1. Materiais disponíveis

Repositório do projeto:

<https://github.com/Mewtry/REPO_MYT.git>

Página públic:

<https://mewtry.github.io/REPO_MYT/>

Arquivos principais desta versão:

| Material | Caminho | Uso recomendado |
| --- | --- | --- |
| BOM | [`Documentos Técnicos/BOM/BOM.xlsx`](../Documentos%20T%C3%A9cnicos/BOM/BOM.xlsx) | Consulta de componentes montados, referências e substituições. |
| Diagrama da placa controladora | [`Documentos Técnicos/Diagramas/MYT_PCB.pdf`](../Documentos%20T%C3%A9cnicos/Diagramas/MYT_PCB.pdf) | Consulta elétrica, pinagem, conectores e blocos funcionais. |
| Diagrama da placa IHM | [`Documentos Técnicos/Diagramas/MYT_IHM_PCB.pdf`](../Documentos%20T%C3%A9cnicos/Diagramas/MYT_IHM_PCB.pdf) | Consulta da interface com LCD, encoder e demais sinais da IHM. |
| BOM interativa da placa controladora | [`Documentos Técnicos/BOM/MYT CONTROL PCB_REV01-2026-05-03_18-43-15.html`](../Documentos%20T%C3%A9cnicos/BOM/MYT%20CONTROL%20PCB_REV01-2026-05-03_18-43-15.html) | Conferência visual de montagem e localização de componentes. |
| Arquivos de fabricação das PCBs | [`Documentos Técnicos/Arquivos de Fabricação/`](../Documentos%20T%C3%A9cnicos/Arquivos%20de%20Fabrica%C3%A7%C3%A3o/) | Pacotes, posições, designadores e BOMs em CSV das placas. |
| Imagens das placas | [`Documentos Técnicos/Imagens/`](../Documentos%20T%C3%A9cnicos/Imagens/) | Referência visual da placa controladora, IHM e montagem final. |
| Datasheets | [`Documentos Técnicos/Datasheets/`](../Documentos%20T%C3%A9cnicos/Datasheets/) | Consulta aos componentes usados no projeto eletrônico. |
| Firmware Arduino IDE - Arduino Nano | [`Firmwares/esteira_teste_arduino_nano/`](../Firmwares/esteira_teste_arduino_nano/) | Caminho principal para aulas introdutórias. |
| Firmware Arduino IDE - ESP32-S3 | [`Firmwares/esteira_teste_esp32s3/`](../Firmwares/esteira_teste_esp32s3/) | Caminho para aulas com maior capacidade de processamento e mais GPIOs. |
| Trabalhos acadêmicos relacionados | [`Trabalhos Relacionados/`](../Trabalhos%20Relacionados/) | Artigos e prévia do TCC para entender a evolução do projeto. |

## 2. Estado do equipamento e limitações

- A esteira ficou montada e funcional com firmware de teste para Arduino Nano e
  ESP32-S3.
- Os firmwares permitem acionar saídas, ler entradas, testar I2C, LCD, sensor de
  cor, motor CC, motor de passo e interface física RS-485.
- O firmware não implementa uma aplicação final completa de seleção por cor.
  Ele deve ser usado como base de estudo, diagnóstico e desenvolvimento pelos
  alunos.
- O RS-485 foi validado em bancada porém o transceptor precisa ser substituído.
  Portanto, não usar Modbus/RS-485 como atividade obrigatória sem
  substituição do transceptor.
- O expansor de GPIO por PCF8574AT não foi validado. Esse recurso deve ser
  tratado como adicional, não como função essencial do equipamento.
- O repositório atual disponibiliza os firmwares de teste como sketches para
  Arduino IDE em [`Firmwares/esteira_teste_arduino_nano/`](../Firmwares/esteira_teste_arduino_nano/)
  e [`Firmwares/esteira_teste_esp32s3/`](../Firmwares/esteira_teste_esp32s3/).

## 3. Cuidados antes da aula

### 3.1 Seleção do microcontrolador

![Placa controladora montada com ESP32-S3 à esquerda e Arduino Nano à direita](../Documentos%20T%C3%A9cnicos/Imagens/pcb-montagem-final-esteira.jpeg)

A placa controladora permite manter o ESP32-S3 e o Arduino Nano instalados ao
mesmo tempo, mas apenas um deles deve ser selecionado para controlar os
periféricos compartilhados. Na orientação da imagem, o ESP32-S3 fica à esquerda
e o Arduino Nano fica à direita.

A chave seletora deve ser entendida de forma direta: ela seleciona o
microcontrolador para o lado em que é forçada. Ao mover a chave para a esquerda,
o microcontrolador ativo é o ESP32-S3. Ao mover a chave para a direita, o
microcontrolador ativo é o Arduino Nano.

Antes de gravar ou executar um firmware, conferir se a chave está posicionada
para o microcontrolador correspondente. Não alterar a seleção durante uma
atividade sem antes colocar o sistema em condição segura.

### 3.2 Preparação e cuidados

Para o professor:

1. Inspecionar visualmente a esteira, a placa controladora, a IHM e os cabos.
2. Conferir se o microcontrolador desejado está selecionado antes de energizar a
   placa.
3. Usar o Arduino Nano para a primeira prática quando o objetivo for
   programação introdutória.
4. Usar a ESP32-S3 quando a prática exigir mais GPIOs ou maior capacidade de
   processamento.
5. Para testar o sistema antes da aula, usar os comandos básicos, como:
   `list`, `status`, `i2c-level` e `i2c-scan`.

Cuidados elétricos:

- Não aplicar tensão diretamente nos GPIOs dos microcontroladores.
- Para sinais externos, usar os conectores e circuitos previstos na placa.
- Não alterar a seleção de microcontrolador durante uma atividade sem antes
  colocar o sistema em condição segura.
- Tratar os testes RS-485 como diagnóstico opcional até que o MAX485 seja
  substituído por componente confiável e novamente validado.

## 4. Programação pela Arduino IDE

### 4.1 Arduino Nano

Sketch:

[`Firmwares/esteira_teste_arduino_nano/esteira_teste_arduino_nano.ino`](../Firmwares/esteira_teste_arduino_nano/esteira_teste_arduino_nano.ino)

Dependências:

- Arduino AVR Boards, instalado pela própria Arduino IDE.
- Biblioteca `hd44780`, pelo Library Manager.
- Biblioteca `PinChangeInterrupt`, pelo Library Manager.
- Arquivos locais [`SoftwareSerial.cpp`](../Firmwares/esteira_teste_arduino_nano/SoftwareSerial.cpp)
  e [`SoftwareSerial.h`](../Firmwares/esteira_teste_arduino_nano/SoftwareSerial.h),
  já incluídos na pasta do sketch.

Configuração sugerida:

1. Abrir a pasta `esteira_teste_arduino_nano` na Arduino IDE.
2. Selecionar a placa `Arduino Nano`.
3. Selecionar o processador `ATmega328P`. Se a gravação falhar, testar a opção
   de bootloader antigo.
4. Selecionar a porta serial correspondente ao Nano.
5. Gravar o sketch.
6. Abrir o Monitor Serial em `115200 bps`.
7. Usar terminador de linha compatível com comandos por texto, como `Nova linha`
   ou `Ambos NL e CR`.

Observação: este sketch inclui uma cópia local ajustada de `SoftwareSerial`.
Ela foi mantida porque o RS-485 usa recepção no pino D5, pertencente ao grupo
`PCINT2`, enquanto o sinal `TCS_OUT` do sensor de cor usa o pino D9, pertencente
ao grupo `PCINT0`. A biblioteca `SoftwareSerial` padrão do Arduino pode ocupar
vetores de interrupção por mudança de pino que também seriam usados pela leitura
do TCS por `PinChangeInterrupt`. A cópia local restringe a serial em software ao
vetor necessário para D5, preservando `PCINT0` para a contagem dos pulsos do
sensor de cor em D9. Se o sensor TCS não for usado no Nano, a biblioteca padrão
do Arduino tende a ser suficiente; neste firmware de teste, a cópia local evita
esse conflito.

### 4.2 ESP32-S3

Sketch:

[`Firmwares/esteira_teste_esp32s3/esteira_teste_esp32s3.ino`](../Firmwares/esteira_teste_esp32s3/esteira_teste_esp32s3.ino)

Dependências:

- Pacote ESP32 da Espressif instalado no gerenciador de placas da Arduino IDE.
- Biblioteca `hd44780`, pelo Library Manager.
- Biblioteca `TCS230_ESP32`, pelo Library Manager.

Configuração sugerida:

1. Abrir a pasta `esteira_teste_esp32s3` na Arduino IDE.
2. Selecionar uma placa compatível com ESP32-S3 DevKitC-1.
3. Usar flash de `16 MB`, USB CDC habilitado no boot e PSRAM desabilitada.
4. Selecionar a porta serial correspondente à ESP32-S3.
5. Gravar o sketch.
6. Abrir o Monitor Serial em `115200 bps`.
7. Usar terminador de linha compatível com comandos por texto, como `Nova linha`
   ou `Ambos NL e CR`.

## 5. Comandos básicos para a prática

Digite `help` no Monitor Serial para listar os comandos suportados pelo firmware
carregado.

Comandos recomendados para a primeira prática:

| Comando | Função |
| --- | --- |
| `help` | Lista os comandos disponíveis. |
| `list` | Lista sinais implementados e barramentos. |
| `status` | Mostra estado de saídas, entradas e contadores de borda. |
| `all-low` | Coloca as saídas digitais em nível baixo. |
| `watch on 500` | Mostra mudanças de entrada periodicamente. |
| `watch off` | Encerra o monitoramento automático. |
| `edges` | Mostra contadores de borda das entradas. |
| `clear-edges` | Zera os contadores de borda. |
| `i2c-level` | Verifica os níveis de repouso de SCL e SDA. |
| `i2c-scan` | Varre os endereços I2C. |
| `lcd-test` | Inicia teste simples do LCD. |
| `lcd-stop` | Encerra o teste do LCD. |
| `lcd-backlight on` / `lcd-backlight off` | Liga ou desliga a iluminação do LCD. |
| `tcs-read` | Realiza uma leitura do sensor de cor. |
| `tcs-stream 1000` | Mostra leituras periódicas do sensor de cor. |
| `tcs-stop` | Encerra o stream do sensor de cor. |
| `tcs-cal dark` / `tcs-cal white` / `tcs-cal reset` | Testa a rotina de calibração do sensor de cor. |
| `cc-jog 120 1000 1000 1000` | Aciona o motor CC com rampa e duty moderado. |
| `cc-stop` | Para imediatamente o teste do motor CC. |

Também existem comandos de saída direta, como `set`, `pulse` e `pulseus`,
comandos I2C de escrita, como `i2c-write` e `i2c-wave`, e comandos para sondar o
PCF8574, como `pcf-probe`, `pcf-read`, `pcf-write` e `pcf-loop`. Use esses
comandos apenas quando a atividade exigir diagnóstico mais específico e com
acompanhamento do professor.

Comandos RS-485, como `rs485-send`, `rs485-burst`, `rs485-read` e
`rs485-rx-level`, além de `rs485-di` e `rs485-clear`, devem ser tratados como
diagnóstico opcional nesta versão.

## 6. Prática mínima sugerida para o aluno

Objetivo: reconhecer as interfaces principais da esteira, gravar o firmware de
teste, observar entradas, acionar recursos básicos e relacionar comandos de
software com comportamento físico.

Sequência sugerida:

1. Abrir o sketch correspondente ao microcontrolador selecionado.
2. Conferir placa, porta serial e bibliotecas instaladas.
3. Gravar o firmware.
4. Abrir o Monitor Serial em `115200 bps`.
5. Executar `help`, `list` e `status`.
6. Girar o encoder da IHM, pressionar sua chave e observar `watch on 500`.
7. Executar `i2c-level` e `i2c-scan`.
8. Executar `lcd-test` e observar a IHM.
9. Executar `tcs-read` ou `tcs-stream 1000` e observar a variação do sensor de
   cor.
10. Com autorização do professor, executar `cc-jog 120 1000 1000 1000` e depois
    `cc-stop`.
11. Executar `all-low` antes de encerrar os testes de saída.
12. Registrar quais sinais mudaram, quais endereços I2C responderam e qual
    comportamento físico foi observado.

Atividade de modificação simples:

- Alterar uma mensagem exibida no LCD.
- Alterar o intervalo de leitura do sensor de cor.
- Alterar o duty ou os tempos do comando de motor, mantendo valores moderados e
  com supervisão.
- Acrescentar uma impressão no Monitor Serial quando uma entrada mudar.

## 7. Consulta dos demais materiais do repositório

O README da raiz organiza os materiais de apoio em três grupos principais:

- [`Documentos Técnicos/`](../Documentos%20T%C3%A9cnicos/): esquemas elétricos,
  BOM, BOM interativa, arquivos de fabricação, datasheets e imagens das placas.
- [`Firmwares/`](../Firmwares/): sketches de teste para Arduino Nano e ESP32-S3.
- [`Trabalhos Relacionados/`](../Trabalhos%20Relacionados/): artigos anteriores
  de Projeto Integrador e prévia do TCC usado como referência acadêmica do
  retrofitting eletrônico.

Para aula, comece por este guia e pelos sketches em
[`Firmwares/`](../Firmwares/). Para montagem, manutenção ou conferência elétrica,
use os esquemas, a BOM e as imagens em
[`Documentos Técnicos/`](../Documentos%20T%C3%A9cnicos/). Para entender a
evolução do projeto, consulte primeiro os arquivos em
[`Trabalhos Relacionados/`](../Trabalhos%20Relacionados/).

## 8. Como atualizar este material

Quando o equipamento receber novos recursos ou correções:

1. Atualizar primeiro o firmware validado.
2. Testar a gravação na placa real.
3. Atualizar os sketches da Arduino IDE se a mudança for relevante para alunos.
4. Atualizar este guia com novas limitações, comandos ou procedimentos.
5. Atualizar o [`README.md`](../README.md) se novos diretórios, documentos ou
   arquivos principais forem adicionados ao repositório.
