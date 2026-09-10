# Technical Design Document  
**Sistema de Renderização de Grandes Cidades**  

**Versão:** 1.0  
**Data:** 04/09/2026  
**Status:** Draft  

---

## 1. Objetivo

Projetar e implementar um sistema escalável capaz de renderizar cidades densas de vários quilômetros quadrados com dezenas de milhares de prédios e milhões de props, mantendo:

- Alto nível de detalhe próximo à câmera
- Representações baratas para distâncias longas
- Streaming eficiente de dados
- Baixo número de draw calls
- Suporte robusto a Frustum Culling e Occlusion Culling

---

## 2. Arquitetura Geral

O sistema é composto pelos seguintes módulos principais, que trabalham em conjunto:

```
┌─────────────────────────────────────────────────────────────┐
│                    World Partition System                   │
│  (Spatial Grid + Streaming de Células + Prioridades)        │
└──────────────────────────┬──────────────────────────────────┘
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
┌─────────────────┐ ┌──────────────┐ ┌─────────────────┐
│  Full Detail    │ │   HLOD 0     │ │    HLOD 1       │
│  (Células       │ │  (Merged     │ │  (Simplified    │
│   próximas)     │ │   Instanced) │ │   / Impostors)  │
└────────┬────────┘ └──────┬───────┘ └────────┬────────┘
         │                 │                  │
         └─────────────────┼──────────────────┘
                           ▼
                ┌──────────────────────┐
                │  Culling Pipeline    │
                │  (Frustum + Occlusion)│
                └──────────┬───────────┘
                           ▼
                ┌──────────────────────┐
                │  Rendering Pipeline  │
                │  (Instancing + LODs) │
                └──────────────────────┘
```

---

## 3. World Partition / Spatial Streaming System

### 3.1. Conceito

O mundo é dividido em uma grade espacial de células. Apenas as células próximas ao jogador (e a outras streaming sources) permanecem carregadas em memória.

### 3.2. Parâmetros Principais

| Parâmetro              | Valor Recomendado     | Descrição                                      |
|------------------------|-----------------------|------------------------------------------------|
| Cell Size              | 128m ~ 256m           | Tamanho de cada célula                         |
| Loading Range          | 300m ~ 600m           | Distância para carregar Full Detail            |
| HLOD0 Range            | 600m ~ 1500m          | Distância para HLOD0                           |
| HLOD1 Range            | > 1500m               | Distância para HLOD1 (sempre carregado ou streaming lento) |
| Preload Distance       | +1 célula             | Células vizinhas carregadas com prioridade baixa |

### 3.3. Estruturas de Dados Principais

```cpp
struct WorldCell
{
    Vector2Int           GridCoord;
    BoundingBox          Bounds;
    CellState            State;          // Unloaded, Loading, Loaded, Unloading
    float                Priority;
    
    // Conteúdo
    std::vector<Actor*>  FullDetailActors;
    HLODActor*           HLOD0;
    HLODActor*           HLOD1;
    
    // Metadados
    uint64_t             LastAccessFrame;
    bool                 bIsDirty;
};

enum class CellState : uint8_t
{
    Unloaded,
    Loading,
    Loaded,
    Unloading
};
```

### 3.4. Fluxo por Frame

1. Atualizar posição das Streaming Sources (jogador + câmeras importantes).
2. Calcular células que devem estar no estado `Loaded` com base nas ranges.
3. Ordenar células por prioridade (distância + importância).
4. Iniciar/continuar carregamento das células de maior prioridade.
5. Descarregar células que saíram das ranges (com hysteresis para evitar thrashing).

### 3.5. Requisitos de Implementação

- Carregamento assíncrono (não bloquear a thread principal).
- Sistema de orçamento de tempo por frame para loading (ex: 2~4ms).
- Suporte a múltiplas Streaming Sources.
- Hysteresis de distância para evitar carregamento/descarregamento constante.

---

## 4. Hierarchical Level of Detail (HLOD)

### 4.1. Camadas de HLOD

| Camada   | Range Aproximado     | Representação                          | Quando usar                  |
|----------|----------------------|----------------------------------------|------------------------------|
| Full     | 0 – Loading Range    | Atores originais + LODs individuais    | Perto do jogador             |
| HLOD0    | Loading Range – HLOD1| Merge de instâncias por célula         | Distância média              |
| HLOD1    | > HLOD1 Range        | Mesh único simplificado ou Impostor    | Horizonte / vistas longas    |

### 4.2. Estratégias de Geração de HLOD

**HLOD0 (recomendado):**
- Agrupar todos os Static Meshes da célula
- Converter para Instanced Static Meshes (ou Hierarchical Instanced)
- Manter materiais originais quando possível
- Opcional: simplificação geométrica leve

**HLOD1:**
- Merge completo em um único mesh
- Redução agressiva de polígonos
- Ou substituição por Impostor (Octahedral / Billboard Atlas)
- Preferencialmente um único material

### 4.3. Transições

- Usar cross-fade ou dithering baseado em screen-size / distância
- Evitar pop-in abrupto

---

## 5. Sistema de LOD por Objeto + Impostors

### 5.1. LOD Individual

Cada mesh deve possuir múltiplos níveis de detalhe:

- **LOD0**: Alta qualidade
- **LOD1**: Média
- **LOD2**: Baixa
- **LOD3**: Impostor (quando aplicável)

**Critério de seleção:** Screen Size (preferencial) ou distância.

### 5.2. Impostors

Recomendado para distâncias médias/longas:

- **Billboard simples**: Mais barato, pior qualidade
- **Octahedral Impostors**: Melhor qualidade, custo moderado
- Atlas de impostors por tipo de prédio

---

## 6. Instancing Massivo

### 6.1. Requisitos

- Suporte a GPU Instancing / Multi-Draw Indirect
- Hierarchical Instanced Static Meshes (agrupamento por célula ou por tipo)
- Atualização eficiente de transforms (apenas quando necessário)

### 6.2. Boas Práticas

- Agrupar por material + mesh
- Separar objetos estáticos de objetos dinâmicos
- Usar buffers de instâncias com capacidade pré-alocada

---

## 7. Pipeline de Culling

Ordem recomendada:

1. **Frustum Culling** (por célula e por objeto)
2. **Distance Culling** (baseado nas ranges de HLOD)
3. **Occlusion Culling** (Hi-Z, Software Rasterization, ou Hardware Occlusion Queries)
4. **Screen Size Culling** (descartar objetos muito pequenos)

As células devem ser culladas antes dos objetos individuais.

---

## 8. Gerenciamento de Memória e Streaming de Assets

### 8.1. Orçamentos Recomendados (exemplo)

| Tipo de Asset     | Orçamento por célula (Full Detail) |
|-------------------|------------------------------------|
| Geometry          | 50–150 MB                          |
| Textures           | 80–200 MB                          |
| Total por célula  | ~150–300 MB                        |

### 8.2. Estratégias

- Streaming de texturas baseado em mipmaps e distância
- Compressão de meshes (quantização de vertex, etc.)
- Virtual Texturing (opcional, se o motor suportar)

---

## 9. Arquitetura de Dados Recomendada

### 9.1. Organização em Disco

- Uma pasta por célula (ou arquivo binário por célula)
- Separação clara entre:
  - Full Detail Actors
  - HLOD0
  - HLOD1
  - Metadados da célula

### 9.2. Conteúdo Típico de uma Célula

- Prédios modulares (Instanced)
- Props
- Estradas / calçadas
- Vegetação (se houver)
- Dados de colisão (simplificados para HLODs)

---

## 10. Fluxo de Runtime (Resumo)

**Por Frame:**

1. Atualizar Streaming Sources
2. Determinar conjunto de células desejadas
3. Atualizar estados de carregamento/descarregamento
4. Executar Culling (células → objetos)
5. Selecionar LOD / HLOD por objeto
6. Gerar comandos de desenho (instanced quando possível)
7. Submeter para a Rendering Pipeline

---

## 11. Prioridade de Implementação

| Prioridade | Sistema                        | Complexidade | Impacto |
|------------|--------------------------------|--------------|---------|
| 1          | World Partition + Streaming    | Alta         | Crítico |
| 2          | Frustum + Distance Culling     | Média        | Alto    |
| 3          | Instancing                     | Média        | Alto    |
| 4          | LOD Individual                 | Média        | Alto    |
| 5          | HLOD0                          | Alta         | Alto    |
| 6          | HLOD1 + Impostors              | Alta         | Médio   |
| 7          | Occlusion Culling avançado     | Alta         | Médio   |

---

## 12. Métricas de Sucesso

- Manter 60 FPS (ou target do projeto) em cidade densa
- Tempo de loading de célula < 100–200ms (assíncrono)
- Número de draw calls controlado (< 2000–4000 em cenas densas, dependendo do hardware)
- Uso de memória estável e previsível
- Transições de LOD/HLOD sem pop-in perceptível

---

## 13. Notas Finais

- Este documento assume um motor com renderer moderno (DX12/Vulkan/Metal), suporte a Indirect Drawing e sistema de cenas básico.
- Nanite, Virtual Geometry ou soluções proprietárias **não** são assumidas.
- A arquitetura é deliberadamente modular para permitir evolução incremental.

---

## 14. Changelog

| Versão | Data       | Descrição                  |
|--------|------------|----------------------------|
| 1.0    | 04/09/2026 | Versão inicial do documento |

---

*Documento gerado para uso interno no motor gráfico.*
