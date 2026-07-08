# LookFilmLab — Manual rápido de build e implantação

Este guia resume os comandos usados para alternar entre os builds
`Calibration` e `Production` do LookFilmLab.

Projeto:

```bash
cd "/Volumes/DataMEDIA/DEV/_OFX/MC OFX/spektrafilm-ofx-look"
```

Nome público no host:

```text
MC Plugins > LookFilmLab v0.1.1
```

Bundle gerado:

```text
MCLookFilmLab.ofx.bundle
```

Master ativo de calibração:

```text
~/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json
```

Esse é o arquivo principal do fluxo:

- o `Calibration` salva e recarrega esse arquivo;
- o `Production` usa uma cópia embutida desse arquivo no bundle;
- os arquivos com timestamp ficam como histórico/backup.
- uma cópia versionada pode ser mantida em
  `calibration/active_production_calibration.lookfilmlab.json` para builds de
  release reproduzíveis.

---

## 1. Build Calibration

Use este build para calibrar e salvar o master ativo.

```bash
cd "/Volumes/DataMEDIA/DEV/_OFX/MC OFX/spektrafilm-ofx-look"

cmake -S . -B build-calibration \
  -DSPEKTRAFILM_PRO_BUILD_MODE=CALIBRATION \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-calibration --target spektrafilm
```

Instalar no sistema:

```bash
sudo cmake --install build-calibration
```

O install do projeto está limitado ao target Pro/LookFilmLab. Ele instala:

```text
/Library/OFX/Plugins/MCLookFilmLab.ofx.bundle
```

No DaVinci Resolve, o build `Calibration` deve mostrar a UI técnica completa e
o grupo `Calibration`, incluindo:

- `Save Global Calibration`;
- `Save Negative Calibration`;
- `Save Print Calibration`;
- `Save Negative/Print Calibration`;
- `Export Production Calibration`;
- `Load Active Calibration`;
- `Active Master`.

Ao salvar/exportar, o plugin atualiza:

```text
~/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json
```

---

## 2. Build Production usando o master ativo

Use este build para gerar o bundle final reduzido, com a calibração embutida.

```bash
cd "/Volumes/DataMEDIA/DEV/_OFX/MC OFX/spektrafilm-ofx-look"

cmake -S . -B build-release \
  -DSPEKTRAFILM_PRO_BUILD_MODE=PRODUCTION \
  -DSPEKTRAFILM_PRO_CALIBRATION_FILE="$HOME/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-release --target spektrafilm
```

Instalar no sistema:

```bash
sudo cmake --install build-release
```

O install do projeto está limitado ao target Pro/LookFilmLab. Ele instala:

```text
/Library/OFX/Plugins/MCLookFilmLab.ofx.bundle
```

No DaVinci Resolve, o build `Production` deve mostrar somente:

- `Technical Controls`;
- `Camera Settings`;
- `Film Stocks`;
- `Laboratory`.

E somente os controles públicos aprovados.

---

## 3. Build Production sem calibração embutida

Use apenas para teste de baseline/defaults.

```bash
cd "/Volumes/DataMEDIA/DEV/_OFX/MC OFX/spektrafilm-ofx-look"

cmake -S . -B build-release \
  -DSPEKTRAFILM_PRO_BUILD_MODE=PRODUCTION \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-release --target spektrafilm
sudo cmake --install build-release
```

---

## 4. Trocar rapidamente entre Calibration e Production

Feche o DaVinci Resolve antes de trocar o bundle.

Instalar Calibration:

```bash
cd "/Volumes/DataMEDIA/DEV/_OFX/MC OFX/spektrafilm-ofx-look"
sudo cmake --install build-calibration
```

Instalar Production:

```bash
cd "/Volumes/DataMEDIA/DEV/_OFX/MC OFX/spektrafilm-ofx-look"
sudo cmake --install build-release
```

Se o host insistir em manter cache antigo, remova o bundle instalado e reinstale:

```bash
sudo rm -rf "/Library/OFX/Plugins/spektrafilm.ofx.bundle"
sudo rm -rf "/Library/OFX/Plugins/spektrafilm_flow.ofx.bundle"
sudo rm -rf "/Library/OFX/Plugins/spektrafilm_dev.ofx.bundle"
sudo rm -rf "/Library/OFX/Plugins/MCLookFilmLab-spektrafilm.ofx.bundle"
sudo rm -rf "/Library/OFX/Plugins/MCLookFilmLab.ofx.bundle"
sudo cmake --install build-release
```

Ou, para voltar ao Calibration:

```bash
sudo rm -rf "/Library/OFX/Plugins/spektrafilm.ofx.bundle"
sudo rm -rf "/Library/OFX/Plugins/spektrafilm_flow.ofx.bundle"
sudo rm -rf "/Library/OFX/Plugins/spektrafilm_dev.ofx.bundle"
sudo rm -rf "/Library/OFX/Plugins/MCLookFilmLab-spektrafilm.ofx.bundle"
sudo rm -rf "/Library/OFX/Plugins/MCLookFilmLab.ofx.bundle"
sudo cmake --install build-calibration
```

---

## 5. Fluxo recomendado de calibração

1. Instalar `Calibration`.
2. Abrir no DaVinci Resolve.
3. Ajustar os controles técnicos.
4. Usar `Export Production Calibration`.
5. Conferir se o master ativo foi atualizado:

```bash
ls -lh "$HOME/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json"
```

6. Fechar o DaVinci Resolve.
7. Gerar `Production` usando o master ativo:

```bash
cmake -S . -B build-release \
  -DSPEKTRAFILM_PRO_BUILD_MODE=PRODUCTION \
  -DSPEKTRAFILM_PRO_CALIBRATION_FILE="$HOME/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-release --target spektrafilm
sudo cmake --install build-release
```

8. Abrir o DaVinci Resolve e validar a UI reduzida + look calibrado.
9. Para continuar calibrando depois, reinstalar `Calibration`.
10. O `Calibration` deve recarregar automaticamente o master ativo.

---

## 6. Comandos rápidos de validação

Ver se o master ativo existe:

```bash
ls -lh "$HOME/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json"
```

Ver o início do JSON do master:

```bash
sed -n '1,40p' "$HOME/Documents/MCLookFilmLab/calibration/active_production_calibration.lookfilmlab.json"
```

Ver se o bundle Production recebeu a calibração embutida:

```bash
ls -lh "build-release/MCLookFilmLab.ofx.bundle/Contents/Resources/production_calibration.lookfilmlab.json"
```

Ver o início do JSON embutido no bundle:

```bash
sed -n '1,40p' "build-release/MCLookFilmLab.ofx.bundle/Contents/Resources/production_calibration.lookfilmlab.json"
```

---

## 7. Limpeza de builds locais

Normalmente não precisa limpar. Mas, se quiser reconfigurar do zero:

```bash
rm -rf build-release build-calibration
```

Depois rode novamente os comandos de configuração/build.

---

## 8. Observações importantes

- Sempre feche o DaVinci Resolve antes de instalar outro bundle.
- O `Production` não altera o master ativo.
- O master ativo é a fonte oficial de trabalho.
- O bundle `Production` é uma cópia congelada do master no momento do build.
- Os arquivos `.lookcalibration.json` com timestamp servem como histórico/backup.
- Para distribuição final, use sempre o build `Production` com
  `SPEKTRAFILM_PRO_CALIBRATION_FILE` apontando para o master ativo.
