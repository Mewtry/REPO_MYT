# Documentação da esteira MYT

Este repositório reúne a documentação final organizada da esteira MYT, uma
esteira seletora por cor de finalidade didática. O material consolida documentos
técnicos, guias de uso, firmwares de teste e trabalhos acadêmicos relacionados
ao equipamento.

A esteira teve como autores iniciais do projeto Matheus Beirão Cabrera, Theo Vieira
Pires e Yasmin da Mata Georgetti Silva. Mais recentemente, o equipamento foi objeto 
do Trabalho de Conclusão de Curso de Theo Vieira Pires, também responsável por este
repositório.

O TCC foi submetido ao Instituto Federal de Educação, Ciência e Tecnologia de
Santa Catarina como parte dos requisitos para obtenção do título de Engenheiro
Mecatrônico, sob orientação do Prof. Valdir Noll, Dr. Eng.

> O arquivo do TCC disponível neste repositório ainda é uma prévia: o texto já
> está completo em termos de conteúdo, mas ainda não corresponde
> necessariamente à versão final aprovada.

## Por onde começar

| Se você quer... | Consulte |
| --- | --- |
| Usar a esteira em uma aula ou demonstração | [`Guias/GUIA_USO_ESTEIRA.md`](Guias/GUIA_USO_ESTEIRA.md) |
| Entender o contexto acadêmico do projeto | [`Trabalhos Relacionados/`](Trabalhos%20Relacionados/) |
| Ler a prévia do TCC mais recente | [`Trabalhos Relacionados/TCC-Prévia-2026.pdf`](Trabalhos%20Relacionados/TCC-Pr%C3%A9via-2026.pdf) |
| Consultar os artigos de Projeto Integrador relacionados à esteira | [`Trabalhos Relacionados/Artigo-PIN22305-2023.pdf`](Trabalhos%20Relacionados/Artigo-PIN22305-2023.pdf) e [`Trabalhos Relacionados/Artigo-PIN22406-2024.pdf`](Trabalhos%20Relacionados/Artigo-PIN22406-2024.pdf) |
| Ver esquemas elétricos das placas | [`Documentos Técnicos/Diagramas/`](Documentos%20T%C3%A9cnicos/Diagramas/) |
| Consultar componentes e montagem da placa | [`Documentos Técnicos/BOM/`](Documentos%20T%C3%A9cnicos/BOM/) |
| Abrir a BOM interativa da placa controladora | [`Documentos Técnicos/BOM/MYT CONTROL PCB_REV01-2026-05-03_18-43-15.html`](Documentos%20T%C3%A9cnicos/BOM/MYT%20CONTROL%20PCB_REV01-2026-05-03_18-43-15.html) |
| Buscar arquivos de fabricação das PCBs | [`Documentos Técnicos/Arquivos de Fabricação/`](Documentos%20T%C3%A9cnicos/Arquivos%20de%20Fabrica%C3%A7%C3%A3o/) |
| Consultar imagens das placas | [`Documentos Técnicos/Imagens/`](Documentos%20T%C3%A9cnicos/Imagens/) |
| Consultar datasheets dos componentes | [`Documentos Técnicos/Datasheets/`](Documentos%20T%C3%A9cnicos/Datasheets/) |
| Ver os firmwares de teste | [`Firmwares/`](Firmwares/) |

## Estrutura do repositório

### `Guias/`

Contém o guia operacional principal da esteira:

- [`GUIA_USO_ESTEIRA.md`](Guias/GUIA_USO_ESTEIRA.md): instruções para uso
  didático, cuidados antes da aula, seleção do microcontrolador, gravação dos
  firmwares e comandos básicos de teste.

Esse é o ponto de partida recomendado para professores, alunos ou qualquer
pessoa que queira operar a esteira sem entrar primeiro nos detalhes de projeto.

### `Documentos Técnicos/`

Reúne os arquivos usados para consulta, montagem, reprodução e manutenção das
placas eletrônicas.

- [`Diagramas/`](Documentos%20T%C3%A9cnicos/Diagramas/): PDFs dos esquemas das
  placas `MYT_PCB` e `MYT_IHM_PCB`.
- [`BOM/`](Documentos%20T%C3%A9cnicos/BOM/): lista de materiais em planilha e
  arquivos HTML de BOM interativa gerados para a placa controladora.
- [`Arquivos de Fabricação/`](Documentos%20T%C3%A9cnicos/Arquivos%20de%20Fabrica%C3%A7%C3%A3o/):
  pacotes de fabricação, listas de posicionamento, designadores, BOMs em CSV e
  backups de versões anteriores.
- [`Datasheets/`](Documentos%20T%C3%A9cnicos/Datasheets/): folhas de dados dos
  principais componentes utilizados no projeto eletrônico.
- [`Imagens/`](Documentos%20T%C3%A9cnicos/Imagens/): imagens das placas controladora e IHM.

### `Firmwares/`

Contém firmwares de teste e apoio didático para os dois microcontroladores
previstos na placa:

- [`esteira_teste_arduino_nano/`](Firmwares/esteira_teste_arduino_nano/):
  sketch para Arduino Nano, incluindo cópia local de `SoftwareSerial`.
- [`esteira_teste_esp32s3/`](Firmwares/esteira_teste_esp32s3/): sketch para
  ESP32-S3.

Esses firmwares servem como base de teste, diagnóstico e atividades didáticas.
Eles não devem ser interpretados, por si só, como uma aplicação final completa de
seleção automática por cor.

### `Trabalhos Relacionados/`

Agrupa os documentos acadêmicos que ajudam a entender a evolução do projeto:

- [`Artigo-PIN22305-2023.pdf`](Trabalhos%20Relacionados/Artigo-PIN22305-2023.pdf)
- [`Artigo-PIN22406-2024.pdf`](Trabalhos%20Relacionados/Artigo-PIN22406-2024.pdf)
- [`TCC-Prévia-2026.pdf`](Trabalhos%20Relacionados/TCC-Pr%C3%A9via-2026.pdf)

Os artigos de Projeto Integrador documentam etapas anteriores relacionadas à
esteira. A prévia do TCC documenta o retrofitting eletrônico mais recente, com
foco na organização da eletrônica de controle, interface didática, firmwares de
teste e documentação de apoio.

## Autoria e créditos

- Autores iniciais do projeto da esteira: Matheus Beirão Cabrera, Theo Vieira
  Pires e Yasmin da Mata Georgetti Silva.
- Autor do TCC e responsável por este repositório: Theo Vieira Pires.
- Orientador do TCC: Prof. Valdir Noll, Dr. Eng.
- Instituição: Instituto Federal de Educação, Ciência e Tecnologia de Santa
  Catarina.

## Observações de uso

- Use o guia em [`Guias/GUIA_USO_ESTEIRA.md`](Guias/GUIA_USO_ESTEIRA.md) antes
  de energizar ou demonstrar a esteira.
- Para montagem ou conferência da placa, use a BOM interativa, os esquemas
  elétricos e os arquivos de fabricação em conjunto.
- Para leitura acadêmica, comece pelos trabalhos em
  [`Trabalhos Relacionados/`](Trabalhos%20Relacionados/) e considere que a
  versão do TCC neste repositório ainda pode receber ajustes finais.
