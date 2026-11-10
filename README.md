# Candy — Engine 2.0

Перерождение движка 2017 года (`D:\Engine1.0\Project1`, GLLern) на современном стеке:

- **C++23** (MSVC `/std:c++latest`, только стабильно поддерживаемые фичи),
- **CMake** + FetchContent — никаких захардкоженных `libs\lib`,
- рендер-агностичный **RHI** с двумя бэкендами: **OpenGL 4.6** (референс — отрефакторенная
  версия старого кода) и **Vulkan 1.4** (целевой),
- UI: **Dear ImGui** (docking) вместо AntTweakBar,
- фичи старого движка: террейн из heightmap со сплаттингом, скелетная анимация (Assimp),
  Bullet Physics (спавн тел из камеры), трава с тесселяцией, вода с отражением/преломлением,
  skybox, shadow mapping, HDR, инстансинг, фрустум-куллинг.

## Сборка

Требования: Visual Studio 2022 (Desktop C++), CMake 3.28+. Для этапа Vulkan — Vulkan SDK.

Зависимости (GLFW, GLM, spdlog, Dear ImGui, затем Assimp/Bullet/VMA) скачиваются
автоматически через CMake FetchContent при первой конфигурации.

```powershell
cmake --preset debug
cmake --build --preset debug
.\build\debug\app\Debug\candy.exe
```

Release:

```powershell
cmake --preset release
cmake --build --preset release
.\build\release\app\Release\candy.exe
```

## Структура

```
engine/   движок: core (лог/время), platform (окно/ввод), ui (ImGui),
          rhi/ + renderers/{gl,vk} (появятся в M2/M3), assets, scene, physics
app/      исполняемый файл «candy» — сцена в духе старого проекта
shaders/  общие GLSL (для Vulkan компилируются в SPIR-V через glslc)
assets/   ресурсы (модели, текстуры, heightmaps из Engine1.0)
cmake/    вспомогательные cmake-модули (warnings, dependencies, shaders)
```

## Вехи

- **M0** — скелет: окно + ImGui docking, сборка на CMake ✔ (текущий)
- **M1** — чистое ядро: ассеты/анимация/физика/сцена (рефакторинг кода 2017 года)
- **M2** — OpenGL 4.6 референс-рендерер (вся канфетка старой сцены)
- **M3** — Vulkan 1.4 бэкенд за тем же RHI
- **M4** — полировка: профилировщик, переключение бэкендов, bloom
