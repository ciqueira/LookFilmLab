# Checklist de distribuição pública — LookFilmLab fork

Este checklist organiza o que precisa ser feito antes de publicar binários
públicos do build modificado `LookFilmLab`.

Observação: isto é uma checklist técnica/prática baseada nos arquivos de
licença presentes no projeto. Não substitui revisão jurídica profissional.

---

## 1. Base legal do projeto

O source do projeto está sob GPL-3.0:

```text
LICENSE.txt
```

Isso permite:

- modificar o código;
- compilar binários;
- distribuir binários publicamente;
- distribuir gratuitamente ou comercialmente.

Mas exige:

- disponibilizar o código-fonte correspondente;
- manter a licença GPL-3.0;
- manter avisos de copyright/licença;
- indicar que a versão foi modificada;
- não transformar o código em produto fechado/proprietário.

---

## 2. Itens obrigatórios antes de publicar binário

## 2.1 Publicar o source correspondente

Para cada binário público, publicar o source exato usado para gerar aquele
binário.

Opções:

- repositório público;
- release/source archive `.zip`;
- link permanente para tag/commit.

Recomendado:

```text
git tag lookfilmlab-v0.1.1
```

E publicar o source dessa tag.

Checklist:

- [ ] Criar tag da versão pública.
- [ ] Garantir que o source da tag compila.
- [ ] Publicar link do source junto do binário.
- [ ] Documentar comandos de build.

---

## 2.2 Incluir GPL-3.0 na distribuição

O pacote/binário deve incluir a licença GPL-3.0.

Arquivo atual:

```text
LICENSE.txt
```

Checklist:

- [ ] Incluir `LICENSE.txt` no pacote distribuído.
- [ ] Incluir referência à GPL-3.0 na página de download/release.

---

## 2.3 Incluir notices de terceiros

O projeto já tem:

```text
Legal/THIRD_PARTY_NOTICES.txt
```

Checklist:

- [ ] Incluir `Legal/THIRD_PARTY_NOTICES.txt` no bundle ou no pacote de release.
- [ ] Conferir se os notices ainda estão corretos após o rebrand.
- [ ] Atualizar nomes antigos `spektrafilm OFX` quando fizer sentido.
- [ ] Creditar `spektrafilm-ofx` como upstream imediato.
- [ ] Creditar o Python `spektrafilm` original como base/research foundation.
- [ ] Não remover créditos a Andrea Volpato, Hanatos/vkdt, OpenFX etc.

---

## 2.4 Marcar como versão modificada/fork

A GPL exige que versões modificadas carreguem avisos apropriados de modificação.

Adicionar um arquivo, por exemplo:

```text
MODIFICATIONS.md
```

Conteúdo mínimo:

- projeto baseado em `spektrafilm-ofx`;
- explicar que `spektrafilm-ofx` é um port/expansão do Python `spektrafilm`;
- alterações principais feitas;
- data;
- responsável/organização;
- link para upstream original;
- aviso de que não é release oficial do upstream.

Checklist:

- [ ] Criar `MODIFICATIONS.md`.
- [ ] Incluir no pacote de distribuição.
- [ ] Referenciar no README/release notes.

---

## 2.5 Publicar instruções de build

Para cumprir bem a GPL, o source deve ser realmente construível.

Já existe:

```text
documentation/build_quick_manual.md
```

Checklist:

- [ ] Atualizar comandos para o nome `MCLookFilmLab.ofx.bundle`.
- [ ] Incluir build Production.
- [ ] Incluir build Calibration, se for público.
- [ ] Incluir dependências.
- [ ] Incluir como usar o master de calibração.
- [ ] Garantir que instruções funcionam em uma máquina limpa.

---

## 2.6 Não redistribuir binários oficiais como base

O arquivo:

```text
Legal/SPEKTRAFILM_OFX_LICENSE.txt
```

limita redistribuição/reempacotamento de binários oficiais.

Estado do fork LookFilmLab:

- esse arquivo legado não é mais copiado para o bundle LookFilmLab;
- o bundle passa a incluir `GPL-3.0.txt`, `MODIFICATIONS.md` e
  `DISTRIBUTION.md`.

Para evitar problema:

- usar somente binários compilados por você a partir do source GPL;
- não reempacotar binários oficiais;
- não incluir recursos não-GPL vindos de builds oficiais.

Checklist:

- [ ] Confirmar que o binário público foi compilado localmente/source próprio.
- [ ] Confirmar que não há recursos extraídos de pacote oficial não-GPL.
- [ ] Remover ou substituir qualquer recurso não redistribuível.

---

## 3. Marca, nome e identidade pública

## 3.1 Evitar parecer release oficial

Mesmo com GPL, o uso de nome/marca pode ter restrições.

Estado atual desejado:

```text
MC Plugins > LookFilmLab v0.1.1
```

Risco:

- `spektrafilm` pode ser interpretado como marca/projeto original;
- usuários podem achar que é release oficial.

Decisão aplicada:

- deixar o produto principal como `LookFilmLab`;
- usar `spektrafilm` apenas como referência técnica/fork/upstream, não como
  marca principal;
- adicionar aviso:

```text
LookFilmLab is an independent modified build based on GPL-3.0 spektrafilm-ofx source.
spektrafilm-ofx ports and expands the original Python spektrafilm project.
LookFilmLab is not an official release of either upstream and is not endorsed by their authors.
```

Checklist:

- [ ] Adicionar aviso de fork independente no README.
- [ ] Adicionar aviso no pacote/release notes.
- [x] Nome público definido como `LookFilmLab v0.1.1`.
- [ ] Evitar logos, ícones ou identidade visual do upstream.

---

## 3.2 Identifier novo

Já definido:

```text
com.mcplugins.lookfilmlab
```

Impacto:

- DaVinci Resolve trata como plugin novo;
- projetos antigos com `org.spektrafilm` não migram automaticamente.

Checklist:

- [ ] Documentar quebra de compatibilidade com identifier antigo.
- [ ] Remover bundles antigos no teste para evitar duplicidade.

---

## 4. Recursos e dados empacotados

## 4.1 Conferir recursos redistribuíveis

Verificar tudo que entra no bundle:

```text
Contents/Resources/
```

Atualmente o bundle pode incluir:

- `SpektraFilm.metallib`;
- `SpektraHanatos2025Spectra.f32`;
- `SpektraOutputGamutCompression.f32`;
- `plugin_manifest.json`;
- `production_calibration.lookfilmlab.json`;
- arquivos em `Legal/`.

Checklist:

- [ ] Confirmar licença/origem de `SpektraHanatos2025Spectra.f32`.
- [ ] Confirmar licença/origem de `SpektraOutputGamutCompression.f32`.
- [x] Remover `manual.pdf` upstream do bundle LookFilmLab.
- [ ] Confirmar que `production_calibration.lookfilmlab.json` não contém
      dados de terceiros não redistribuíveis.
- [ ] Garantir que dados SMPTE/APD não estão embutidos sem licença.

---

## 4.2 SMPTE/APD/standards

O README menciona que dados SMPTE ST 2065-2 não são redistribuídos no repo
público.

Checklist:

- [ ] Verificar se arquivos SMPTE/APD não estão no bundle.
- [ ] Se estiverem, remover antes de publicar, salvo se houver licença para
      redistribuição.
- [ ] Manter APD fora do plano atual, como decidido.

---

## 5. Licenças específicas de LUT

O arquivo upstream:

```text
Legal/SPEKTRAFILM_OFX_LUT_LICENSE.txt
```

não é mais copiado para o bundle LookFilmLab, pois fala de LUTs exportadas pelo
spektrafilm OFX. Se LUT export for exposto em uma release pública do
LookFilmLab, criar termos próprios revisados para o produto.

Checklist:

- [x] Remover `SPEKTRAFILM_OFX_LUT_LICENSE.txt` do bundle LookFilmLab.
- [ ] Decidir se LUT export continua público.
- [ ] Criar termos próprios de LUT para LookFilmLab, se necessário.

---

## 6. Documentação pública

Arquivos recomendados para preparar:

```text
README.md
MODIFICATIONS.md
DISTRIBUTION.md
LICENSE.txt
Legal/THIRD_PARTY_NOTICES.txt
documentation/build_quick_manual.md
```

Checklist:

- [ ] Atualizar README para LookFilmLab.
- [ ] Manter crédito ao projeto original.
- [ ] Explicar que é fork/modificação.
- [ ] Explicar licença GPL-3.0.
- [ ] Explicar onde baixar source.
- [ ] Explicar onde baixar binário.
- [ ] Explicar compatibilidade de hosts.
- [ ] Explicar que não é release oficial do upstream.

---

## 7. Pacote de distribuição recomendado

Estrutura sugerida:

```text
LookFilmLab-v0.1.1-macOS/
  MCLookFilmLab.ofx.bundle/
  LICENSE.txt
  MODIFICATIONS.md
  THIRD_PARTY_NOTICES.txt
  BUILD_SOURCE.txt
  README.txt
```

`BUILD_SOURCE.txt` deve conter:

- tag/commit do source;
- URL do repositório/source archive;
- comandos principais de build;
- versão pública;
- versão Core.

Checklist:

- [ ] Criar pasta stage de release.
- [ ] Copiar bundle.
- [ ] Copiar licenças/notices.
- [ ] Copiar `MODIFICATIONS.md`.
- [ ] Criar `BUILD_SOURCE.txt`.
- [ ] Compactar `.zip`.
- [ ] Testar instalação a partir do zip.

---

## 8. Checklist técnico antes de release

Production:

- [ ] Build Production compila.
- [x] Bundle esperado: `MCLookFilmLab.ofx.bundle`.
- [x] Identifier esperado: `com.mcplugins.lookfilmlab`.
- [x] Nome host esperado: `LookFilmLab v0.1.1`.
- [x] Grupo host esperado: `MC Plugins`.
- [ ] UI Production reduzida correta.
- [ ] Calibração embutida carregando.
- [ ] Bundle não contém `Resources/icons`.
- [ ] Bundle não contém arquivos SMPTE/APD restritos.

Calibration:

- [ ] Build Calibration compila.
- [ ] Master ativo salva.
- [ ] Master ativo recarrega.
- [ ] Export Production Calibration funciona.

Instalação:

- [ ] Remover bundles antigos antes do teste.
- [ ] Instalar novo bundle.
- [ ] Abrir DaVinci Resolve.
- [ ] Confirmar apenas um plugin esperado na lista.

---

## 9. Checklist jurídico/prático final

Antes de publicar:

- [ ] Source correspondente publicado.
- [ ] Licença GPL-3.0 incluída.
- [ ] Third-party notices incluídos.
- [ ] Modificações documentadas.
- [ ] Aviso de fork independente incluído.
- [ ] Nenhum binário oficial reempacotado.
- [ ] Nenhum recurso não redistribuível incluído.
- [ ] Página/release informa onde obter o source.
- [ ] Página/release informa que é GPL-3.0.
- [ ] Página/release não sugere endosso oficial do upstream.

---

## 10. Próximas mudanças recomendadas no repositório

1. Criar script de package próprio para `LookFilmLab`.
2. Revisar `Legal/SPEKTRAFILM_OFX_LICENSE.txt` para decidir se será mantido
   apenas como referência histórica ou removido do fork.
3. Criar termos próprios para LUT export se esse recurso continuar disponível
   publicamente.
6. Criar script de package próprio para `LookFilmLab`.
7. Criar tag pública `lookfilmlab-v0.1.1`.
