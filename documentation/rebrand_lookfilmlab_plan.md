# Plano — Rebrand do build Pro para MC LookFilmLab

Objetivo: manter a estrutura técnica atual do projeto `spektrafilm-ofx-look`,
mas mudar a identidade pública do build Pro distribuível para a linha
`MC Plugins / LookFilmLab`.

---

## 1. Resultado desejado

### Grupo no DaVinci Resolve

De:

```text
spektrafilm OFX
```

Para:

```text
MC Plugins
```

### Nome exibido no DaVinci Resolve

Formato desejado:

```text
LookFilmLab v0.0.1
```

Onde `v0.0.1` não vem da versão do Core atual. Será lido de um novo arquivo:

```text
spektrafilm-ofx-look/VERSION
```

### Nome do bundle

De:

```text
spektrafilm.ofx.bundle
```

Para:

```text
MCLookFilmLab.ofx.bundle
```

### Identifier OFX / Bundle Identifier

De:

```text
org.spektrafilm
```

Para:

```text
com.mcplugins.lookfilmlab
```

---

## 2. Separação de versões

Existirão duas versões diferentes:

### Versão do Core

Continua como está hoje no CMake:

```cmake
SPEKTRAFILM_MACOS_VERSION
SPEKTRAFILM_WINDOWS_VERSION
SPEKTRAFILM_LINUX_VERSION
SPEKTRAFILM_VERSION
SPEKTRAFILM_VERSION_STRING
```

Essa versão continuará representando o Core interno SpektraFilm.

### Versão pública LookFilmLab

Será adicionada na raiz do source:

```text
spektrafilm-ofx-look/VERSION
```

Conteúdo inicial:

```text
0.0.1
```

Essa versão será usada para montar o nome público:

```text
LookFilmLab v0.0.1
```

---

## 3. Pontos de código/build que precisam mudar

## 3.1 CMakeLists.txt

Adicionar leitura do arquivo `VERSION`:

```cmake
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MCLOOKFILMLAB_VERSION LIMIT_COUNT 1)
```

Criar o label público:

```cmake
set(MCLOOKFILMLAB_DISPLAY_NAME "LookFilmLab v")
```

Alterar somente o target Pro:

Atual:

```cmake
add_spektra_ofx_plugin(spektrafilm spektrafilm spektrafilm org.spektrafilm 1 TRUE)
```

Desejado:

```cmake
add_spektra_ofx_plugin(
  spektrafilm
  MCLookFilmLab
  "${MCLOOKFILMLAB_DISPLAY_NAME}"
  com.mcplugins.lookfilmlab
  1
  TRUE
)
```

Observação importante:

- o target continua sendo `spektrafilm`;
- portanto o comando de build continua:

```bash
cmake --build build-release --target spektrafilm
```

Mas o artefato gerado muda para:

```text
MCLookFilmLab.ofx.bundle
```

---

## 3.2 Descriptor OFX

No arquivo:

```text
src/SpektraFilmPlugin.cpp
```

Alterar o grouping:

Atual:

```cpp
gPropHost->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, "spektrafilm OFX");
```

Desejado:

```cpp
gPropHost->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, "MC Plugins");
```

O label principal já vem do CMake via:

```cpp
SPEKTRAFILM_PLUGIN_LABEL
```

Então, após o ajuste no CMake, o DaVinci deve exibir:

```text
LookFilmLab v0.0.1
```

Também é recomendado definir short/long label, caso o host use variações:

```cpp
gPropHost->propSetString(props, kOfxPropLabel, 0, kPluginLabel);
gPropHost->propSetString(props, kOfxPropShortLabel, 0, kPluginLabel);
gPropHost->propSetString(props, kOfxPropLongLabel, 0, kPluginLabel);
```

---

## 3.3 Remoção de ícones

Hoje o plugin registra ícones no descriptor:

```cpp
gPropHost->propSetString(props, kOfxPropIcon, 0, svgIconFileForFlavor());
gPropHost->propSetString(props, kOfxPropIcon, 1, pngIconFileForFlavor());
```

E o CMake copia arquivos de:

```text
Resources/icons/
```

Mudança desejada:

- remover o registro de ícones no descriptor OFX;
- parar de copiar `Resources/icons` para dentro do bundle;
- remover dependências de ícones no CMake;
- manter o build puro, sem ícone próprio.

Decisão sugerida:

- remover uso/cópia dos ícones no build;
- remover o registro de ícones no descriptor OFX;
- remover fisicamente os arquivos fonte `Resources/icons/*`;
- remover a pasta `Resources/icons` quando vazia.

---

## 3.4 Info.plist

O `Info.plist` já usa variáveis do CMake:

```xml
<key>CFBundleExecutable</key>
<string>@SPEKTRAFILM_BUNDLE_EXECUTABLE@</string>

<key>CFBundleIdentifier</key>
<string>@SPEKTRAFILM_BUNDLE_IDENTIFIER@</string>

<key>CFBundleName</key>
<string>@SPEKTRAFILM_BUNDLE_NAME@</string>
```

Ao alterar `artifact_name`, `display_name` e `plugin_id`, o plist deve passar a
receber automaticamente:

```text
CFBundleExecutable = MCLookFilmLab.ofx
CFBundleIdentifier = com.mcplugins.lookfilmlab
CFBundleName = LookFilmLab v0.0.1
```

---

## 3.5 Manifest interno

O `plugin_manifest.json` também usa:

```cmake
SPEKTRAFILM_MANIFEST_ID
SPEKTRAFILM_MANIFEST_NAME
SPEKTRAFILM_MANIFEST_VERSION
```

Plano:

- `id`: usar `com.mcplugins.lookfilmlab`;
- `name`: usar `LookFilmLab v0.0.1`;
- `version`: manter a versão do Core atual por enquanto, a menos que
  posteriormente seja criado um campo separado para versão pública.

---

## 4. Instalação e limpeza

Como o nome do bundle vai mudar, o bundle antigo pode continuar instalado:

```text
/Library/OFX/Plugins/spektrafilm.ofx.bundle
```

E o novo será:

```text
/Library/OFX/Plugins/MCLookFilmLab.ofx.bundle
```

Para evitar duplicidade no DaVinci Resolve, durante os testes deve remover o
antigo:

```bash
sudo rm -rf "/Library/OFX/Plugins/spektrafilm.ofx.bundle"
sudo rm -rf "/Library/OFX/Plugins/MCLookFilmLab.ofx.bundle"
```

Depois instalar o build novo:

```bash
sudo cmake --install build-release
```

---

## 5. Comandos esperados após a mudança

Production com calibração:

```bash
cd "/Volumes/DataMEDIA/DEV/_OFX/MC OFX/spektrafilm-ofx-look"

cmake -S . -B build-release \
  -DSPEKTRAFILM_PRO_BUILD_MODE=PRODUCTION \
  -DSPEKTRAFILM_PRO_CALIBRATION_FILE="$HOME/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-release --target spektrafilm
sudo cmake --install build-release
```

Artifact esperado:

```text
build-release/MCLookFilmLab.ofx.bundle
```

Instalado em:

```text
/Library/OFX/Plugins/MCLookFilmLab.ofx.bundle
```

Calibration:

```bash
cd "/Volumes/DataMEDIA/DEV/_OFX/MC OFX/spektrafilm-ofx-look"

cmake -S . -B build-calibration \
  -DSPEKTRAFILM_PRO_BUILD_MODE=CALIBRATION \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-calibration --target spektrafilm
sudo cmake --install build-calibration
```

---

## 6. Impactos esperados

### DaVinci Resolve

O plugin deve aparecer em:

```text
OpenFX > MC Plugins > LookFilmLab v0.0.1
```

### Compatibilidade com projetos antigos

Ao trocar o identifier de:

```text
org.spektrafilm
```

para:

```text
com.mcplugins.lookfilmlab
```

o host passa a enxergar como outro plugin.

Impacto:

- projetos antigos usando `org.spektrafilm` não serão automaticamente o mesmo
  efeito;
- isso é aceitável se não houver necessidade de abrir projetos antigos;
- durante testes, pode aparecer o plugin antigo e o novo juntos se os dois
  bundles estiverem instalados.

---

## 7. Bloqueantes

Não há bloqueante técnico identificado.

Cuidados antes da implementação:

1. confirmar que o target CMake deve continuar `spektrafilm`;
2. confirmar que a versão pública inicial será `0.0.1` no arquivo `VERSION`;
3. confirmar que a remoção de ícones significa:
   - remover do descriptor;
   - não copiar para o bundle;
   - manter ou deletar fisicamente `Resources/icons/*` em patch separado;
4. limpar o bundle antigo instalado antes de testar, para evitar duplicidade no
   DaVinci Resolve.

---

## 8. Ordem de implementação sugerida

1. Criar `spektrafilm-ofx-look/VERSION` com `0.0.1`.
2. Ler `VERSION` no CMake.
3. Criar `MCLOOKFILMLAB_DISPLAY_NAME`.
4. Alterar o target Pro para:
   - artifact: `MCLookFilmLab`;
   - label: `LookFilmLab v${MCLOOKFILMLAB_VERSION} - spektrafilm`;
   - identifier: `com.mcplugins.lookfilmlab`.
5. Alterar grouping OFX para `MC Plugins`.
6. Definir label, short label e long label com o mesmo nome público.
7. Remover registro de ícones do descriptor.
8. Remover cópia/dependência de ícones no CMake.
9. Reconfigurar `build-release` e `build-calibration`.
10. Compilar ambos os modos.
11. Remover bundles antigos instalados.
12. Instalar e validar no DaVinci Resolve.
