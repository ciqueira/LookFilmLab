# Look Film Lab

[English](README.md)

Simplificar o fluxo para obter resultados consistentes. O projeto
prioriza controles reduzidos, stocks calibrados e releases estáveis para fluxos
com DaVinci Resolve, tanto na aba Color quanto na nova aba Photos. O pacote é
dividido em três partes, com funções diferentes.

As ferramentas usam uma base modificada GPL-3.0 relacionada ao
`spektrafilm-ofx`, que por sua vez é um port OpenFX nativo e uma expansão do
projeto original `spektrafilm` em Python.

Look Film Lab Plugins é distribuído pelo
[MCNexus](https://github.com/ciqueira/MCNexus). O Nexus fornece distribuição,
entrega de licenças, atualizações e suporte ao produto. O MCNexus é o aplicativo
desktop usado para ativar, instalar, atualizar e gerenciar os plugins.

## Plugins Incluídos

Esta distribuição de demonstração inclui:

| Plugin | Versão | Distribuição | Obter Chave |
| --- | --- | --- | --- |
| Look Film Lab CINE | 0.1.0 | OpenKey | [Obter Chave](https://bridge.magnociqueira.com.br/github/claim?t=spektrafilm-ofx-look&tmpl=0e0f439a-a923-463a-9871-a2e1fbff6512&sig=8a17ba59b4cf673e) |
| Look Film Lab PHOTO | 0.1.1 | OpenKey | [Obter Chave](https://bridge.magnociqueira.com.br/github/claim?t=spektrafilm-ofx-look&tmpl=b14f2962-e363-469a-9107-eea7114a2278&sig=bbf12f349303a7cb) |
| Look Film Lab SCAN | 0.1.4 | OpenKey | [Obter Chave](https://bridge.magnociqueira.com.br/github/claim?t=spektrafilm-ofx-look&tmpl=df16b079-4aff-4a31-a317-fab1c295f31f&sig=93d7d8cf4a16c6ec) |

## Look Film Lab CINE

Look Film Lab CINE é voltado a color grading para filme com stocks de cinema. O
fluxo combina seleção de negativo, seleção de print, calibração de stocks e
controles de laboratório em uma interface com menos parâmetros expostos.

O processo segue a lógica de uma cadeia fotoquímica:

- `Profile Negative`: escolhe o stock negativo usado como base.
- `Profile Print`: escolhe o material de print ou papel de saída.
- `Film Format`: altera a referência de formato para a resposta espacial.
- `Exposure EV`: ajusta a exposição antes da resposta de filme.
- `Film Push / Pull Stops`: simula variação de revelação no negativo.
- `Negative Bleach Bypass`: adiciona uma resposta neutra de retenção de prata
  no negativo.
- `Printer Light`: altera o equilíbrio de cor na etapa de impressão.
- `Print Bleach Bypass`: aplica retenção de prata na etapa de print.

O objetivo não é expor todo o modelo espectral, mas manter os controles que
mais afetam a construção do look.

## Look Film Lab PHOTO

Look Film Lab PHOTO adapta fotos digitais para características de stocks de
filme usando parte da mesma base espectral do CINE. A interface mantém seleção
de negativo, seleção de papéis usados em stocks fotográficos, push/pull e
bleach bypass em uma configuração mais direta para imagem estática.

O plugin foi pensado para a aba Photos do Resolve e para fluxos em que várias
imagens precisam manter uma direção visual consistente sem montar uma estrutura
de nodes para cada foto.

## Look Film Lab SCAN

Look Film Lab SCAN é voltado a negativos físicos escaneados. A ferramenta
trabalha com inversão da imagem para positivo, compensação de base do filme,
exposição de scan, escala de densidade por canal e escolha de papel emulado.

Controles públicos principais:

- `Film Base Color`, `Film Base Temp` e `Film Base Tint`: corrigem a base
  laranja ou variações do material escaneado.
- `Scan Exposure EV`: ajusta a exposição do scan antes da conversão.
- `Density Scale R/G/B`: equilibra a densidade dos canais.
- `Profile Print`: define a resposta de papel usada após a inversão.
- `Print Push / Pull Stops` e `Print Bleach Bypass`: ajustam a etapa final de
  print.

O fluxo preserva as características do negativo físico e aplica a resposta de
impressão em um papel simulado.

## Suporte de Plataforma

Os builds atuais suportam:

- macOS, Apple Silicon e Macs Intel compatíveis
- Windows x64

Backends de processamento suportados:

- Metal no macOS
- Vulkan nos builds de Windows correspondentes

## Instalação

Cada plugin Look Film Lab possui sua própria licença OpenKey de demonstração.

1. Use o link `Obter Chave` correspondente na tabela acima.
2. Autorize com uma conta GitHub.
3. Copie a chave de licença emitida.
4. Abra o MCNexus.
5. Ative o plugin correspondente com essa chave.
6. Instale ou atualize o plugin pelo MCNexus.

Perda de chave: o mesmo link de solicitação, aberto com a mesma conta GitHub,
recupera a licença já emitida.

## Créditos

Projeto original `spektrafilm` em Python:

Andrea Volpato  
https://github.com/andreavolpato/spektrafilm

Port OpenFX nativo e expansão:

Aedan Oskar Otto Diez / chaert-s  
https://github.com/chaert-s/spektrafilm-ofx

Modificação Look Film Lab, divisão de produtos, calibração, distribuição via
MCNexus e releases:

Magno Ciqueira  
https://github.com/ciqueira

OpenFX SDK:

Academy Software Foundation OpenFX  
https://github.com/AcademySoftwareFoundation/openfx

## Licença

Look Film Lab Plugins é uma distribuição modificada independente baseada no
código-fonte `spektrafilm-ofx` licenciado sob GPL-3.0. Não é um release oficial
do `spektrafilm-ofx` nem um release oficial do projeto original `spektrafilm` em
Python. Não há endosso implícito dos autores upstream.

Consulte:

- [LICENSE.txt](LICENSE.txt)
- [MODIFICATIONS.md](MODIFICATIONS.md)
- [DISTRIBUTION.md](DISTRIBUTION.md)
- [Legal/THIRD_PARTY_NOTICES.txt](Legal/THIRD_PARTY_NOTICES.txt)

## Releases Binários

Os releases binários OFX são distribuídos pelo Nexus e também podem ser
publicados pelo GitHub Releases.

Cada release binário público deve incluir ou apontar para o código-fonte
correspondente àquela versão exata, junto com o texto da licença GPL-3.0, notas
de modificação, notas de distribuição e avisos de terceiros.
