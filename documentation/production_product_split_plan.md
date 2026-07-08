# Look Film Lab Production Product Split Plan

## Objetivo

Separar o modo Production atual em tres produtos OFX independentes, mantendo o
mesmo core de renderizacao e calibracao, mas com identidade, versao, UI,
defaults e lista de stocks proprios por produto.

Produtos planejados:

- `Look Film Lab CINE v0.1.0`
- `Look Film Lab PHOTO v0.1.1`
- `Look Film Lab SCAN v0.1.4`

Cada produto deve aparecer no host como uma OFX diferente. Portanto, cada um
precisa ter:

- nome publico proprio;
- identificador OFX unico;
- versao propria;
- bundle/artefato proprio;
- defaults proprios;
- UI propria;
- nenhuma colisao de cache ou identificacao no DaVinci Resolve.

O modo Calibration continua existindo como ambiente interno de validacao,
ajuste tecnico e desenvolvimento. A divisao em CINE, PHOTO e SCAN e uma divisao
do lado Production.

## Contexto Atual

Hoje o projeto separa principalmente:

- Core/versionamento interno do motor;
- Production build;
- Calibration build.

O Production atual e tratado como um unico produto final. A nova arquitetura
deve preservar o core compartilhado, mas transformar Production em uma familia
de produtos finais.

O versionamento deve ficar separado em duas camadas:

1. Versao do core
   - representa o motor compartilhado;
   - cobre renderizacao, shaders, dados espectrais, pipeline Metal e pipeline
     Vulkan;
   - pode ter variacoes tecnicas por backend quando necessario, por exemplo uma
     versao/compatibilidade para Metal e outra para Vulkan.

2. Versao de produto
   - representa a OFX distribuida ao usuario;
   - cada produto CINE, PHOTO e SCAN tem versao propria;
   - a versao de produto pode evoluir independentemente da versao do core,
     desde que declare qual core usa.

Modelo conceitual desejado:

```text
Core compartilhado
  |
  +-- Calibration / interno
  |
  +-- Production CINE
  |
  +-- Production PHOTO
  |
  +-- Production SCAN
```

## Produtos

### Look Film Lab CINE v0.1.0

Produto equivalente ao Production atual.

Escopo inicial:

- fluxo `Full Pipeline`;
- negativos cine;
- prints cine;
- UI baseada na production atual;
- comportamento e defaults atuais preservados tanto quanto possivel.

Stocks negativos cine:

- `kodak_vision3_50d` - Kodak Vision3 50D
- `kodak_vision3_250d` - Kodak Vision3 250D
- `kodak_verita_200d` - Kodak Verita 200D
- `kodak_vision3_200t` - Kodak Vision3 200T
- `kodak_vision3_500t` - Kodak Vision3 500T

Prints cine:

- `kodak_2383` - Kodak Vision 2383
- `kodak_2393` - Kodak Vision Premier 2393

### Look Film Lab PHOTO v0.1.1

Produto para fluxo full pipeline com materiais still/photo.

Escopo inicial:

- fluxo `Full Pipeline`;
- somente negativos still/photo;
- somente papel photographic/still;
- UI production inicial definida neste plano;
- deve herdar o mesmo motor de print/negative simulation do CINE, mas com listas
  filtradas e defaults especificos de foto.

Negativos still/photo:

- `kodak_ektar_100` - Kodak Ektar 100
- `kodak_portra_160` - Kodak Portra 160
- `kodak_portra_400` - Kodak Portra 400
- `kodak_portra_800` - Kodak Portra 800
- `kodak_portra_800_push1` - Kodak Portra 800 Push 1
- `kodak_portra_800_push2` - Kodak Portra 800 Push 2
- `kodak_gold_200` - Kodak Gold 200
- `kodak_ultramax_400` - Kodak Ultramax 400
- `fujifilm_pro_400h` - Fujifilm Pro 400H
- `fujifilm_c200` - Fujifilm C200
- `fujifilm_xtra_400` - Fujifilm X-Tra 400

Papeis still/photo:

- `kodak_endura_premier` - Kodak Professional Endura Premier
- `kodak_ultra_endura` - Kodak Professional Ultra Endura
- `kodak_ektacolor_edge` - Kodak Ektacolor Edge
- `kodak_supra_endura` - Kodak Professional Supra Endura
- `kodak_portra_endura` - Kodak Professional Portra Endura
- `fujifilm_crystal_archive_typeii` - Fujifilm Crystal Archive Type II

Stocks reversiveis/positivos que ficam fora do PHOTO no primeiro momento:

- `kodak_ektachrome_100` - Kodak Ektachrome 100
- `kodak_kodachrome_64` - Kodak Kodachrome 64
- `fujifilm_velvia_100` - Fujifilm Velvia 100
- `fujifilm_provia_100f` - Fujifilm Provia 100F

Esses stocks podem virar um produto/familia propria no futuro, ou entrar como
modo especifico se a pipeline para reversivel for tratada corretamente.

### Look Film Lab SCAN v0.1.4

Produto para scans/capturas de negativos fisicos.

Escopo inicial:

- fluxo `Scanned Negative / Bypass Negative`;
- sem simulacao de negativo digital;
- foco em normalizar o scan/captura e alimentar a simulacao de print;
- somente materiais de print/papel;
- UI production inicial definida neste plano;
- deve usar o trabalho atual do bypass negative como base.

Materiais de print incluidos:

Papeis still/photo:

- `kodak_endura_premier` - Kodak Professional Endura Premier
- `kodak_ultra_endura` - Kodak Professional Ultra Endura
- `kodak_ektacolor_edge` - Kodak Ektacolor Edge
- `kodak_supra_endura` - Kodak Professional Supra Endura
- `kodak_portra_endura` - Kodak Professional Portra Endura
- `fujifilm_crystal_archive_typeii` - Fujifilm Crystal Archive Type II

Prints cine:

- `kodak_2383` - Kodak Vision 2383
- `kodak_2393` - Kodak Vision Premier 2393

No produto SCAN, o `Negative Stock` nao deve influenciar o resultado. O negativo
fisico ja esta presente na imagem de entrada, e o produto deve expor somente os
controles necessarios para preparar o scan e escolher o material de print.

## Identificacao OFX

Cada produto precisa de um identificador unico para evitar conflito no host.

Sugestao inicial:

```text
Look Film Lab CINE
  label: Look Film Lab CINE v0.1.0
  identifier: com.mclookfilmlab.cine

Look Film Lab PHOTO
  label: Look Film Lab PHOTO v0.1.1
  identifier: com.mclookfilmlab.photo

Look Film Lab SCAN
  label: Look Film Lab SCAN v0.1.4
  identifier: com.mclookfilmlab.scan
```

Os nomes finais dos bundles tambem devem ser unicos, por exemplo:

```text
MCLookFilmLabCINE.ofx.bundle
MCLookFilmLabPHOTO.ofx.bundle
MCLookFilmLabSCAN.ofx.bundle
```

## Build E Versao

O CMake deve deixar de tratar Production como um unico artefato final.

Opcao recomendada:

```text
SPEKTRAFILM_PRODUCT_KIND
  CALIBRATION
  PRODUCTION_CINE
  PRODUCTION_PHOTO
  PRODUCTION_SCAN
```

Cada produto deve receber por compile definition:

- `SPEKTRAFILM_PLUGIN_IDENTIFIER`
- `SPEKTRAFILM_PLUGIN_LABEL`
- `SPEKTRAFILM_PRODUCT_VERSION_STRING`
- `SPEKTRAFILM_PRODUCT_VERSION_MAJOR`
- `SPEKTRAFILM_PRODUCT_VERSION_MINOR`
- `SPEKTRAFILM_PRODUCT_VERSION_PATCH`
- `SPEKTRAFILM_PRODUCT_KIND`
- `SPEKTRAFILM_VERSION_STRING`, mantendo a semantica atual de versao do
  core/backend
- `SPEKTRAFILM_CORE_BACKEND_VERSION_STRING`, se a separacao Metal/CUDA/Vulkan
  for formalizada.

Arquivos de versao planejados:

```text
VERSION_CINE
  versao do produto Look Film Lab CINE

VERSION_PHOTO
  versao do produto Look Film Lab PHOTO

VERSION_SCAN
  versao do produto Look Film Lab SCAN
```

O arquivo `VERSION` atual deve ser removido. A versao do core/backend continua
vindo da definicao atual `SPEKTRAFILM_VERSION_STRING`, que hoje e alimentada
pelos valores hardcoded/cache do CMake.

O CMake deve ler o arquivo especifico do produto para gerar:

- label publico;
- manifest;
- artifact/release;
- metadados do plugin;
- `SPEKTRAFILM_PRODUCT_VERSION_STRING`;
- `SPEKTRAFILM_PRODUCT_VERSION_MAJOR`;
- `SPEKTRAFILM_PRODUCT_VERSION_MINOR`;
- `SPEKTRAFILM_PRODUCT_VERSION_PATCH`.

Os usos publicos que hoje exibem `SPEKTRAFILM_VERSION_STRING` devem ser
revisados. Onde a UI, manifest ou preset estiver falando da versao do produto,
deve usar `SPEKTRAFILM_PRODUCT_VERSION_STRING`. Onde estiver falando do motor
ou backend, deve continuar usando `SPEKTRAFILM_VERSION_STRING`.

Observacao:

O OpenFX usa major/minor para a versao do plugin, mas o patch deve continuar
visivel no label, manifest, nome do produto e/ou metadados auxiliares.

Metadados recomendados por produto:

```text
Product: Look Film Lab CINE
Product version: 0.1.0
Core version: definido pelo build
Bundle: MCLookFilmLabCINE.ofx.bundle

Product: Look Film Lab PHOTO
Product version: 0.1.1
Core version: definido pelo build
Bundle: MCLookFilmLabPHOTO.ofx.bundle

Product: Look Film Lab SCAN
Product version: 0.1.4
Core version: definido pelo build
Bundle: MCLookFilmLabSCAN.ofx.bundle
```

## UI Por Produto

### CINE

Estado inicial:

- preservar a UI production atual;
- esconder controles de calibracao;
- usar listas filtradas para cine;
- defaults atuais como baseline.

Grupos publicos atuais no Production CINE:

1. `Technical Controls`
   - `Input Space`
   - `Input Gamma`
   - `Output Display`
   - `Output Gamma`

2. `Camera`
   - `Film Format`
   - `Exposure`

3. `Stocks`
   - `Profile Negative`
   - `Profile Print`

4. `Lab`
   - `Push/Pull`
   - `Bleach Bypass`
   - `Enabled Printer Light`
   - `Linked Printer Light`
   - `Red Printer Light`
   - `Green Printer Light`
   - `Blue Printer Light`
   - `Print Bleach Bypass`

5. `Support`
   - `About and Help`;
   - `App MCNexus`.

Pagina principal atual:

```text
Controls
  Input Space
  Input Gamma
  Output Display
  Output Gamma
  Film Format
  Exposure
  Profile Negative
  Profile Print
  Push/Pull
  Bleach Bypass
  Enabled Printer Light
  Linked Printer Light
  Red Printer Light
  Green Printer Light
  Blue Printer Light
  Print Bleach Bypass
  Support/About
```

Controles internos usados mas escondidos no Production CINE:

- `dirUsesStockCalibration`

### PHOTO

Estado inicial:

- fluxo full pipeline;
- esconder materiais cine;
- expor negativos still/photo e papeis still/photo;
- usar UI simplificada equivalente ao CINE, mas com linguagem de foto/papel.

Grupos publicos planejados para PHOTO:

1. `Technical Controls`
   - `Input Space`
   - `Input Gamma`
   - `Output Display`
   - `Output Gamma`

2. `Camera`
   - `Film Format`
   - `Exposure`

3. `Stocks`
   - `Profile Negative`
   - `Profile Paper`

4. `Lab`
   - `Negative Push/Pull`
   - `Bleach Bypass`
   - `Paper Push/Pull`
   - `Paper Bleach Bypass`

5. `Support`
   - `About and Help`
   - `App MCNexus`

Pagina principal planejada:

```text
Controls
  Input Space
  Input Gamma
  Output Display
  Output Gamma
  Film Format
  Exposure
  Profile Negative
  Profile Paper
  Negative Push/Pull
  Bleach Bypass
  Paper Push/Pull
  Paper Bleach Bypass
  About and Help
  App MCNexus
```

Questoes pendentes:

- defaults para Portra/Endura ou outra combinacao.

### SCAN

Estado inicial:

- forcar ou defaultar `Print Source = Scanned Negative / Bypass Negative`;
- remover influencia de `Negative Stock`;
- expor somente `Print/Paper Stock` com todos os materiais de print;
- concentrar controles de scan no grupo `Scanned Negative`;
- esconder controles irrelevantes do full pipeline.

Grupos publicos planejados para SCAN:

1. `Technical Controls`
   - `Input Space`
   - `Input Gamma`
   - `Output Display`
   - `Output Gamma`

2. `Scanned Negative`
   - `Film Base Color`
   - `Film Base Temp`
   - `Film Base Tint`
   - `Scan Exposure EV`
   - `Density Scale R`
   - `Density Scale G`
   - `Density Scale B`

3. `Stocks`
   - `Profile Paper`

4. `Lab`
   - `Paper Push/Pull`
   - `Paper Bleach Bypass`

5. `Support`
   - `About and Help`
   - `App MCNexus`

Pagina principal planejada:

```text
Controls
  Input Space
  Input Gamma
  Output Display
  Output Gamma
  Film Base Color
  Film Base Temp
  Film Base Tint
  Scan Exposure EV
  Density Scale R
  Density Scale G
  Density Scale B
  Profile Paper
  Paper Push/Pull
  Paper Bleach Bypass
  About and Help
  App MCNexus
```

Observacao de UI:

- `Density Scale R/G/B` devem ser expostos como sliders separados no SCAN
  production, mesmo que internamente continuem mapeados para um vetor RGB.

Questoes pendentes:

- definir defaults finais de `Film Base Color`, `Scan Exposure EV` e `Density
  Scale R/G/B`;
- confirmar se `Scan Density Contrast` fica fixo internamente no production
  SCAN ou se entra depois como controle publico.

## Dados De Stock

A fonte atual da ordem de stocks e:

```text
tools/ofx_stock_lists.py
```

Os metadados completos ficam em:

```text
Resources/data/profiles/*.json
```

Campos relevantes:

- `support`: `film` ou `paper`;
- `stage`: `filming` ou `printing`;
- `use`: `cine` ou `still`;
- `type`: `negative` ou `positive`;
- `target_print`: papel/print sugerido.

Plano:

1. manter uma lista completa para Calibration;
2. criar listas filtradas por produto;
3. garantir que indices internos continuem apontando para os mesmos perfis
   gerados;
4. evitar quebrar presets existentes quando possivel;
5. testar o comportamento quando um preset antigo referencia um stock oculto no
   produto atual.

## Fases De Implementacao

### Fase 1 - Arquitetura de produto

- adicionar `ProductKind` no build e no codigo;
- parametrizar label, identifier e versao por produto;
- criar targets/bundles separados para CINE, PHOTO e SCAN;
- garantir que os tres produtos aparecem lado a lado no DaVinci Resolve;
- manter comportamento visual o mais proximo possivel do Production atual.

### Fase 2 - Listas de stocks por produto

- criar filtros de stock para CINE, PHOTO e SCAN;
- garantir que dropdowns exibem apenas os stocks aprovados para cada produto;
- definir defaults por produto;
- validar fallback quando um indice fica fora da lista do produto.

### Fase 3 - UI production por produto

- congelar UI CINE como baseline;
- desenhar UI PHOTO;
- desenhar UI SCAN;
- esconder controles que pertencem apenas a Calibration;
- revisar ordem dos grupos e defaults.

### Fase 4 - Packaging e instalacao

- atualizar scripts de build/package;
- atualizar manifests;
- atualizar instalador/uninstaller;
- garantir que os tres bundles podem coexistir;
- garantir que cache do Resolve nao mistura os produtos.
- atualizar ou criar o workflow GitHub Actions para build/release dos tres
  produtos.

### Fase 5 - Testes

Testes minimos:

- build dos tres produtos;
- instalacao lado a lado no DaVinci Resolve;
- abertura de cada OFX no host;
- confirmacao de label/versao/identifier;
- validacao de dropdowns de stock;
- render simples em CINE;
- render simples em PHOTO;
- render simples em SCAN com scan de negativo fisico;
- export/still para confirmar consistencia de cor.

## GitHub Actions / CI

O workflow de build/release tambem precisa acompanhar a divisao em produtos.

Arquivo planejado:

```text
.github/workflows/build-maclookfilmelab-spektrafilm.yml
```

Observacao do checkpoint:

- no working tree atual do `spektrafilm-ofx-look`, a pasta
  `.github/workflows` nao esta presente;
- portanto, a implementacao pode precisar criar esse arquivo do zero ou
  restaurar o workflow esperado antes de adapta-lo;
- o workflow de referencia e:

```text
/Volumes/DataMEDIA/DEV/_OFX/Plugins OFX/BaldavengerOFX/.github/workflows/build-plugins.yml
```

Estrutura desejada, inspirada no workflow do BaldavengerOFX:

- `workflow_dispatch` com inputs para escolher produto e alvo;
- input `product` com opcoes:
  - `all`;
  - `cine`;
  - `photo`;
  - `scan`;
- input `target` com opcoes:
  - `both`;
  - `macos`;
  - `windows`;
- input opcional de descricao de release;
- input opcional `prerelease`;
- job `prepare` para montar a matrix de produtos;
- job `build-macos`;
- job `build-windows`;
- `build-windows` usa o backend Vulkan, portanto precisa instalar/preparar
  Vulkan SDK e `glslc` antes do configure CMake;
- upload de artefatos por produto;
- etapa futura de release, se mantivermos publicacao automatica.

Matrix planejada:

```text
cine
  product_name: Look Film Lab CINE
  product_version: 0.1.0
  bundle: MCLookFilmLabCINE.ofx.bundle
  artifact: MCLookFilmLabCINE-macOS / MCLookFilmLabCINE-Windows

photo
  product_name: Look Film Lab PHOTO
  product_version: 0.1.1
  bundle: MCLookFilmLabPHOTO.ofx.bundle
  artifact: MCLookFilmLabPHOTO-macOS / MCLookFilmLabPHOTO-Windows

scan
  product_name: Look Film Lab SCAN
  product_version: 0.1.4
  bundle: MCLookFilmLabSCAN.ofx.bundle
  artifact: MCLookFilmLabSCAN-macOS / MCLookFilmLabSCAN-Windows
```

Comportamento esperado no build:

- CINE compila como target `MCLookFilmLabCINE` com `SPEKTRAFILM_PRODUCT_KIND=1`;
- PHOTO compila como target `MCLookFilmLabPHOTO` com `SPEKTRAFILM_PRODUCT_KIND=2`;
- SCAN compila como target `MCLookFilmLabSCAN` com `SPEKTRAFILM_PRODUCT_KIND=3`;
- o target agregado `spektrafilm` em modo Production depende dos tres produtos;
- o modo Calibration continua usando o target `spektrafilm` e gera
  `MCLookFilmLab.ofx.bundle`;
- cada produto deve receber label, identifier, versao e bundle name proprios;
- os artefatos nao podem sobrescrever uns aos outros no mesmo build;
- a versao do core deve ser reportada junto da versao de produto no manifest ou
  no log de build.

Comandos de build locais:

```text
cmake -S . -B build-production -DSPEKTRAFILM_PRO_BUILD_MODE=PRODUCTION
cmake --build build-production --target spektrafilm

cmake --build build-production --target MCLookFilmLabCINE
cmake --build build-production --target MCLookFilmLabPHOTO
cmake --build build-production --target MCLookFilmLabSCAN
```

## Pontos De Atencao

- O plugin hoje ainda tem partes do versionamento fixas no codigo. Isso precisa
  ser parametrizado para suportar versoes por produto.
- Os nomes de bundle, binario, manifest e identifier precisam ser unicos.
- Presets/defaults de usuario podem precisar ser separados por produto.
- PHOTO e SCAN tem UI production inicial definida, mas ainda precisam de ajuste
  fino de defaults e validacao no DaVinci Resolve.
- SCAN deve impedir contaminacao pelo negativo digital/stock negativo.
- A lista de stocks reversiveis/positivos ainda nao entra em PHOTO no primeiro
  ciclo.
- O workflow `.github/workflows/build-maclookfilmelab-spektrafilm.yml` precisa
  ser criado/restaurado no repo atual antes da adaptacao, se continuar ausente.

## Decisoes Confirmadas

- CINE representa o produto production atual.
- PHOTO usa full pipeline com negativos e papeis still/photo.
- SCAN usa bypass negative e inclui todos os materiais de print:
  papeis still/photo + Kodak 2383/2393.
- Cada produto deve ser uma OFX independente com identificacao propria.
