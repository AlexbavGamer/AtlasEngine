# Atlas UI Library — Design & Implementation Plan

> **Status:** Rascunho de planejamento — implementação começa amanhã.
> **Branch:** `feat/custom-ui`
> **Objetivo:** Biblioteca de UI customizada **separada** da engine, com componentes que derivam de `UIWidget`, no estilo *retained-mode*.

---

## 1. Decisões-chave (resumo)

| Decisão | Escolha | Por quê |
| --------- | --------- | --------- |
| **Localização** | `libs/atlas_ui/` (fora de `src/`) | Biblioteca independente, reutilizável, não mistura com a engine |
| **Modelo de widgets** | **Retained-mode** (`UIWidget` base, instâncias persistentes) | Cada componente é uma classe derivada; estados (hover, press, value) vivem no objeto — mais previsível que immediate-mode para componentes complexos |
| **Render** | Vulkan pipeline próprio (premultiplied-alpha, batching por textura+scissor) | Overlay 2D, eficiente, sem dependência de ImGui |
| **Fonte** | Atlas de glifos via `stb_truetype`, mipmaps, texel branco reservado para quads sólidos | Texto nítido + quads sólidos sem bleed |
| **Build** | Projeto `premake5` **estático** (`atlas_ui_lib`), padrão de `imgui_lib` | Mesma infraestrutura de build do projeto, linkado ao editor e ao game |
| **Coordenação de inputs** | Classe `UIInput` (mouse/teclado/scroll/texto) alimentada pelo host (GLFW) | UI desacoplada do windowing |

---

## 2. Estrutura de diretórios

```
libs/atlas_ui/
├── CMakeLists.txt                     # (opcional) se precisar de build CMake
├── include/atlas_ui/
│   ├── ui_context.h                   # motor principal (retained-mode)
│   ├── ui_widget.h                    # base UIWidget
│   ├── ui_types.h                     # UIVertex, UIStyle, UIInput, UIFontGlyph
│   ├── ui_draw_list.h                 # batching de primitivas
│   ├── ui_font.h                      # atlas de fonte (stb_truetype)
│   └── ui_events.h                    # eventos de input/action (callbacks opcionais)
├── src/
│   ├── ui_context.cpp
│   ├── widgets/
│   │   ├── ui_button.cpp/.h
│   │   ├── ui_checkbox.cpp/.h
│   │   ├── ui_slider.cpp/.h
│   │   ├── ui_text_input.cpp/.h
│   │   ├── ui_label.cpp/.h
│   │   ├── ui_combo.cpp/.h
│   │   ├── ui_collapsing_header.cpp/.h
│   │   └── ui_window.cpp/.h
│   ├── panels/
│   │   ├── ui_console_panel.cpp/.h
│   │   ├── ui_hierarchy_panel.cpp/.h
│   │   └── ui_properties_panel.cpp/.h
│   └── render/
│       ├── ui_renderer.cpp/.h        # pipeline Vulkan
│       └── ui_shaders.glsl           # vert/frag (compilados p/ SPIR-V)
└── shaders/
    ├── ui_vert.glsl
    └── ui_frag.glsl
```

> **Convenção:** headers públicos em `include/atlas_ui/`, implementações em `src/`, widgets em `src/widgets/`, painéis compostos em `src/panels/`, render em `src/render/`.

**Razão de cada classe ser derivada de `UIWidget`:** permite composição (um `UIWindow` contém vários `UIWidget`), layout uniforme (`measure`/`setBounds`), e tratamento de input centralizado via vtable. Não usaremos immediate-mode para os componentes — cada widget é um objeto que o host cria/armazena e re-desenha a cada frame.

---

## 3. Arquitetura de classes

### 3.1 `UIWidget` (base — tudo deriva daqui)

```cpp
class UIWidget {
public:
    virtual ~UIWidget() = default;
    UIWidget(const UIWidget&) = delete;
    UIWidget& operator=(const UIWidget&) = delete;

    // Layout: medir tamanho intrínseco, depois posicionar.
    virtual glm::vec2 measure(const UIFont& font, const UIStyle& style) const = 0;
    virtual void   setBounds(const glm::vec2& min, const glm::vec2& max) { m_Min = min; m_Max = max; }

    // Desenho + input (chamados toda frame pelo container/contexto).
    virtual void draw(UIDrawList& dl, const UIFont& font, const UIStyle& style) = 0;
    virtual bool handle(const UIInput& input) { return false; } // true = consumiu

    // Utilidades.
    glm::vec2 min() const { return m_Min; }
    glm::vec2 max() const { return m_Max; }
    glm::vec2 size() const { return m_Max - m_Min; }
    bool contains(const glm::vec2& p) const;
    bool visible() const { return m_Visible; }
    void setVisible(bool v) { m_Visible = v; }

protected:
    glm::vec2 m_Min{0.0f};
    glm::vec2 m_Max{0.0f};
    bool m_Visible = true;
};
```

### 3.2 Catálogo de widgets (todos derivam de `UIWidget`)

| Widget | Estado interno | Responsável por |
| -------- | ---------------- | ----------------- |
| `UIButton` | `pressed`, `hovered`, callback | clique acionado no release sobre o botão |
| `UICheckbox` | `checked` | toggle binário |
| `UISlider` | `value`, `min`, `max`, `dragging` | valor contínuo horizontal |
| `UIDragFloat` | `value`, `dragOrigin` | edição numérica por arrasto |
| `UITextInput` | `text`, `cursorPos`, `focused` | edição de texto UTF-8, cursor, setas |
| `UICombo` | `selectedIndex`, `open` | dropdown |
| `UICollapsingHeader` | `collapsed` | seção expansível |
| `UILabel` | `text`, `color` | texto estático |
| `UISeparator` | — | divisor horizontal |
| `UIProgressBar` | `fraction` | barra de progresso |
| `UITabBar` | `activeIndex` | abas clicáveis |
| `UIMenuBar` | `openMenuIndex` | menu topo |
| `UIWindow` | `title`, `dragging`, `resizing`, rect | janela flutuante com title bar, resize, close |

### 3.3 Composição

- **`UIWindow`** é um container que possui um `std::vector<std::unique_ptr<UIWidget>>` de filhos (content).
- **`UIHierarchyPanel`/`UIPropertiesPanel`/`UIConsolePanel`** são widgets compostos que **instanciam** `UIWidget`s internamente (não herdam de painel separado) OU expõem API de desenho. Decidir: painéis como **widgets compostos** derivando de `UIWidget` para uniformidade.

---

## 4. Motor / Context

`UIContext` é o "frame driver" — coordena widgets, layout, e alimenta o renderer.

```cpp
class UIContext {
public:
    // 1. Configuração por frame
    void beginFrame(const UIInput& input, const glm::vec2& screenSize);
    void endFrame(UIDrawList& outDrawList);

    // 2. Layout: containers, scroll, janelas
    void pushClip(const glm::vec2& min, const glm::vec2& max);
    void popClip();
    void beginScrollArea(const std::string& id, const glm::vec2& min, const glm::vec2& max, bool followTail);
    void endScrollArea(float contentHeight);
    bool beginWindow(const std::string& title, glm::vec2& rect, float titleBarH);
    void endWindow();

    // 3. Registro/gestão de widgets (retained)
    template <typename T, typename... Args>
    T* addWidget(Args&&... args);
    void clearWidgets();
    void processInput();   // distribui handle() aos widgets em ordem de hit-test
    void drawAll(UIDrawList& dl);

    // 4. Helpers de fonte/estilo
    float textWidth(const std::string&) const;
    void  drawText(UIDrawList& dl, const std::string&, const glm::vec2& pos, ...);

private:
    std::vector<std::unique_ptr<UIWidget>> m_Widgets;
    UIDrawList m_DrawList;
    UIStyle m_Style;
    const UIFont* m_Font = nullptr;
};
```

**Diferença vs. versão anterior:** o `UIContext` aqui **possui os widgets** (retained) e os desenha/processa; não é o motor immediate-mode que lê `if (ctx.button(...))`. Isso resolve o bug de "IDs por tipo colidem" porque cada widget **é um objeto com identidade própria** (sem hash de label por chamada).

---

## 5. Render (Vulkan)

**`UIRenderer`** — pipeline dedicado:

- Batching por `(textureIndex, scissor)` → um `vkCmdDrawIndexed` por batch.
- **Premultiplied-alpha** blending (`ONE / ONE_MINUS_SRC_ALPHA`).
- Dynamic state: viewport + scissor.
- **Texture array** de 8 slots:
  - `0` = atlas de fonte (RGBA, alpha mask, mipmapped)
  - `1..7` = texturas externas (viewport, thumbnails)
- Push constants: `{ vec2 viewportSize }` (40 bytes — dentro do limite de 256).

**`ui_vert.glsl`**: screen-space (top-left) → NDC (Y-down).
**`ui_frag.glsl`**: `uTextures[8]` bindless; slot 0 → `inColor * tex.a`; slot N → `inColor * tex`.

> **Nota de build:** shaders compilados por `scripts/build_shaders.bat` (glslc) e embutidos via `embed_shaders.ps1` — adicionar `ui_vert.glsl`/`ui_frag.glsl` a esse script.

---

## 6. Fonte (`UIFont`)

- Atlas `1024×1024` RGBA, 224 caracteres (32–255), bake a 2x.
- UVs com inset de meio texel (evita bleed).
- Bloco branco opaco reservado (canto inf. dir.) para quads sólidos.
- Mipmaps via blit.
- UTF-8 decoder próprio.

---

## 7. Integração no build (premake5)

Adicionar um novo projeto estático, no padrão de `imgui_lib`:

```lua
project "atlas_ui_lib"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"

    files {
        "libs/atlas_ui/**.cpp",
        "libs/atlas_ui/**.h",
    }

    includedirs {
        "libs/atlas_ui/include",
        path.join(deps_src, "glm"),
        path.join(deps_src, "stb"),
        path.join(deps_src, "vma/include"),
        vulkan_inc,
    }

    links { "vulkan-1" }
```

E no `project "AtlasEngine"` (e `AtlasGame`) adicionar `"atlas_ui_lib"` a `links`.

---

## 8. Integração no host (editor/game)

- Host cria um `UIContext` + `UIRenderer` + `UIFont`.
- A cada frame: monta `UIInput` (mouse, teclado, scroll, texto) → `ctx.beginFrame` → `ctx.processInput` → `ctx.drawAll` → `uiRenderer.recordDraw(cmd, drawList)`.
- **Exemplo mínimo de uso (retained):**

  ```cpp
  auto* btn = ctx.addWidget<UIButton>("Play", []{ startGame(); });
  auto* sld = ctx.addWidget<UISlider>("Volume", &volume, 0.f, 1.f);
  ```

---

## 9. Roadmap (fases — amanhã em diante)

### Fase 0 — Esqueleto da lib (½ dia)

- [ ] Criar `libs/atlas_ui/` com `ui_types.h`, `ui_widget.h`, `ui_draw_list.h`, `ui_font.h`.
- [ ] Adicionar projeto `atlas_ui_lib` no `premake5.lua`; regenerar build.
- [ ] Adicionar `ui_vert.glsl`/`ui_frag.glsl` ao `build_shaders.bat`.
- [ ] `UIRenderer` (pipeline Vulkan + batching) compilando e desenhando um quad de teste.

### Fase 1 — Widgets base (1 dia)

- [ ] `UIButton`, `UILabel`, `UISeparator`.
- [ ] `UIWidget::handle()` com hit-test + captura de mouse (press→release, cancel se press fora).
- [ ] Hot/active tracking (hover, pressed).
- [ ] Teste: renderizar botão clicável via context.

### Fase 2 — Controles de dados (1–2 dias)

- [ ] `UICheckbox`, `UISlider`, `UIDragFloat`, `UIProgressBar`.
- [ ] `UITextInput` (cursor, UTF-8, setas, backspace/delete).
- [ ] `UICombo`, `UICollapsingHeader`, `UITabBar`, `UIMenuBar`.

### Fase 3 — Containers / layout (1–2 dias)

- [ ] `beginScrollArea` (wheel + scrollbar + followTail).
- [ ] `UIWindow` (title bar drag, resize, close, rect persistente).
- [ ] Layout vertical automático (medir → empilhar) nos containers.

### Fase 4 — Painéis compostos (2 dias)

- [ ] `UIConsolePanel`, `UIHierarchyPanel`, `UIPropertiesPanel`.
- [ ] Bind ao `RuntimeConsole` singleton e ao `Scene`.
- [ ] Substituir o editor ImGui (progressivamente) pelo custom UI.

### Fase 5 — Polimento (contínuo)

- [ ] Tema `UIStyle` (cores, rounding, padding) ajustável.
- [ ] HiDPI / scale factor.
- [ ] Performance: culling por clip, layout cache.
- [ ] Docs de API + exemplos.

---

## 10. Riscos & decisões em aberto

| Risco | Decisão / mitigação |
| ------- | --------------------- |
| **ID / estado entre frames** | Resolvido por design: widgets são objetos (retained), sem hash de label. |
| **Ordem de hit-test** | Widgets processados em ordem de criação; containers testam filhos antes de si. |
| **Fonte/atlas** | Usar `stb_truetype` (já em `deps/src/stb`). |
| **Premultiplied alpha** | Manter consistência entre `addQuad` e fragment shader (como versão anterior). |
| **Limite de push constants** | Manter ≤256 bytes (usar 40). |
| **Rede de dependências** | Lib **não** deve depender de `src/` da engine; painéis recebem dados via interface (não incluem `scene.h`). Decidir se `UIHierarchyPanel` recebe um `Scene*` ou um callback de enumeração de entidades. **Em aberto.** |

---

## 11. Referências úteis

- `deps/src/stb/stb_truetype.h` — rasterização de fonte.
- `src/renderer/embedded_shaders.h` — como shaders são embutidos.
- `scripts/build_shaders.bat` — pipeline de compilação de shaders.
- `premake5.lua:89-120` — modelo de `StaticLib` (`imgui_lib`).

---

*Este plano é um ponto de partida. Vamos começar amanhã pela Fase 0.*
