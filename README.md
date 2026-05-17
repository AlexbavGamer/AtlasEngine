# AtlasEngine

**Um motor de jogo / renderizador leve, moderno e em desenvolvimento.**

![License](https://img.shields.io/github/license/AlexbavGamer/AtlasEngine)
![Language](https://img.shields.io/github/languages/top/AlexbavGamer/AtlasEngine)
![Stars](https://img.shields.io/github/stars/AlexbavGamer/AtlasEngine?style=social)

---

## Sobre o Projeto

**AtlasEngine** é um motor de jogo / engine de renderização em desenvolvimento focado em **performance**, **simplicidade** e **modernidade**.

O objetivo é criar uma base leve e flexível para protótipos, jogos indie e experimentações técnicas, utilizando tecnologias atuais.

### Principais Características

- **Linguagem principal**: C++20 / C++23
- **API de Gráficos**: Vulkan (foco principal)
- **Cross-platform**: Windows, Linux (macOS planejado)
- **Entity Component System** (ECS)
- **Renderização baseada em Física (PBR)**
- **Asset Pipeline** moderno
- **Editor** (em planejamento)

---

## Status Atual

Em fase inicial de desenvolvimento.

---

## Como Compilar

### Pré-requisitos

- CMake 3.22+
- Vulkan SDK
- Compilador com suporte a C++20+

### Compilação

```bash
git clone https://github.com/AlexbavGamer/AtlasEngine.git
cd AtlasEngine
git submodule update --init --recursive

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

---

## Contribuindo

Pull requests são bem-vindos!

---

**Feito com ❤️ por [Alexsandre](https://github.com/AlexbavGamer)**