# Plano de Arquitetura LookFilmLab: Calibração e Produção

## 1. Objetivo

O objetivo é usar exclusivamente a versão **LookFilmLab** para todo o fluxo,
mantendo um único motor, um único flavor OFX e um único produto:

- **Pro / Calibration build mode**: compilação interna da versão Pro com
  somente os grupos e controles aprovados para calibração.
- **Pro / Production build mode**: compilação distribuída da mesma versão Pro,
  contendo apenas os controles criativos e operacionais aprovados.

A calibração criada no modo Calibration da versão Pro deverá ser exportada,
validada e incorporada ao modo Production da própria versão Pro. Assim, o
usuário final recebe o comportamento calibrado sem ter acesso aos parâmetros
técnicos usados para construí-lo.

O ponto de partida não será uma calibração vazia. O sistema atual será tratado
como **baseline calibrada**. A nova calibração continuará a partir dos valores
que já existem no build:

- perfis espectrais e curvas geradas;
- calibração DIR armazenada nos perfis;
- filtros neutros C/M/Y existentes;
- defaults técnicos atuais de Film e Print;
- comportamento atual de Filtering;
- pipeline SDR legado com Color Adaptation desligada.

A nova etapa salvará overrides ou ajustes adicionais sobre essa baseline. Um
campo ausente no novo arquivo de calibração significa “usar o valor atual do
build”, e não “usar zero”.

O motor Metal/Vulkan, as estruturas de render e os dados espectrais devem
continuar compartilhados entre os dois modos de build.

---

## 2. Resultado esperado

O fluxo completo deverá ser:

```text
LookFilmLab
Build mode: Calibration
        |
        | calibração de negativos, prints e combinações
        v
active_production_calibration.lookfilmlab.json
        |
        | validação e geração durante o build
        v
production_calibration.lookfilmlab.json
        |
        | compilado dentro do plugin
        v
LookFilmLab
Build mode: Production
MCLookFilmLab.ofx.bundle
```

O bundle Pro distribuído:

- não dependerá de arquivos de calibração externos;
- não dependerá das preferências locais da máquina usada para calibrar;
- será compilado com a interface Calibration indisponível;
- manterá apenas os controles públicos aprovados;
- produzirá o mesmo resultado visual do modo Pro Calibration quando os
  controles públicos estiverem em seus valores neutros;
- terá calibração identificável por versão e hash.

---

## 3. Estrutura atual relevante

### `src/SpektraFilmPlugin.cpp`

Responsável por:

- declaração dos parâmetros OFX;
- organização dos grupos da interface;
- visibilidade dos parâmetros por flavor;
- cache dos handles OFX;
- leitura dos parâmetros e criação de `RenderParams`;
- presets, defaults e clipboard;
- exportação de LUT;
- callbacks de alteração de parâmetros;
- criação e gerenciamento das instâncias.

Este será o principal ponto de alteração da interface e de combinação entre
calibração e controles públicos.

### `src/SpektraParameters.h`

Contém:

- enums compartilhados;
- estrutura `RenderParams`;
- valores enviados aos renderizadores.

Essa estrutura deve continuar representando os valores finais efetivos usados
no processamento. A calibração não deve ser aplicada novamente dentro de cada
renderer.

### `src/SpektraMetalRenderer.mm`

Implementa o renderer Metal e recebe `RenderParams` já resolvidos.

### `src/SpektraVulkanRenderer.cpp`

Implementa o renderer Vulkan e também deve receber os mesmos `RenderParams`
resolvidos.

### `src/SpektraProfileCurves.h`

Declara as tabelas de perfis de negativos e papéis geradas durante o build.

### `CMakeLists.txt`

Atualmente cria três flavors:

- `spektrafilm_flow`;
- `spektrafilm`;
- `spektrafilm_dev`.

O flavor `Pro`, atualmente associado ao target `spektrafilm`, será a base única
da operação. Flow e Dev permanecem fora do escopo desta arquitetura.

### Sistema atual de snapshots e presets

`SpektraFilmPlugin.cpp` já possui:

- captura de parâmetros em `DefaultsSnapshot`;
- serialização e desserialização;
- presets `.spkpreset`;
- defaults persistentes;
- aplicação de snapshots ao conjunto de parâmetros.

Esse sistema pode ser reutilizado parcialmente, mas a calibração de produção
deverá ter formato, validação e ciclo de vida próprios.

### Observação sobre o grupo `Manage`

O grupo `Manage` atual já implementa uma base útil para persistência de
parâmetros. Ele define:

- `Preset Name`;
- `Preset`;
- `Save Preset`;
- `Load Preset`;
- `Copy Params`;
- `Paste Params`;
- `Set Defaults`;
- `Reset Factory Defaults`;
- `Export LUT`;
- opções auxiliares de LUT e GPU tiling.

O fluxo atual funciona assim:

- `Save Preset` captura um `DefaultsSnapshot` completo por meio de
  `captureParamSetSnapshot()` e grava um `.spkpreset` em
  `~/Documents/MCLookFilmLab/presets`;
- `Load Preset` lê esse `.spkpreset` e aplica os valores com
  `applySnapshotToParamSet()`;
- `Copy Params` usa o mesmo snapshot, mas envia para o clipboard;
- `Set Defaults` salva defaults de usuário em `ofx-defaults-v1.spkdefaults`;
- os arquivos usam um payload `SPKDFLT2` obfuscado, com nome, tipo e valor dos
  parâmetros.

Isso é excelente como infraestrutura, mas não deve ser usado diretamente como
arquivo oficial de calibração de produção, porque:

- captura parâmetros por lista global de defaults, não por escopo técnico de
  calibração;
- pode incluir controles criativos/públicos, como exposição, stock selecionado,
  grain, halation, diffusion, scanner e LUT;
- é pensado para preferências do usuário e presets de sessão;
- depende da existência dos parâmetros OFX definidos no host;
- não tem schema explícito para negativo, print, global filtering e correção
  opcional de combinação.

Decisão: reaproveitar a infraestrutura, mas separar o produto final.

O plano passa a tratar o sistema atual de `Manage` como uma biblioteca interna
para:

- ler/escrever arquivo com segurança;
- gerar nomes e timestamps;
- capturar/aplicar valores de parâmetros OFX;
- reusar helpers de serialização simples quando fizer sentido;
- reaproveitar mensagens de sucesso/erro e refresh de escolhas.

A calibração Pro deverá usar uma camada nova por cima:

- `Calibration Snapshot`, diferente de `DefaultsSnapshot`;
- whitelist explícita de parâmetros calibráveis;
- exclusão obrigatória dos controles públicos;
- escopos separados: `global`, `negative`, `print` e `pairOverride`;
- formato versionado próprio, por exemplo
  `lookfilmlab-calibration-v1`;
- exportação para arquivo fonte/JSON incorporado no bundle Production;
- validação antes de aceitar/exportar.

Em outras palavras: `Manage` mostra que a mecânica já existe. A calibração
precisa de outro envelope e outra política.

---

## 4. Decisão arquitetural principal

### Um flavor Pro com dois modos de build

A implementação utilizará o mesmo target, artifact e identificador Pro:

```text
spektrafilm.ofx.bundle
org.spektrafilm
```

O que muda será uma opção de compilação:

```text
SPEKTRAFILM_PRO_BUILD_MODE=CALIBRATION
SPEKTRAFILM_PRO_BUILD_MODE=PRODUCTION
```

Não haverá um checkbox runtime capaz de transformar o bundle distribuído em
Calibration. A seleção acontece no CMake e fica fixada no binário.

Como os dois modos usam o mesmo plugin ID, eles não deverão ser instalados
simultaneamente. O fluxo interno substitui temporariamente o bundle Pro pelo
build Calibration, exporta a calibração e recompila o mesmo target em
Production.

Essa decisão oferece:

- continuidade do produto e dos projetos criados com a versão Pro;
- preservação do ID `org.spektrafilm`;
- ausência de um quarto flavor;
- menor risco de divergência entre produtos;
- um único pipeline de packaging;
- controles técnicos ausentes ou secretos no binário distribuído.

### Um único motor

Os builds não devem duplicar:

- shaders;
- renderizadores;
- estruturas de parâmetros;
- tabelas espectrais;
- algoritmos de filme e print.

A diferença entre os modos Pro estará em:

- interface exposta;
- ferramentas disponíveis;
- origem dos valores técnicos;
- recursos internos de calibração.

---

## 5. Configuração do flavor Pro

O enum atual não precisa receber novos flavors:

```cpp
enum class PluginFlavor : int32_t {
  Flow = 0,
  Pro = 1,
  FilmDev = 2,
};
```

O target usado será o existente:

```cmake
add_spektra_ofx_plugin(
  spektrafilm
  spektrafilm
  spektrafilm
  org.spektrafilm
  1
  TRUE
)
```

### Build mode do Pro

Adicionar uma opção CMake específica:

```cmake
set(
  SPEKTRAFILM_PRO_BUILD_MODE
  "PRODUCTION"
  CACHE STRING
  "LookFilmLab mode: CALIBRATION or PRODUCTION"
)

set_property(
  CACHE SPEKTRAFILM_PRO_BUILD_MODE
  PROPERTY STRINGS CALIBRATION PRODUCTION
)
```

O target Pro recebe uma compile definition:

```cmake
target_compile_definitions(
  spektrafilm
  PRIVATE
  SPEKTRAFILM_PRO_CALIBRATION_BUILD=$<STREQUAL:${SPEKTRAFILM_PRO_BUILD_MODE},CALIBRATION>
)
```

O valor default deve ser `PRODUCTION`, reduzindo o risco de empacotar
acidentalmente a interface interna.

Os flavors Flow e Dev continuarão presentes no código, mas não participam do
ciclo de calibração, build padrão ou distribuição Pro definido neste plano.

Decisão atual:

- os targets Flow e Dev serão desativados no build padrão e no release;
- o código-fonte e os flavors permanecerão no repositório;
- uma opção interna de desenvolvimento poderá reativá-los quando necessário;
- somente o target Pro fará parte do build e packaging normais.

---

## 6. Interface Pro Production desejada

### Technical Controls

| Tipo | Controle |
| --- | --- |
| Dropdown | Input Space |
| Dropdown | Output Display |

### Camera Settings

| Tipo | Controle |
| --- | --- |
| Slider | Exposure |

### Film Stocks

| Tipo | Controle |
| --- | --- |
| Dropdown | Profile Negative |
| Dropdown | Profile Print |

### Laboratory

| Tipo | Controle |
| --- | --- |
| Slider | Push/Pull |
| Dropdown | Film Format |
| Slider | Bleach Bypass |
| Slider | Red Printer Light |
| Slider | Green Printer Light |
| Slider | Blue Printer Light |
| Slider | Print Bleach Bypass |

---

## 7. Mapeamento dos controles públicos

| Controle Pro Production | Parâmetro atual | Observação |
| --- | --- | --- |
| Input Space | `inputColorSpace` | Pode usar diretamente a lista já existente ou uma lista reduzida. |
| Output Display | `sdrOutputColorSpace` | Usar somente os outputs SDR; `outputRole` ficará fixado em `DisplaySdr`. |
| Exposure | `filmExposureEv` | Equivalência direta. |
| Profile Negative | novo choice público → `film` oculto | Mostrar quatro opções e mapear para os índices internos originais. |
| Profile Print | novo choice público → `paper` oculto | Mostrar duas opções e mapear para os índices internos originais. |
| Push/Pull | `filmPushPullStops` | Controlará somente o desenvolvimento do negativo. |
| Film Format | `filmFormat` | Usar diretamente o formato global existente. |
| Bleach Bypass | `negativeBleachBypassAmount` | Equivalência direta. |
| Red Printer Light | novo controle público criativo | Trim RGB criativo aplicado sobre o pipeline atual. |
| Green Printer Light | novo controle público criativo | Trim RGB criativo aplicado sobre o pipeline atual. |
| Blue Printer Light | novo controle público criativo | Trim RGB criativo aplicado sobre o pipeline atual. |
| Print Bleach Bypass | `printBleachBypassAmount` | Equivalência direta. |

### Output Display SDR

O modo Pro Production não oferecerá HDR nem RCM em `Output Display`.

O comportamento será:

```cpp
params.outputRole = spektrafilm::OutputRole::DisplaySdr;
params.outputColorSpace = selectedSdrOutputColorSpace;
```

A lista poderá reutilizar diretamente as opções SDR já existentes:

```text
sRGB
Display P3
ProPhoto RGB
Adobe RGB (1998)
DCI-P3
P3-D65 Gamma 2.2
P3-D65 Gamma 2.6
Rec.709 Gamma 2.2
Rec.709 Gamma 2.4
```

A decisão atual é manter todas as nove opções SDR.

### Film Format global

O controle público será apenas `Film Format`, ligado diretamente a
`filmFormat`. Não haverá um segundo controle de formato.

Como esse parâmetro é global, continuará influenciando todos os efeitos
espaciais que atualmente dependem do formato de filme.

### Printer lights independentes

Os três controles RGB serão mantidos, mas sem checkbox de ativação e sem link
entre canais. Cada canal será ajustado independentemente.

Na primeira versão, esses controles serão **criativos**, não técnicos. Eles não
farão parte da calibração salva e não tentarão representar printer points
físicos de laboratório.

O comportamento esperado é:

- valor `0` não altera a imagem;
- valores positivos/negativos funcionam como trims RGB de laboratório;
- os trims são aplicados sobre a baseline calibrada atual;
- a calibração técnica continua usando os filtros/offsets internos existentes;
- presets de usuário podem salvar esses valores;
- snapshots de calibração não devem salvar esses valores.

Implementação recomendada para a primeira fase:

- criar controles públicos próprios, por exemplo
  `creativePrinterLightR/G/B`;
- manter os controles técnicos atuais ocultos;
- converter os trims criativos para ajustes compatíveis com o pipeline atual de
  `Filtered Enlarger`;
- não usar unidade física na UI;
- usar tooltip deixando claro que é um ajuste criativo de cor, não uma medição
  física de laboratório.

---

## 8. Escopo da UI de calibração por grupo

O modo Pro Calibration não deve simplesmente mostrar todos os grupos do Pro
atual. Cada grupo terá um estado explícito:

| Grupo atual | Participa da calibração | Estado no Pro Calibration |
| --- | --- | --- |
| Color Management | Não inicialmente | Seletores públicos disponíveis quando necessários; parâmetros técnicos fixados e fora da captura. |
| Filtering | Sim | Visível e habilitado. |
| Film Plane | Não | Desativado na UI. |
| Film | Sim, exceto controles públicos | Controles técnicos habilitados; controles públicos excluídos da captura de calibração. |
| Print | Sim, exceto controles públicos | Controles técnicos habilitados; controles públicos excluídos da captura de calibração. |
| DIR Couplers | Sim, exceto eventuais controles públicos | Controles técnicos habilitados; qualquer controle público fica excluído da captura. |
| Grain | Não | Desativado na UI. |
| Halation | Não | Desativado na UI. |
| Diffusion | Não | Desativado na UI. |
| Scanner | Não | Desativado na UI. |
| Info | Não | Desativado na UI. |
| Manage | Não | Desativado na UI. |

### Color Management

O grupo atual contém:

- modo de processo;
- inversão de negativo;
- Input Color Space;
- Output Role;
- Output Color Space SDR;
- controles HDR;
- modo RCM;
- Color Adaptation e seus controles técnicos de compressão.

No Pro Production:

- o modo de processo será fixado em Print Simulation;
- `Input Space` será um controle público;
- `Output Display` será um controle público limitado a SDR;
- HDR, RCM, scan negative e process negative ficarão indisponíveis;
- Color Adaptation ficará fixada em `Off`.

Decisão inicial: Color Management não será capturado como calibração. Ele será
tratado como configuração pública ou configuração técnica fixa do produto.

Color Adaptation é atualmente um caminho experimental que combina:

- compressão de input para o visible locus na conversão RGB-to-raw;
- interpolação suavizada das curvas de densidade;
- compressão de lightness em OKLch;
- compressão de chroma para o gamut do display de destino.

Como `Off` preserva o pipeline SDR legado atualmente usado, esse controle e
seus quatro subcontroles ficarão ocultos e fora da calibração. Uma avaliação
futura poderá ser feita separadamente, sem alterar a baseline desta fase.

### Filtering

O grupo `Filtering` entra na calibração global. Seus controles técnicos serão
capturados uma única vez e aplicados a todos os negativos e prints.

### Film, Print e DIR Couplers

Esses três grupos formam o núcleo da calibração.

Dentro deles, é necessário separar:

- parâmetros técnicos calibráveis;
- parâmetros públicos expostos no Production;
- parâmetros fixos internos;
- parâmetros obsoletos ou sem efeito.

Os controles públicos não serão gravados na calibração:

| Grupo | Controle público excluído da calibração |
| --- | --- |
| Film | Exposure, Profile Negative, Push/Pull, Film Format e Bleach Bypass |
| Print | Profile Print, Red/Green/Blue Printer Light e Print Bleach Bypass |
| DIR Couplers | Nenhum controle público na lista final atual |

`Profile Negative` e `Profile Print` continuam ativos como seletores do escopo
que está sendo calibrado, mas seus valores não são parâmetros técnicos de
calibração.

Durante o trabalho de calibração, os demais controles públicos de Film e Print
devem ficar bloqueados em valor neutro para não contaminar o resultado técnico.
Eles voltam a ser editáveis somente no modo Pro Production.

### Grupos fora da calibração

`Film Plane`, `Grain`, `Halation`, `Diffusion`, `Scanner`, `Info` e `Manage`
ficarão desativados na UI do modo Pro Calibration.

Os valores desses grupos:

- não serão capturados no JSON;
- não serão usados para decidir a calibração de um stock;
- permanecerão em defaults internos estáveis;
- não poderão alterar o resultado enquanto uma calibração estiver sendo
  capturada.

Como `Manage` ficará desativado, as ações de salvar, resetar, validar e exportar
calibração deverão ficar em um novo grupo dedicado chamado `Calibration`.

---

## 9. Modelo de calibração

A calibração não deve ser apenas um snapshot global de todos os parâmetros OFX.
Ela deve representar explicitamente os níveis em que os valores são aplicados.

### Baseline + overrides

O valor efetivo será resolvido nesta ordem:

```text
dados e defaults atuais do build
  + overrides globais de Filtering
  + overrides do negativo
  + overrides do print
  + correção opcional da combinação
  + controles públicos do usuário
```

Regras:

- a baseline atual permanece a fonte de verdade quando não houver override;
- exportar uma nova calibração não precisa duplicar todos os valores atuais;
- resetar uma calibração remove o override e retorna à baseline do build;
- a primeira versão do arquivo pode começar vazia e produzir exatamente o
  resultado atual;
- cada mudança deve ser comparável visualmente contra a baseline anterior.

### Níveis sugeridos

#### Global

Valores válidos para todo o produto:

- parâmetros do grupo Filtering;
- opções técnicas fixadas.

#### Negative

Valores específicos de cada negativo:

- DIR amount;
- coeficientes same-layer;
- coeficientes interlayer;
- difusão DIR;
- gamma técnico;
- bleach-bypass coupling;

#### Print

Valores específicos de cada print:

- gamma técnico;
- shadow/highlight shape;
- propriedades espectrais adicionais;
- resposta de bleach bypass;

#### Negative + Print

Valores específicos da combinação:

- filtros C/M/Y;
- print exposure;
- preflash;
- ajustes químicos da combinação;
- compensações para neutralidade.

### Estratégia sem matriz completa de combinações

A calibração não exigirá uma entrada para cada combinação possível de negativo
e print.

O modelo recomendado será hierárquico:

```text
resultado técnico
  = calibração do negativo
  + calibração do print
  + correção opcional da combinação
```

Regras:

- cada negativo de cinema possui uma calibração própria;
- cada print de cinema possui uma calibração própria, reutilizada com todos os
  negativos;
- uma correção negative + print é opcional e começa em zero;
- uma entrada de combinação só será criada quando testes mostrarem que o print
  independente não mantém neutralidade ou exposição adequadas com determinado
  negativo;
- a ausência de uma combinação nunca deve impedir o uso do negativo com o
  print.

O renderer atual usa a baseline do modo `Filtered Enlarger`. A neutralidade das
combinações aprovadas vem dos filtros C/M/Y existentes.

Valores C/M/Y atuais para as oito combinações aprovadas:

| Print | Negativo | C | M | Y |
| --- | --- | ---: | ---: | ---: |
| Kodak 2383 | Vision3 50D | 0.000 | 51.354 | 57.936 |
| Kodak 2383 | Vision3 250D | 0.000 | 52.405 | 55.384 |
| Kodak 2383 | Vision3 200T | 0.000 | 45.833 | 55.469 |
| Kodak 2383 | Vision3 500T | 0.000 | 46.008 | 57.595 |
| Kodak 2393 | Vision3 50D | 0.000 | 51.692 | 59.245 |
| Kodak 2393 | Vision3 250D | 0.000 | 52.726 | 56.773 |
| Kodak 2393 | Vision3 200T | 0.000 | 45.861 | 56.980 |
| Kodak 2393 | Vision3 500T | 0.000 | 46.087 | 59.005 |

Esses valores fazem parte da baseline atual e continuarão ativos durante a nova
calibração.

### Printer points físicos fora do plano atual

Printer points físicos de laboratório não fazem parte deste plano.

Quando houver interesse nessa abordagem, será criado um documento separado com:

- dados necessários;
- validação do pipeline;
- unidade dos printer points;
- compatibilidade com a UI;
- impacto sobre os filtros C/M/Y atuais.

Até lá, os controles públicos de `Red/Green/Blue Printer Light` são apenas
trims criativos sobre o pipeline atual.

### Catálogo inicial de cinema

O Production deve expor somente os perfis aprovados como materiais de cinema.

Lista final de negativos:

```text
Kodak Vision3 50D
Kodak Vision3 250D
Kodak Vision3 200T
Kodak Vision3 500T
```

Lista final de prints:

```text
Kodak 2383
Kodak 2393
```

Os demais perfis continuam compilados no catálogo interno para preservar dados,
índices e compatibilidade, mas não aparecem nos dropdowns públicos.

Stocks internos ocultos:

**Negativos ocultos**

```text
Kodak Ektar 100
Kodak Portra 160
Kodak Portra 400
Kodak Portra 800
Kodak Portra 800 Push +1
Kodak Portra 800 Push +2
Kodak Gold 200
Kodak Ultramax 400
Kodak Verita 200D
Fujifilm Pro 400H
Fujifilm C200
Fujifilm X-Tra 400
Kodak Ektachrome 100
Kodak Kodachrome 64
Fujifilm Velvia 100
Fujifilm Provia 100F
```

**Prints ocultos**

```text
Kodak Endura Premier
Kodak Ultra Endura
Kodak Ektacolor Edge
Kodak Supra Endura
Kodak Portra Endura
Fujifilm Crystal Archive Type II
```

“Oculto” significa somente ausente da lista pública. Esses perfis não serão
removidos nem renumerados no catálogo interno.

### Preservação dos índices originais

Os índices internos atuais serão preservados:

| Material | Índice interno original |
| --- | ---: |
| Kodak Vision3 50D | 8 |
| Kodak Vision3 250D | 9 |
| Kodak Vision3 200T | 11 |
| Kodak Vision3 500T | 12 |
| Kodak 2383 | 6 |
| Kodak 2393 | 7 |

Não é seguro reduzir diretamente as opções dos parâmetros OFX antigos `film` e
`paper`, pois um choice param OFX retorna sempre a posição visível da opção.
Uma lista de quatro negativos retornaria `0..3`, perdendo a referência aos
índices `8`, `9`, `11` e `12`.

A implementação seguirá o padrão já utilizado no `MCLookFilmLabV2`:

```cpp
constexpr int kProductionFilmUiToInternal[] = {8, 9, 11, 12};
constexpr int kProductionPrintUiToInternal[] = {6, 7};
```

Serão usados:

- dropdowns públicos reduzidos para Production e Calibration;
- parâmetros internos antigos `film` e `paper`, mantidos ocultos;
- conversão UI → índice interno antes do render e da captura;
- defaults novos apontando para um dos quatro negativos e dois prints
  aprovados.

Não haverá fluxo de migração de projetos antigos nesta fase. A preservação dos
índices internos continua sendo feita para manter o catálogo e o código
estáveis, não por existir um conjunto de projetos legados que precise abrir.

### Estrutura C++ sugerida

```cpp
struct SpektraGlobalCalibration {
  bool cameraUvFilterEnabled;
  float cameraUvCutNm;
  bool cameraIrFilterEnabled;
  float cameraIrCutNm;
};

struct SpektraNegativeCalibration {
  int negativeId;
  float filmGamma;
  float dirAmount;
  float dirDiffusionUm;
  float dirDiffusionTailUm;
  float dirDiffusionTailWeight;
  float dirInhibitionSameLayer;
  float dirInhibitionInterlayer;
  float dirGamma[9];
};

struct SpektraPrintCalibration {
  int printId;
  float printGamma;
  float printShadowShape;
  float printHighlightShape;
};

struct SpektraPairCalibration {
  int negativeId;
  int printId;
  float printExposureEv;
  float filterC;
  float filterM;
  float filterY;
  float preflashExposure;
  float printerOffsetR;
  float printerOffsetG;
  float printerOffsetB;
};
```

Os campos definitivos devem ser baseados no inventário completo dos parâmetros
que realmente alteram o resultado.

---

## 10. Formato do arquivo de calibração

Formato sugerido:

```json
{
  "format": "spektrafilm-production-calibration",
  "schema_version": 1,
  "plugin_version": "0.3.0",
  "created_utc": "2026-06-22T12:00:00Z",
  "calibration_name": "production-main",
  "source_plugin": "org.spektrafilm",
  "source_build_mode": "CALIBRATION",
  "profile_catalog_hash": "sha256:...",
  "global": {
    "filtering": {
      "uv_enabled": false,
      "uv_cut_nm": 410.0,
      "ir_enabled": false,
      "ir_cut_nm": 675.0
    }
  },
  "negatives": [
    {
      "id": "kodak_vision3_500t",
      "display_name": "Kodak Vision3 500T",
      "film_gamma": 1.0,
      "dir": {
        "amount": 1.0,
        "diffusion_um": 20.0,
        "tail_um": 200.0,
        "tail_weight": 0.06,
        "same_layer": 1.0,
        "interlayer": 1.0,
        "gamma": [
          0.336,
          0.319,
          0.273,
          0.353,
          0.302,
          0.154,
          0.353,
          0.168,
          0.226
        ]
      }
    }
  ],
  "prints": [],
  "pairs": [
    {
      "negative_id": "kodak_vision3_500t",
      "print_id": "kodak_2383",
      "print_exposure_ev": 0.0,
      "filters": {
        "c": 0.0,
        "m": 0.0,
        "y": 0.0
      },
      "printer_offsets": {
        "r": 0.0,
        "g": 0.0,
        "b": 0.0
      }
    }
  ]
}
```

### Requisitos do formato

- IDs estáveis, sem depender da posição do item na lista;
- versão obrigatória do schema;
- suporte a overrides esparsos sobre a baseline atual;
- ausência de um campo significa preservar o valor do build;
- rejeição de `NaN` e infinito;
- ranges validados;
- detecção de entradas duplicadas;
- verificação de negativos e prints desconhecidos;
- hash do catálogo de perfis;
- ordem determinística para facilitar comparação em Git;
- compatibilidade explícita entre versões.

---

## 11. Exportação no modo Pro Calibration

O modo Calibration do build Pro deve permitir trabalhar em três escopos.

### Calibração do negativo

Botões sugeridos:

- Save Negative Calibration;
- Reset Negative Calibration.

Salva os valores associados somente ao negativo selecionado.

### Calibração do print

Botões sugeridos:

- Save Print Calibration;
- Reset Print Calibration.

Salva os valores associados somente ao print selecionado.

### Calibração da combinação

Botões sugeridos:

- Save Negative/Print Calibration;
- Reset Negative/Print Calibration.

Salva os valores que dependem da combinação dos dois materiais.

### Exportação final

Botões sugeridos:

- Validate Production Calibration;
- Export Production Calibration.

A exportação deve:

1. reunir todas as entradas;
2. validar o schema;
3. validar IDs e ranges;
4. garantir cobertura mínima;
5. ordenar deterministicamente;
6. calcular hash;
7. gravar de forma atômica;
8. mostrar o caminho final ao usuário.

---

## 12. Geração no build

Criar:

```text
tools/generate_calibration_header.py
```

Entrada:

```text
Resources/calibration/spektrafilm_calibration.json
```

Saídas:

```text
generated/SpektraGeneratedCalibration.h
generated/SpektraGeneratedCalibration.cpp
```

Exemplo CMake:

```cmake
set(
  SPEKTRAFILM_CALIBRATION_JSON
  "${CMAKE_CURRENT_SOURCE_DIR}/Resources/calibration/spektrafilm_calibration.json"
)

set(
  SPEKTRAFILM_GENERATED_CALIBRATION_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/SpektraGeneratedCalibration.h"
)

set(
  SPEKTRAFILM_GENERATED_CALIBRATION_SOURCE
  "${CMAKE_CURRENT_BINARY_DIR}/generated/SpektraGeneratedCalibration.cpp"
)

add_custom_command(
  OUTPUT
    "${SPEKTRAFILM_GENERATED_CALIBRATION_HEADER}"
    "${SPEKTRAFILM_GENERATED_CALIBRATION_SOURCE}"
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_calibration_header.py"
    --input "${SPEKTRAFILM_CALIBRATION_JSON}"
    --header "${SPEKTRAFILM_GENERATED_CALIBRATION_HEADER}"
    --source "${SPEKTRAFILM_GENERATED_CALIBRATION_SOURCE}"
  DEPENDS
    "${SPEKTRAFILM_CALIBRATION_JSON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_calibration_header.py"
  VERBATIM
)
```

O modo Production do build Pro deverá falhar quando:

- o JSON não existir;
- o schema for inválido;
- os IDs não corresponderem ao catálogo;
- houver valores fora dos ranges permitidos;
- faltar alguma calibração marcada como obrigatória.

O modo Pro Calibration poderá usar defaults quando o arquivo ainda não existir,
desde que deixe isso claro na interface.

---

## 13. Resolução dos parâmetros efetivos

Deve existir uma etapa única para combinar:

1. defaults estruturais;
2. perfil espectral;
3. calibração global;
4. calibração do negativo;
5. calibração do print;
6. calibração da combinação;
7. ajustes públicos do usuário.

Fluxo sugerido:

```cpp
RenderParams readParams(InstanceData *data, OfxTime time) {
  PublicControls controls = readPublicControls(data, time);
  TechnicalControls technical = readTechnicalControls(data, time);

  const CalibrationView calibration =
      calibrationFor(controls.film, controls.paper);

  RenderParams params =
      makeBaseRenderParams(controls, technical, calibration);

  applyPublicAdjustments(params, controls, calibration);
  sanitizeRenderParams(params);
  return params;
}
```

### Regra importante

O renderer nunca deve precisar saber:

- qual modo de build Pro está ativo;
- se o valor veio da UI;
- se o valor veio do JSON;
- se o valor veio de uma calibração por stock.

Metal e Vulkan devem receber somente os valores finais.

---

## 14. Parâmetros secretos e compatibilidade OFX

Existem duas estratégias possíveis.

### Definir todos os parâmetros e esconder os técnicos

Vantagens:

- maior compatibilidade entre projetos;
- menor quantidade de caminhos condicionais;
- handles continuam disponíveis;
- snapshots antigos têm maior chance de funcionar.

Desvantagens:

- os parâmetros ainda existem no descriptor;
- alguns hosts ou scripts podem encontrá-los;
- aumenta a superfície interna do modo Pro Production.

### Não definir parâmetros técnicos no modo Pro Production

Vantagens:

- interface e descriptor realmente menores;
- menor exposição dos controles internos.

Desvantagens:

- exige tratamento cuidadoso de handles ausentes;
- `readParams()` não pode depender desses parâmetros;
- aumenta a diferença entre os descriptors Calibration e Production.

### Recomendação

Primeira etapa:

- manter todos definidos;
- esconder os parâmetros técnicos;
- mover a fonte oficial dos valores para a calibração compilada.

Etapa posterior:

- avaliar a remoção física dos parâmetros técnicos no modo Pro Production
  depois dos testes de compatibilidade com Resolve, Nuke e outros hosts.

---

## 15. Presets, defaults e calibração

Esses três conceitos devem permanecer separados.

### Calibração Pro

Define o comportamento técnico do produto e entra no build.

### Defaults

Define os valores iniciais dos controles públicos em uma nova instância.

### Presets

Armazena escolhas criativas feitas pelo usuário.

Um preset da interface Pro Production não deve sobrescrever valores técnicos de
calibração.

Mesmo que o código atual de `Save Preset` seja reaproveitado internamente como
referência, o botão público `Save Preset` continuará significando preset de
usuário. A calibração técnica deverá ser salva por ações separadas no modo
interno, por exemplo:

- `Save Global Calibration`;
- `Save Negative Calibration`;
- `Save Print Calibration`;
- `Save Pair Override`;
- `Export Production Calibration`.

O snapshot usado pelos presets Pro Production deve capturar apenas:

- input/output;
- exposição;
- seleção de negativo e print;
- push/pull e formatos;
- trims criativos de printer light;
- bleach bypass;
- outros controles públicos futuros.

### Estado atual dos presets

O código já possui suporte a presets `.spkpreset`, incluindo salvar, listar e
carregar. Entretanto:

- não existe nenhum `.spkpreset` versionado dentro do projeto;
- não foi encontrado nenhum `.spkpreset` na pasta pessoal padrão da máquina
  analisada;
- não foi encontrado arquivo de defaults persistentes do SpektraFilm nessa
  máquina.

Portanto, não há atualmente um acervo conhecido de presets externos que precise
ser migrado.

Também foi definido que não existem projetos antigos que precisem ser migrados
para o novo Production.

As decisões de implementação serão:

1. preservar o plugin ID `org.spektrafilm`;
2. preservar os parâmetros internos `film` e `paper`;
3. preservar seus índices originais;
4. mapear os novos dropdowns públicos para esses parâmetros internos;
5. criar presets novos capturando somente os controles públicos.

---

## 16. Alterações previstas por arquivo

### `src/SpektraFilmPlugin.cpp`

- manter o flavor `Pro` como base única;
- adicionar detecção compile-time do modo Pro Calibration/Production;
- adicionar metadata específica da interface Pro reduzida;
- criar os grupos públicos do Pro Production;
- reorganizar e renomear os controles existentes aprovados;
- criar dropdowns públicos reduzidos de negativo e print;
- manter `film` e `paper` como parâmetros internos ocultos;
- mapear os dropdowns públicos para os índices internos originais;
- sincronizar índices internos antigos de volta para a UI quando possível;
- expor controles técnicos somente no modo Pro Calibration;
- esconder ou excluir controles técnicos no modo Pro Production;
- aplicar a matriz de estado dos grupos no modo Pro Calibration;
- habilitar `Filtering`, `Film`, `Print` e `DIR Couplers` para calibração;
- desativar `Film Plane`, `Grain`, `Halation`, `Diffusion`, `Scanner`, `Info`
  e `Manage` no modo Pro Calibration;
- criar um grupo dedicado `Calibration` para save, reset, validate e export;
- reaproveitar do grupo `Manage` os helpers de arquivo, snapshot, timestamp,
  mensagens e aplicação de parâmetros;
- não gravar a calibração como `.spkpreset` nem como `.spkdefaults`;
- criar um snapshot próprio de calibração, com whitelist e escopos técnicos;
- capturar calibração por whitelist, evitando snapshots indiscriminados;
- excluir controles públicos dos snapshots de calibração;
- neutralizar controles públicos durante a captura;
- separar leitura de controles públicos e técnicos;
- aplicar a calibração antes dos ajustes públicos;
- criar callbacks de exportação da calibração;
- aplicar regras de enabled/secret;
- limitar presets Pro Production aos parâmetros públicos;
- mostrar versão/hash da calibração no grupo `Calibration` interno e no
  manifest do bundle Production.

### `src/SpektraParameters.h`

- adicionar somente os campos finais realmente necessários;
- evitar colocar estruturas de persistência ou JSON dentro de `RenderParams`;
- preservar os campos existentes usados pelo renderer.

### `src/SpektraMetalRenderer.mm`

- preservar o processamento atual;
- manter a leitura de valores finais sem lógica de flavor ou build mode.

### `src/SpektraVulkanRenderer.cpp`

- preservar o processamento atual;
- manter paridade com Metal.

### `src/SpektraTooltips.h`

- tooltips para todos os controles públicos Pro;
- descrições diferentes para controles técnicos do modo Pro Calibration;
- documentação clara dos valores neutros.

### Metadata de parâmetros

A metadata deverá registrar, para cada parâmetro:

- grupo ao qual pertence;
- visibilidade no Pro Production;
- participação ou não na calibração;
- escopo global, negative, print ou pair;
- valor neutro usado durante a captura;
- estado enabled/disabled no modo Pro Calibration.

### Novo `src/SpektraCalibration.h/.cpp`

Responsabilidades sugeridas:

- estruturas de calibração;
- busca por ID;
- resolução negative/print/pair;
- fallback;
- aplicação ao `RenderParams`;
- exposição de versão e hash.

### `tools/generate_calibration_header.py`

- ler JSON;
- validar schema;
- validar ranges;
- validar IDs;
- ordenar dados;
- gerar C++ determinístico;
- gerar hash;
- falhar com mensagens claras.

### `CMakeLists.txt`

- gerar os arquivos de calibração;
- manter o target existente `spektrafilm`;
- desativar os targets Flow e Dev por padrão sem remover seu código;
- oferecer uma opção interna para reativar Flow e Dev em builds de
  desenvolvimento;
- adicionar `SPEKTRAFILM_PRO_BUILD_MODE`;
- definir compile definitions específicas para o target Pro;
- garantir que o modo Pro Production dependa da calibração gerada;
- impedir packaging quando o Pro estiver em modo Calibration;
- incluir somente o modo Pro Production no pacote público.

### `Resources/calibration/`

Arquivos sugeridos:

```text
Resources/calibration/
  spektrafilm_calibration.json
  spektrafilm_calibration.schema.json
  README.md
```

### `tests/`

Adicionar testes do schema, geração, interface, paridade e build.

---

## 17. Fases de implementação

## Fase 0 — Inventário e decisões

Objetivos:

- catalogar todos os parâmetros atuais;
- classificar cada parâmetro por grupo, escopo e participação na calibração;
- criar a whitelist inicial de parâmetros calibráveis;
- registrar os valores neutros dos controles públicos;
- definir IDs estáveis dos stocks;
- confirmar a lista de outputs SDR;
- manter Color Management fora da captura inicial;
- desativar Flow e Dev no build padrão e no release;
- manter todos os perfis compilados, limitando apenas a lista pública;

Classificação:

```text
PUBLIC_PRODUCTION
CALIBRATION_GLOBAL
CALIBRATION_NEGATIVE
CALIBRATION_PRINT
CALIBRATION_PAIR
FIXED_INTERNAL
DIAGNOSTIC_ONLY
DEPRECATED
```

Entrega:

- tabela completa de classificação;
- especificação dos controles públicos;
- decisão sobre ranges e valores neutros.

## Fase 1 — Interface Pro Production com defaults fixos

Objetivos:

- adicionar os dois modos de build ao target Pro existente;
- criar a interface reduzida no modo Pro Production;
- criar a interface técnica restrita do modo Pro Calibration;
- habilitar Filtering, Film, Print e DIR Couplers conforme a whitelist;
- desativar Film Plane, Grain, Halation, Diffusion, Scanner, Info e Manage;
- criar o novo grupo `Calibration`;
- usar os valores atuais do build como baseline oficial;
- iniciar sem overrides para garantir resultado idêntico ao build atual;
- fixar o output como `DisplaySdr`;
- usar `filmFormat` como formato global;
- manter o renderer inalterado.

Entrega:

- `spektrafilm.ofx.bundle` Pro funcional em modo Production;
- o mesmo target Pro compilável em modo Calibration;
- interface aproximada da lista desejada;
- nenhum sistema de calibração externo ainda.

## Fase 2 — Modelo de calibração

Objetivos:

- capturar Filtering no escopo global inicial;
- criar estruturas C++;
- criar JSON e schema;
- implementar gerador;
- incorporar calibração no build;
- mostrar versão/hash.

Entrega:

- Pro Production compilando com calibração versionada;
- falha de build para calibração inválida.

## Fase 3 — Ferramentas Pro Calibration

Objetivos:

- forçar controles públicos para valores neutros durante a captura;
- salvar calibração por negativo;
- salvar calibração por print;
- salvar calibração por combinação;
- resetar entradas;
- validar e exportar JSON.

Entrega:

- ciclo completo Pro Calibration → JSON → Pro Production.

## Fase 4 — Packaging e release

Objetivos:

- assinar/notarizar macOS;
- empacotar Windows;
- impedir que o target Pro seja empacotado em modo Calibration;
- validar que o pacote foi gerado com
  `SPEKTRAFILM_PRO_BUILD_MODE=PRODUCTION`;
- incluir versão/hash de calibração no manifest;
- gerar documentação final.

Entrega:

- `.ofx.bundle` de produção distribuível.

---

## 18. Testes por prioridade

Os testes serão divididos em:

- **P0 — obrigatório para implementar e funcionar**;
- **P1 — obrigatório antes da distribuição pública**;
- **P2 — recomendado para robustez e manutenção**.

### P0 — Schema e captura básica

- JSON válido;
- schema desconhecido;
- campo obrigatório ausente;
- ID duplicado;
- stock desconhecido;
- valor fora do range;
- `NaN` ou infinito;
- combinação duplicada;
- controles públicos ausentes do payload de calibração;
- grupos fora do escopo ausentes do payload.

### P2 — Geração determinística

- mesmo JSON produz exatamente o mesmo C++;
- alteração de valor muda o hash;
- ordem diferente no JSON gera saída ordenada equivalente;
- mensagens de erro identificam o campo inválido.

### P0 — Resolução e mapeamento

- busca de negativo existente;
- busca de print existente;
- busca de combinação existente;
- fallback para negative + print quando a combinação não existe;
- uso de uma calibração de print com diferentes negativos;
- aplicação de correção esparsa somente quando uma combinação estiver
  cadastrada;
- prevenção de index out of range;
- IDs estáveis após reorganizar a lista da UI;
- Filtering aplicado a partir da calibração global;
- controles públicos ignorados pelo snapshot de calibração;
- grupos fora do escopo ignorados pelo snapshot de calibração.

### P0 — Paridade funcional

Para cada stock selecionado:

```text
Pro Calibration com controles públicos neutros
==
Pro Production com a mesma calibração
```

Comparar:

- erro máximo absoluto;
- RMSE;
- diferença perceptual;
- imagens de chart;
- imagens reais.

### P1 — Paridade Metal/Vulkan

- smoke render básico em cada backend disponível;
- mesmos parâmetros efetivos;
- buffers com layout compatível;
- novos campos inicializados;
- tolerância de paridade documentada.

### P0 — Interface

- apenas grupos aprovados visíveis;
- nenhum controle técnico visível no modo Pro Production;
- Filtering habilitado no modo Pro Calibration;
- Film, Print e DIR Couplers habilitados somente nos controles calibráveis;
- Film Plane, Grain, Halation, Diffusion, Scanner, Info e Manage desativados
  no modo Pro Calibration;
- grupo dedicado `Calibration` habilitado no modo Pro Calibration;
- controles públicos mantidos em valor neutro durante a captura;
- outputs disponíveis são exclusivamente SDR;
- `outputRole` permanece fixado em `DisplaySdr`;
- Film Format usa o parâmetro global;
- somente negativos e prints de cinema aprovados aparecem nas listas;
- dropdowns públicos mapeiam para os índices internos `8, 9, 11, 12` e `6, 7`;
- printer lights RGB ficam disponíveis como trims criativos independentes;
- printer lights RGB não entram no snapshot de calibração técnica;
- controles indisponíveis ficam disabled, não quebrados.

### P1 — Hosts oficialmente suportados

O escopo oficial permanecerá restrito aos hosts já declarados e contemplados
pelo projeto:

- DaVinci Resolve macOS;
- DaVinci Resolve Windows;
- Nuke nas plataformas para as quais o projeto já produzir um bundle funcional;
- reabertura de projeto;
- copy/paste de node;
- presets;
- keyframes;
- render cache.

Não será adicionada implementação específica para Fusion Studio, Natron,
Baselight, SCRATCH ou outros hosts nesta fase. Compatibilidade incidental com
outros hosts OFX não significa suporte oficial.

### P1 — Packaging e release

- ferramentas e controles Pro Calibration ausentes do pacote público;
- build mode registrado como `PRODUCTION`;
- JSON fonte não incluído desnecessariamente;
- calibração compilada presente;
- manifest correto;
- assinatura válida;
- notarização válida;
- instalação e remoção.

---

## 19. Critérios de aceitação

O projeto estará pronto para produção quando:

1. o target Pro puder ser compilado nos modos Calibration e Production;
2. o bundle Pro Production tiver a interface reduzida;
3. a calibração puder ser exportada de forma determinística;
4. o modo Pro Production incorporar a calibração;
5. o bundle Pro Production não depender de arquivos externos;
6. parâmetros públicos neutros reproduzirem o modo Pro Calibration;
7. Metal e Vulkan estiverem dentro da tolerância definida;
8. Output Display expuser somente outputs SDR;
9. Film Format utilizar o parâmetro global existente;
10. somente Filtering, Film, Print e DIR Couplers participarem da calibração
    inicial;
11. os grupos fora do escopo permanecerem desativados no Pro Calibration;
12. controles públicos não forem gravados como calibração;
13. a calibração de cada print funcionar com todos os negativos aprovados sem
    exigir uma matriz completa de combinações;
14. correções por combinação permanecerem opcionais;
15. presets Pro Production não modificarem calibração técnica;
16. o pacote público não contiver ferramentas do modo Calibration;
17. Flow e Dev não forem compilados nem empacotados por padrão;
18. os índices internos originais dos quatro negativos e dois prints forem
    preservados;
19. versão e hash da calibração puderem ser identificados;
20. um arquivo de overrides vazio reproduzir exatamente a baseline atual;
21. os testes de host e packaging passarem.

---

## 20. Riscos e cuidados

### Alterar IDs de parâmetros OFX

Renomear labels é seguro. Renomear IDs internos pode quebrar projetos existentes.

Recomendação:

- manter IDs quando o comportamento for equivalente;
- evitar mudanças desnecessárias nos IDs internos.

### Usar índices como identidade dos stocks

Índices podem mudar quando perfis são adicionados ou reordenados.

Recomendação:

- usar IDs textuais estáveis;
- converter ID para índice durante a geração.

### Depender de parâmetros secretos

Hosts podem tratar parâmetros secretos de maneiras diferentes.

Recomendação:

- usar a calibração compilada como fonte oficial;
- usar parâmetros secretos apenas para compatibilidade.

### Instalar os dois modos Pro simultaneamente

Como Calibration e Production compartilham `org.spektrafilm`, o host poderá
manter cache do descriptor ou carregar o binário incorreto.

Recomendação:

- nunca instalar os dois modos simultaneamente;
- usar diretórios de build separados;
- remover ou substituir o bundle Pro antes de trocar de modo;
- reiniciar o host e limpar seu cache de plugins quando necessário;
- adicionar um label visível `Calibration Build` no modo interno.

---

## 21. Ordem recomendada para começar

Primeiro conjunto de alterações:

1. adicionar `SPEKTRAFILM_PRO_BUILD_MODE` ao CMake;
2. aplicar a opção somente ao target `spektrafilm` de flavor Pro;
3. criar a matriz de grupos e a whitelist de parâmetros calibráveis;
4. criar tabela explícita de parâmetros públicos;
5. construir os grupos reduzidos no modo Pro Production;
6. configurar os estados enabled/disabled do modo Pro Calibration;
7. criar o grupo dedicado `Calibration`;
8. limitar Output Display aos espaços SDR;
9. usar diretamente o `filmFormat` global;
10. criar `SpektraCalibration` com valores provisórios;
11. adicionar teste de paridade entre os dois modos Pro.

Segundo conjunto:

1. definir o schema JSON;
2. criar gerador C++;
3. incorporar JSON no CMake;
4. implementar exportação no modo Pro Calibration;
5. validar o ciclo completo.

Terceiro conjunto:

1. validar visualmente todos os controles públicos;
2. executar testes Metal/Vulkan e de host;
3. finalizar packaging.

---

## 22. Estado das decisões

Não existe decisão bloqueante para iniciar a calibração baseada no pipeline
atual.

Printer points físicos foram removidos deste plano. Se no futuro houver
interesse nessa abordagem, será criado um plano separado.

---

## 23. Implementação iniciada

Primeira rodada implementada:

- adicionado `SPEKTRAFILM_PRO_BUILD_MODE` no CMake;
- modos aceitos: `PRODUCTION` e `CALIBRATION`;
- default do target Pro permanece `PRODUCTION`;
- o target Pro `spektrafilm` recebe o macro compile-time;
- adicionados helpers internos:
  - `isProProductionBuild()`;
  - `isProCalibrationBuild()`;
- adicionados grupos públicos de Production:
  - `Technical Controls`;
  - `Camera Settings`;
  - `Film Stocks`;
  - `Laboratory`;
- adicionados choices públicos reduzidos:
  - `Profile Negative`;
  - `Profile Print`;
- `Profile Negative` mapeia para os índices internos originais:
  - `8`: Kodak Vision3 50D;
  - `9`: Kodak Vision3 250D;
  - `11`: Kodak Vision3 200T;
  - `12`: Kodak Vision3 500T;
- `Profile Print` mapeia para os índices internos originais:
  - `6`: Kodak 2383;
  - `7`: Kodak 2393;
- adicionados controles criativos:
  - `Red Printer Light`;
  - `Green Printer Light`;
  - `Blue Printer Light`;
- os printer lights criativos são convertidos para trims C/M/Y no pipeline
  atual `Filtered Enlarger`;
- no modo Pro Production, o render força:
  - `PrintSimulation`;
  - `DisplaySdr`;
  - `Color Adaptation` desligado;
  - `Filtered Enlarger`;
  - `Auto Exposure` desligado;
  - `Print Push/Pull` neutro;
  - `Film Plane`, `Grain`, `Halation`, `Diffusion` e `Scanner` desligados;
- modo Pro Calibration mostra os grupos técnicos principais e um grupo
  `Calibration` inicial;
- os dois modos foram compilados com sucesso:
  - `build-release`: `spektrafilm` em Production;
  - `build-calibration`: `spektrafilm` em Calibration.

Ainda não implementado nesta rodada:

- botões reais de salvar/exportar calibração;
- persistência real do `CalibrationSnapshot`;
- incorporação de arquivo de calibração no bundle Production;
- botões reais conectados ao `CalibrationSnapshot`.

Segunda rodada implementada:

- adicionada whitelist explícita de controles públicos do Pro Production;
- adicionada whitelist inicial de controles calibráveis;
- criado `SnapshotScope`;
- escopos disponíveis:
  - `All`;
  - `ProductionPublic`;
  - `Calibration`;
- criado esqueleto inicial de `CalibrationSnapshot`;
- defaults persistentes no Pro Production agora ignoram parâmetros que não
  pertencem à whitelist pública;
- `Copy Params` no Pro Production captura somente controles públicos;
- `Paste Params` no Pro Production aplica somente controles públicos;
- `Save Preset` no Pro Production salva somente controles públicos;
- `Load Preset` no Pro Production aplica somente controles públicos;
- `Set Defaults` no Pro Production salva somente controles públicos;
- presets antigos ou completos podem ser lidos, mas no Production apenas a
  parte pública será aplicada;
- recompilado com sucesso:
  - `build-release`: Pro Production;
  - `build-calibration`: Pro Calibration.

Ainda não implementado após a segunda rodada:

- carregamento/merge de múltiplos arquivos de calibração;
- exportação/incorporação da calibração no bundle Production;
- validação visual no host.

Terceira rodada implementada:

- adicionados botões internos no grupo `Calibration`:
  - `Save Global Calibration`;
  - `Save Negative Calibration`;
  - `Save Print Calibration`;
  - `Save Negative/Print Calibration`;
  - `Export Production Calibration`;
- implementada primeira serialização própria:
  - formato: `lookfilmlab-calibration-v1`;
  - extensão: `.lookcalibration.json`;
  - destino: `~/Documents/MCLookFilmLab/calibration`;
- cada captura grava:
  - `created`;
  - `plugin`;
  - `version`;
  - `capture_kind`;
  - `film_index`;
  - `paper_index`;
  - seções `global`, `negative`, `print` e `pairOverride`;
- a captura usa somente `SnapshotScope::Calibration`;
- `Export Production Calibration` preenche as quatro seções iniciais a partir
  do estado calibrável atual;
- escrita feita de forma atômica com arquivo temporário;
- o host mostra mensagem com o caminho salvo;
- recompilado com sucesso:
  - `build-release`: Pro Production;
  - `build-calibration`: Pro Calibration.

Ainda não implementado após a terceira rodada:

- merge avançado de múltiplos arquivos de calibração;
- validação visual no DaVinci Resolve.

Quarta rodada implementada:

- adicionado `SPEKTRAFILM_PRO_CALIBRATION_FILE` no CMake;
- quando informado, esse arquivo é copiado para o bundle Pro como:
  - `Resources/production_calibration.lookfilmlab.json`;
- o Pro Production procura esse arquivo no bundle;
- se o arquivo não existir ou não for reconhecido, o Production mantém a
  baseline atual;
- implementado parser para `lookfilmlab-calibration-v1`;
- implementada leitura das seções:
  - `global`;
  - `negative`;
  - `print`;
  - `pairOverride`;
- implementada aplicação em `RenderParams` dos parâmetros calibráveis
  suportados;
- a aplicação ocorre no Pro Production depois dos locks técnicos do modo
  Production e antes dos printer lights criativos;
- o helper de localização de recursos do bundle foi generalizado e também
  passou a atender `manual.pdf`;
- recompilado com sucesso:
  - `build-release`: Pro Production;
  - `build-calibration`: Pro Calibration.

Ordem atual do render no Pro Production:

```text
baseline atual do build
+ parâmetros OFX ocultos/defaults
+ seleção pública de negativo e print
+ locks técnicos Production
+ calibração do bundle, se existir
+ controles públicos criativos
= RenderParams final
```

Ainda não implementado após a quarta rodada:

- seleção/merge de vários arquivos de calibração independentes;
- validação de hash/versão da calibração no manifest;
- botão para carregar calibração de volta no modo Calibration;
- validação visual no DaVinci Resolve.

Reversão da tentativa de ocultar títulos de grupos:

- a tentativa de transformar a UI Pro Production em controles sem grupo foi
  descartada;
- o modo Pro Production deve continuar usando os grupos reduzidos aprovados:
  - `Technical Controls`;
  - `Camera Settings`;
  - `Film Stocks`;
  - `Laboratory`;
- os controles públicos devem permanecer ligados aos seus respectivos grupos
  via `kOfxParamPropParent`;
- `groupVisibleInFlavor()` deve manter visíveis somente os grupos públicos do
  Production;
- qualquer ajuste futuro para esconder ou suavizar o texto visual dos grupos
  deve ser feito sem remover grupos, sem remover parents e sem alterar a
  hierarquia funcional que já foi validada no host.

Ajuste posterior validado em código:

- no Pro Production, os grupos técnicos antigos não devem ser definidos no
  descriptor, porque o DaVinci Resolve ainda pode exibir headers de grupos
  marcados apenas como secretos;
- os únicos grupos definidos/visíveis no Pro Production são:
  - `colorGroup`, exibido como `Technical Controls`;
  - `productionCameraGroup`, exibido como `Camera Settings`;
  - `productionStocksGroup`, exibido como `Film Stocks`;
  - `productionLaboratoryGroup`, exibido como `Laboratory`;
- parâmetros ocultos no Pro Production não devem receber parent para grupos
  que não existem no descriptor desse build;
- além dos grupos, o Pro Production também deve limitar o descriptor aos
  controles públicos aprovados; qualquer controle fora de `Technical Controls`,
  `Camera Settings`, `Film Stocks` e `Laboratory` deve ficar oculto/não
  disponível na UI;
- parâmetros técnicos internos só podem existir no Pro Production quando forem
  indispensáveis para estado interno e sempre como secretos, sem aparecer na
  interface;
- no Pro Calibration, a UI técnica original deve permanecer disponível para
  calibração, incluindo grupos como `Filtering`, `Film Plane`, `Film`, `Print`,
  `DIR Couplers`, `Grain`, `Halation`, `Diffusion`, `Scanner`, `Info` e
  `Manage`;
- os grupos públicos específicos do Pro Production continuam ocultos no Pro
  Calibration para evitar duplicidade de controles.

Master ativo de calibração implementado:

- o arquivo oficial de continuidade passa a ser:
  - `~/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json`;
- todo save/export no Pro Calibration continua criando um arquivo histórico com
  timestamp e também atualiza o master ativo;
- saves parciais atualizam somente a seção correspondente no master ativo:
  - `Save Global Calibration` atualiza `global`;
  - `Save Negative Calibration` atualiza `negative`;
  - `Save Print Calibration` atualiza `print`;
  - `Save Negative/Print Calibration` atualiza `pairOverride`;
  - `Export Production Calibration` atualiza todas as seções;
- o master ativo recebe metadados identificáveis:
  - `role: active-work-master`;
  - `product: LookFilmLab`;
  - `purpose: Production build calibration master`;
  - `source_build_mode: CALIBRATION`;
  - `calibration_id: active_production_calibration`;
- o Pro Calibration tenta carregar automaticamente esse master ativo ao criar a
  instância, permitindo continuar a calibração de onde parou;
- adicionado botão interno `Load Active Calibration` para recarregar o master
  manualmente;
- adicionado label `Active Master` no grupo `Calibration` mostrando o caminho
  esperado do arquivo;
- o Pro Production continua apenas lendo a cópia embutida no bundle; ele não
  escreve nem altera o master ativo.
