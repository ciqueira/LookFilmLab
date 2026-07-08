# Scanned Negative Print Bypass Plan

## Checkpoint Atual

Status: MVP implementado e validado em testes manuais iniciais no DaVinci Resolve usando timeline normal.

O recurso atual adiciona um caminho de print para scan/captura de negativo fisico real. O modo novo fica em `Process = Print simulation`, com `Print Source = Scanned Negative / Bypass Negative`.

Decisao importante do checkpoint:

- O caminho `Neutral CMY` foi mantido como aproximacao pratica, nao como simulacao espectral completa de print optico.
- O negativo fisico ja esta presente na imagem de entrada; portanto o bypass nao cria um negativo digital nem deve depender do `Negative Stock`.
- No modo `Scanned Negative / Bypass Negative`, a UI do grupo `Film` e escondida e os parametros internos de filme sao fixados para evitar que o stock negativo contamine o resultado.
- `Print Stock`/`Paper` continua ativo e e a principal simulacao criativa/fisica do lado do papel.
- `Film Base RGB`, `Black / Flare RGB`, `Scan Exposure EV`, `Density Scale RGB` e `Density Offset RGB` sao os controles principais para preparar o scan antes do print.

Fluxo implementado hoje:

```text
Scan/captura de negativo fisico
-> decodificacao por Scan Input Color Space
-> conversao para Scan Working Space linear
-> normalizacao por film base / black flare / exposure
-> densidade CMY neutra aproximada
-> simulacao de papel/print
-> scan/output final
```

Limite fisico atual:

```text
O caminho Neutral CMY nao simula luz espectral atravessando o negativo fisico real.
```

Para isso, seria necessario estimar ou medir densidade espectral do negativo real. O caminho `Selected Negative Stock` preserva uma opcao comparativa, mas usa a base espectral do stock escolhido na OFX, nao a densidade espectral medida do negativo do usuario.

## Resultado Dos Testes Manuais

Observacoes ate este checkpoint:

- Timeline normal no DaVinci Resolve funcionou melhor que a nova aba/pagina de Photos para este fluxo.
- Para foto, `sRGB / Linear` no input do Resolve e saida `sRGB / sRGB` funcionaram bem no teste atual.
- O erro anterior de export/still mais opaco foi associado ao contexto da aba Photos/album e/ou gerenciamento de cor do Resolve, nao ao shader principal da OFX.
- `Film Base RGB` foi essencial para neutralizar a mascara/base do negativo.
- `Scan Exposure EV` em torno de `+1.0` foi util no teste com RAW Canon `.CR2`.
- `Print Bleach Bypass` em torno de `0.5` ajudou a reduzir saturacao no resultado.
- O vermelho saturado observado em builds anteriores foi reduzido apos corrigir cache/build e isolar o `Negative Stock`.

## Atualizacao: Scan Input Color Space

O controle simples `Scan Input Encoding` foi substituido na UI por um controle mais completo:

```text
Scan Input Color Space
```

Tambem foi adicionado:

```text
Scan Working Space
```

Resultado implantado:

- suportar melhor `sRGB`, `Adobe RGB (1998)`, `Display P3`, `ProPhoto RGB`, `Rec.709` e entradas lineares;
- funcionar melhor com `.CR2`, `.JPG`, `.PNG`, `.TIFF` e outros formatos que o Resolve decodifica antes de entregar pixels para a OFX;
- respeitar nao apenas gamma/transfer, mas tambem primarias/gamut;
- reaproveitar o sistema de color spaces que a OFX ja possui;
- converter scan/base/flare para um espaco linear comum antes de calcular densidade.

Opcoes de entrada implantadas:

- `sRGB/Rec.709 / Linear`
- `sRGB / sRGB`
- `Adobe RGB (1998) / Gamma 2.199`
- `Display P3 / sRGB`
- `ProPhoto RGB / ProPhoto`
- `Rec.709 / Gamma 2.4`
- `Rec.709 / Gamma 2.2`
- `P3-D65 / Linear`
- `Rec.2020 / Linear`
- `ACEScg / Linear`

Opcoes de espaco interno implantadas:

- `Rec.2020 / Linear` como default amplo
- `ACEScg / Linear` para comparacao wide-gamut
- `sRGB/Rec.709 / Linear` como baseline pequeno

Limite ainda existente:

- `Adobe RGB (1998) / Linear` nao foi exposto como opcao separada porque ainda nao existe como perfil/matriz no gerador de color spaces atual.

Matriz de testes sugerida para o proximo ciclo:

- Canon RAW `.CR2`;
- JPG sRGB;
- PNG sRGB;
- TIFF sRGB;
- TIFF Adobe RGB;
- JPG Adobe RGB, se o Resolve preservar/interpretar o perfil;
- teste separado em timeline normal e na aba Photos.

## Objetivo

Adicionar ao SpektraFilm OFX um caminho de print para scans/capturas de negativo fisico real. O novo caminho deve permitir usar uma captura RAW linear de um negativo fotografado por camera digital, normalizar essa captura para uma representacao aproximada de densidade de negativo, e alimentar o simulador de print/papel sem passar pela simulacao de negativo digital existente.

Fluxo atual principal:

```text
Imagem positiva digital
-> simulacao de negativo
-> simulacao de print/papel
-> positivo final
```

Fluxo desejado:

```text
Scan/captura linear de negativo fisico
-> normalizacao de base/luz/flare
-> conversao para densidade CMY aproximada
-> simulacao de print/papel
-> positivo final
```

O recurso deve ser valido somente para `Process = Print simulation`. `Scan negative` e `Process negative` continuam com a semantica atual.

## Contexto Atual Do Codigo

Arquivos relevantes:

- `src/SpektraParameters.h`
  - Define `ProcessMode` com `PrintSimulation`, `ScanNegative` e `ProcessNegative`.
  - Contem `RenderParams`, que deve receber os novos parametros resolvidos.

- `src/SpektraAppBridge.mm`
  - Define os parametros expostos ao app/OFX.
  - O dropdown atual `process` fica no grupo `color` e tem as opcoes `Print simulation`, `Scan negative` e `Process negative`.
  - Ja existem controles de print como `paper`, `printTiming`, `printExposureEv`, filtros CMY e printer points.

- `shaders/vulkan/SpektraPrintScan.comp`
  - Contem o ponto mais importante do pipeline:
    - `timedPrintRaw(vec3 filmDensityCmy)`: print a partir de densidade de negativo.
    - `timedPrintRawFromNegativeLight(vec3 inputRgb)`: cria raw de print a partir de imagem positiva/luz de negativo.
    - `developPrintDensity(...)`: desenvolve densidade do papel/print.
    - `scanDensityToOutputRgbLinearY(..., printStage=true)`: escaneia/renderiza densidade de print para saida.
  - No final, `PrintSimulation` usa `timedPrintRaw(pixel.rgb)`, assumindo que `pixel.rgb` ja e densidade de negativo gerada antes no pipeline.
  - `ProcessNegative` usa `timedPrintRawFromNegativeLight(pixel.rgb)`, ou seja, gera o negativo internamente a partir da imagem de entrada.

- `src/SpektraMetalRenderer.mm` e `src/SpektraVulkanRenderer.cpp`
  - Orquestram as passagens e buffers.
  - O Vulkan ja tem `kPrintScanOpPrintRaw`, `kPrintScanOpFinalFromPrintRaw` e `kPrintScanOpPrintRawFromNegativeLight`.
  - Metal tem logica equivalente e precisa receber a mesma alteracao funcional.

## Decisao De UI

Adicionar um dropdown no grupo `print`, visivel/ativo apenas quando `Process = Print simulation`.

### Novo controle: `printSource`

Label sugerido: `Print Source`

Opcoes:

1. `Full Pipeline`
   - Mantem o comportamento atual.
   - Entrada esperada: imagem positiva digital.
   - Fluxo: positivo digital -> negativo simulado -> print.

2. `Scanned Negative / Bypass Negative`
   - Novo comportamento.
   - Entrada esperada: scan/captura de negativo fisico real.
   - Fluxo: scan linear do negativo -> densidade CMY aproximada -> print.

Nome interno sugerido:

```cpp
enum class PrintSourceMode : int32_t {
  FullPipeline = 0,
  ScannedNegativeBypass = 1,
};
```

Adicionar em `RenderParams`:

```cpp
PrintSourceMode printSource = PrintSourceMode::FullPipeline;
```

## Controles Necessarios Para O Bypass

Criar um grupo ou subgrupo no grupo `print`, por exemplo `Scanned Negative Input`, ativo apenas quando:

```text
process == PrintSimulation && printSource == ScannedNegativeBypass
```

### MVP

1. `scanInputEncoding`
   - Label: `Scan Input Encoding`
   - Opcoes implementadas: `Linear`, `sRGB`, `Rec.709 / Gamma 2.4`, `Rec.709 / Gamma 2.2`
   - Default: `Linear`
   - Motivo: o usuario pode mandar RAW da Canon pelo Resolve em gamma linear, ou materiais ja renderizados/gerenciados como sRGB/Rec.709.

2. `scanFilmBaseRgb`
   - Label: `Film Base RGB`
   - Tipo: `Double3`
   - Default inicial sugerido: `(1.0, 1.0, 1.0)`
   - Range sugerido: `0.001..4.0`
   - Motivo: representa a leitura RGB da borda nao exposta/base do filme, sob a luz e camera usadas no scan.

3. `scanBlackFlareRgb`
   - Label: `Black / Flare RGB`
   - Tipo: `Double3`
   - Default: `(0.0, 0.0, 0.0)`
   - Range sugerido: `0.0..0.25`
   - Motivo: remove offset de flare, preto levantado, vazamento de luz e nivel residual da captura.

4. `scanExposureEv`
   - Label: `Scan Exposure EV`
   - Tipo: `Double`
   - Default: `0.0`
   - Range sugerido: `-8.0..8.0`
   - Motivo: escala global do scan antes de calcular densidade.

5. `scanDensityScaleRgb`
   - Label: `Density Scale RGB`
   - Tipo: `Double3`
   - Default: `(1.0, 1.0, 1.0)`
   - Range implementado: `-4.0..4.0`
   - Motivo: compensa diferencas de resposta entre camera Canon, luz usada, mascara do negativo e camadas de cor do filme.

6. `scanDensityOffsetRgb`
   - Label: `Density Offset RGB`
   - Tipo: `Double3`
   - Default: `(0.0, 0.0, 0.0)`
   - Range sugerido: `-2.0..2.0`
   - Motivo: ajuste fino de equilibrio do negativo no dominio de densidade antes do print. Pode funcionar como correcao inicial antes dos printer points.

7. Reusar controles de print/papel existentes
   - `paper`, `printExposureEv`, `printGamma`, `printShadowShape`, `printHighlightShape` e `printBleachBypassAmount` continuam relevantes.
   - No caminho `Neutral CMY`, filtros espectrais de enlarger/preflash nao tem efeito util porque o negativo espectral foi removido do bypass.

### Controles Pos-MVP

Adicionar apenas se o MVP mostrar necessidade real:

- `scanMatrix3x3`: matriz de cor para compensar camera/luz.
- `scanDensityContrast`: contraste/pivot no dominio de densidade.
- `sampleFilmBase` interativo: amostrar a borda nao exposta do frame.
- `scanBasePreset`: presets por combinacao camera/luz/filme.

## Matematica Do Bypass

Para entrada linear, converter scan para densidade aproximada:

```glsl
vec3 scannedNegativeToFilmDensity(vec3 inputRgb) {
  vec3 rgb = inputRgb;

  if (scanInputEncoding == sRGB) {
    rgb = srgbToLinear(rgb);
  }

  rgb *= exp2(scanExposureEv);

  vec3 black = scanBlackFlareRgb;
  vec3 base = max(scanFilmBaseRgb - black, vec3(1.0e-6));
  vec3 normalized = clamp((rgb - black) / base, vec3(1.0e-6), vec3(1.0e6));

  // Negative density: darker transmitted light means higher density.
  vec3 density = -log10(normalized);
  density = density * scanDensityScaleRgb + scanDensityOffsetRgb;

  return max(density, vec3(0.0));
}
```

Observacoes:

- `Film Base RGB` deve ser medido na borda nao exposta do negativo. Essa area deve virar densidade perto de zero.
- Valores mais escuros que a base geram densidade positiva.
- Valores acima da base podem gerar densidade negativa; o MVP deve clampar para zero para evitar quebrar o print.
- O mapeamento RGB -> CMY e aproximado. O objetivo e alimentar o print com uma densidade calibravel, nao reconstruir espectralmente o negativo fisico.

## Encaixe No Pipeline

### Caminho atual preservado

`PrintSourceMode::FullPipeline` nao deve alterar nenhum resultado existente.

Caminho atual para `PrintSimulation`:

```text
input positivo
-> decode/input transform
-> film exposure/raw
-> developFilmDensity
-> timedPrintRaw(filmDensityCmy)
-> developPrintDensity
-> output scan
```

### Novo caminho

Para `Process = PrintSimulation` e `PrintSource = ScannedNegativeBypass`:

```text
input scan negativo
-> decode/input transform conforme inputColorSpace/input encoding
-> scannedNegativeToFilmDensity
-> timedPrintRaw(filmDensityCmy)
-> developPrintDensity
-> output scan
```

A implementacao deve evitar as etapas que criam negativo simulado a partir de imagem positiva:

- sem `processNegativeHanatosRaw(...)` para esse caminho;
- sem desenvolvimento de negativo baseado em curva de filme simulada;
- sem grain/halation/camera diffusion antes da densidade do negativo, salvo decisao futura explicita.

## Alteracoes Por Arquivo

### `src/SpektraParameters.h`

- Adicionar `PrintSourceMode`.
- Adicionar campos do MVP em `RenderParams`:
  - `printSource`
  - `scanInputEncoding`
  - `scanFilmBaseR/G/B`
  - `scanBlackFlareR/G/B`
  - `scanExposureEv`
  - `scanDensityScaleR/G/B`
  - `scanDensityOffsetR/G/B`

### `src/SpektraAppBridge.mm`

- Adicionar opcoes para `printSource` e `scanInputEncoding`.
- Adicionar parametros no grupo `print` ou novo grupo dedicado, mantendo tiers coerentes.
- Mapear os novos parametros para `RenderParams`.
- Definir defaults.
- Atualizar snapshots/defaults se o sistema de parametros exigir inclusao automatica.

### `src/SpektraFilmPlugin.cpp`

- Buscar/cachear os novos handles OFX se necessario.
- Atualizar logica de enable/visibility:
  - controles de scanned negative ativos somente em `PrintSimulation + ScannedNegativeBypass`.
  - controles existentes de Film podem ficar visiveis, mas alguns deixam de afetar o bypass; idealmente desabilitar ou documentar os que nao entram no caminho.
- Garantir que presets/defaults e clipboard preservem os novos parametros.

### `shaders/vulkan/SpektraPrintScan.comp`

- Adicionar constantes/uniforms para os novos parametros.
- Implementar `decodeScannedNegativeInput(...)` e `scannedNegativeToFilmDensity(...)`.
- No caminho final de `PrintSimulation`, escolher:

```glsl
vec3 filmDensity = printSource == ScannedNegativeBypass
  ? scannedNegativeToFilmDensity(inputScanRgb)
  : pixel.rgb;
vec3 printRaw = timedPrintRaw(filmDensity);
```

- Se o input bruto nao estiver disponivel nesse shader no ponto certo, criar uma op/passe dedicada antes de `kPrintScanOpPrintRaw` para converter source RGB em density buffer.

### `src/SpektraVulkanRenderer.cpp`

- Determinar se o caminho `ScannedNegativeBypass` deve:
  1. pular as passagens de film density existentes; ou
  2. reaproveitar o buffer de film density, preenchendo-o a partir do scan real.

Recomendacao MVP:

- Criar/usar um caminho que grava o buffer de densidade de filme a partir do source RGB.
- Depois reutilizar `kPrintScanOpPrintRaw` e `kPrintScanOpFinalFromPrintRaw`.
- Assim o print simulator continua recebendo `filmDensityCmy`, preservando boa parte da arquitetura existente.

### `shaders/SpektraFilm.metal`

- Espelhar a mesma funcao `scannedNegativeToFilmDensity`.
- Atualizar o caminho Metal equivalente ao Vulkan.
- Manter paridade visual entre Metal e Vulkan.

### `src/SpektraMetalRenderer.mm`

- Espelhar a orquestracao do novo caminho.
- Garantir que o bypass nao caia em `encodePrintRawFromNegativeLight`, pois essa funcao representa o fluxo de negativo simulado a partir de imagem positiva.

### `src/SpektraTooltips.h`

- Adicionar tooltips curtos explicando:
  - `Full Pipeline`: use com imagem positiva digital.
  - `Scanned Negative / Bypass Negative`: use com captura/scan linear de negativo fisico.
  - `Film Base RGB`: ajuste pela borda nao exposta do negativo.
  - `Black / Flare RGB`: remocao de offset antes da densidade.

## Comportamento De Controles Existentes

No modo `Scanned Negative / Bypass Negative`:

- `film`, `filmExposureEv`, `filmGamma`, `filmPushPullStops`, DIR e controles de desenvolvimento de negativo nao devem afetar a formacao do negativo, porque o negativo ja existe fisicamente.
- `paper`, `printExposureEv`, `printGamma`, `printShadowShape`, `printHighlightShape`, `printPushPullStops` e `printBleachBypassAmount` continuam relevantes.
- `C Filter`, `M Filter Shift`, `Y Filter Shift`, `Preflash M Filter Shift` e `Preflash Y Filter Shift` pertencem ao modelo espectral do enlarger/preflash. No caminho `Neutral CMY`, eles nao tem efeito util porque a densidade do scan e convertida para print raw de forma neutra, sem a tabela espectral do negativo.
- `Preflash Exposure` pode ter efeito neutro, mas os shifts de preflash nao participam do bypass neutro.
- Grain/halation/camera diffusion devem ser tratados com cautela. Para o MVP, preferir nao aplicar efeitos de camera/negativo que pertencem ao fluxo positivo->negativo simulado. Print diffusion pode continuar se estiver no caminho de print final.

## Validacao Visual

### Testes manuais

1. Baseline de regressao
   - `Print Source = Full Pipeline` deve bater visualmente com o build atual.

2. Scan linear de negativo fisico
   - Usar RAW Canon no Resolve com gamma linear ou sRGB/Rec.709 conforme o pipeline do Resolve.
   - Ajustar `Film Base RGB` usando borda nao exposta.
   - Ajustar `Black / Flare RGB` ate a base nao clipar e sombras ficarem estaveis.
   - Ajustar `Density Scale RGB` para neutralidade em cinzas conhecidos.
   - Usar `printExposureEv`, `printGamma`, `printBleachBypassAmount` e controles de papel para acertar densidade e saturacao do print.

3. Stress test
   - Negativo subexposto.
   - Negativo superexposto.
   - Borda de filme com flare/luz irregular.
   - Areas saturadas no scan.

### Testes automatizados sugeridos

- Teste de defaults: `FullPipeline` e valores neutros preservam parametros antigos.
- Teste matematico CPU ou shader fixture: `scan == filmBase` gera densidade zero.
- Teste matematico: `scan < filmBase` gera densidade positiva.
- Teste de clamp: valores zero/negativos nao geram NaN/Inf.
- Teste de snapshot/preset: novos parametros serializam e desserializam.

## Riscos E Limitacoes

1. Scan RGB nao e espectral
   - A camera Canon + luz nao capturam a resposta espectral completa do negativo.
   - Mitigacao: `Density Scale RGB`, printer points e futuro `scanMatrix3x3`.

2. Dependencia de linearidade
   - O melhor resultado depende de saber como o Resolve entregou os pixels para a OFX.
   - Mitigacao atual: `Scan Input Color Space`, respeitando transfer e primarias ja existentes no motor de cor.
   - Limitacao: combinacoes lineares que ainda nao existem como perfil, como Adobe RGB linear, precisam ser adicionadas ao gerador de color spaces antes de aparecerem no drop.

3. Base/mask mal medida
   - Se `Film Base RGB` estiver errado, todo o print fica com cor errada.
   - Mitigacao: documentar amostragem da borda nao exposta e futuramente criar sample tool.

4. Controles de negativo existentes podem confundir
   - No bypass, varios controles de `film` deixam de fazer sentido.
   - Mitigacao implementada: esconder o grupo `Film` e fixar internamente o `Negative Stock` durante o bypass.

5. Paridade Metal/Vulkan
   - O shader Metal e o Vulkan precisam implementar exatamente a mesma matematica.
   - Mitigacao: criar fixtures numericos simples e comparar resultados.

## Checkpoint: I/O Separado

Implementado o modelo de I/O separado para a OFX inteira:

- `Input Color Space` agora representa somente as primarias/gamut.
- `Input Gamma` representa a curva/transfer de entrada.
- `Output Color Space` representa somente as primarias/gamut de saida.
- `Output Gamma` representa a curva/transfer de saida.
- O antigo `Scan Input Color Space` fica oculto. No bypass, o scan usa o I/O principal e continua usando `Scan Working Space` como espaco linear interno.
- Metal e Vulkan foram alinhados para decodificar pela curva de entrada, converter pela matriz das primarias de entrada, processar o print e codificar pela curva de saida.
- O build `cmake --build build-calibration --target spektrafilm -- -j4` passou. Warning conhecido: helper Metal `spektra_scan_density_to_output_rgb` ainda nao usado.

Limitacoes remanescentes:

- `User Timeline` real ainda nao foi implementado, porque depende de confirmar se o host OFX expoe com seguranca a configuracao ativa do Resolve/RCM para o plugin.
- `P3 D60` e `Canon Log` original ainda nao aparecem como opcoes separadas porque nao existem como perfis gerados no motor atual.
- As combinacoes sao livres; se o usuario escolher primarias e gamma incompativeis com o material, a imagem pode ficar incorreta.

## Ordem De Implementacao

1. Adicionar enums e campos em `SpektraParameters.h`.
2. Adicionar parametros e defaults em `SpektraAppBridge.mm`.
3. Expor e condicionar a UI em `SpektraFilmPlugin.cpp`.
4. Implementar funcao de scan->density em Vulkan.
5. Ajustar orquestracao Vulkan para preencher/reusar buffer de densidade.
6. Espelhar shader e orquestracao em Metal.
7. Adicionar tooltips.
8. Rodar build Metal e, se disponivel, Vulkan.
9. Validar baseline `Full Pipeline`.
10. Validar com scan real linear de negativo fisico.

## Criterio De Aceite MVP

- O build atual permanece identico com `Print Source = Full Pipeline`.
- O modo `Scanned Negative / Bypass Negative` nao usa a simulacao de negativo digital.
- O print simulator recebe densidade CMY derivada do scan real.
- Entrada igual a `Film Base RGB` gera densidade proxima de zero.
- O usuario consegue positivar um scan linear de negativo fisico e usar `paper`, print exposure e printer points para formar uma imagem positiva com caracteristicas de print.
