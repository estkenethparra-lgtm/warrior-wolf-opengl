# Warrior + Hell Wolf (OpenGL)

Escena 3D en **C++ / OpenGL 3.3** donde un guerrero animado permanece de pie sobre un suelo mientras un lobo animado corre en círculo a su alrededor, siguiendo un recorrido hecho con **curvas de Bézier**. Los personajes se cargan desde archivos **glTF/GLB** con animación por huesos (*skinning*).

---

## Contenido

- [Características](#características)
- [Tecnologías](#tecnologías)
- [Estructura del proyecto](#estructura-del-proyecto)
- [Requisitos](#requisitos)
- [Compilación](#compilación)
- [Configuración](#configuración)
- [Controles](#controles)
- [Cómo funciona](#cómo-funciona)
- [Ajuste de la orientación de un modelo](#ajuste-de-la-orientación-de-un-modelo)
- [Solución de problemas](#solución-de-problemas)
- [Créditos](#créditos)

---

## Características

- Carga de modelos `.gltf` / `.glb` con **Assimp**, incluyendo mallas, materiales, texturas (externas o incrustadas) y huesos.
- **Animación esquelética** en GPU (hasta 128 huesos y 4 influencias por vértice), reproduciendo la primera animación del archivo en bucle.
- Recorrido circular formado por **4 curvas de Bézier cúbicas**, con la cabeza del lobo siempre alineada con la tangente de la curva.
- Velocidad del lobo constante (en unidades por segundo), independiente de los FPS.
- **Suelo** configurable: un plano con textura propia o un modelo glTF.
- Iluminación sencilla (ambiente, difusa y especular) con vector de luz fijo.
- Cámara orbital controlada con el ratón.
- Corrección de orientación por modelo (pitch, yaw y roll) y **teclas para calibrarla en ejecución**.
- Normalización automática: cada modelo se escala a una altura objetivo, se centra y se apoya en `Y = 0`.

## Tecnologías

| Librería | Uso |
|---|---|
| **OpenGL 3.3 Core** | Render |
| **GLFW** | Ventana, entrada y contexto |
| **GLAD** | Carga de funciones de OpenGL |
| **GLM** | Matemáticas (matrices, vectores, cuaterniones) |
| **Assimp** | Importación de glTF/GLB, huesos y animaciones |
| **stb_image** | Carga de texturas (`.jpg`, `.jpe`, `.png`, ...) |
| **C++17** | `std::filesystem`, `std::clamp`, etc. |

## Estructura del proyecto

```
proyecto/
├── main.cpp            # Todo el código de la aplicación
├── stb_image.h         # Cabecera de stb_image (debe estar junto a main.cpp)
├── README.md
└── (CMakeLists.txt)    # Opcional, ver sección de compilación
```

Dentro de `main.cpp`:

| Elemento | Descripción |
|---|---|
| `ShaderProgram` | Compila y enlaza shaders, y fija uniforms. |
| `BezierCurve` | Un tramo de Bézier cúbica: posición y tangente. |
| `BezierLoop` | Círculo de 4 tramos de Bézier y matriz de orientación para que `+X` del modelo siga la tangente. |
| `SkinnedModel` | Carga el modelo, procesa mallas/huesos/texturas, anima y dibuja. |
| `GroundMesh` / `createGround` | Plano de respaldo con textura repetida. |
| `main()` | Ventana, carga de recursos y bucle principal. |

## Requisitos

- Compilador con soporte de **C++17** (MSVC 2019+, GCC 9+, Clang 10+).
- GPU y drivers con **OpenGL 3.3** o superior.
- Las librerías de la tabla de tecnologías.
- Los modelos y texturas descritos en [Configuración](#configuración).

## Compilación

### Windows con Visual Studio + vcpkg (recomendado)

```bash
vcpkg install glfw3 glad glm assimp --triplet x64-windows
vcpkg integrate install
```

Coloca `stb_image.h` junto a `main.cpp`, crea un proyecto de consola vacío, añade `main.cpp` y compila en **x64** con el estándar **C++17** o superior.

### CMake (ejemplo)

```cmake
cmake_minimum_required(VERSION 3.16)
project(WarriorWolf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

add_executable(WarriorWolf main.cpp)

target_link_libraries(WarriorWolf PRIVATE
    glfw glad::glad glm::glm assimp::assimp)
```

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<ruta-a-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Configuración

Las rutas y parámetros principales están como constantes al inicio de `main.cpp`.

### Rutas de recursos

| Constante | Qué es |
|---|---|
| `WARRIOR_DIR` | Carpeta del modelo del guerrero. |
| `WOLF_DIR` | Carpeta del modelo del lobo. |
| `GROUND_DIR` | Carpeta del suelo en glTF (solo si `USE_GLTF_GROUND = true`). |
| `GROUND_TEX` | Imagen para el suelo plano (por ejemplo `suelo.jpe`). |

En cada carpeta de modelo, el programa busca `scene.gltf`, luego `scene.glb`, y si no los encuentra toma el primer `.gltf` o `.glb` que haya dentro (búsqueda recursiva). Mantén junto al archivo su `.bin` y sus texturas.

### Suelo

| Constante | Descripción |
|---|---|
| `USE_GLTF_GROUND` | `false`: plano con `GROUND_TEX`. `true`: usa el modelo de `GROUND_DIR`. |
| `GROUND_SIZE` | Tamaño del suelo glTF (su dimensión mayor, en unidades). |
| `GROUND_Y_OFFSET` | Ajuste vertical del suelo; negativo lo baja. |

Para el plano con textura, en `main()`:

```cpp
ground = createGround(20.0f, 8.0f, GROUND_TEX);
//                    ^mitad del lado   ^repeticiones de la textura por lado
```

Si no se encuentra la textura, el suelo se dibuja de color gris liso y la consola lo avisa.

### Escena

| Parámetro | Dónde | Efecto |
|---|---|---|
| `warriorPos` | `main()` | Posición del guerrero y centro del círculo. |
| `loopRadius` | `main()` | Radio del recorrido del lobo. |
| `wolfSpeed` | `main()` | Velocidad del lobo (unidades/segundo). |
| Altura de los modelos | Constructores de `SkinnedModel` | Segundo parámetro (por ejemplo `2.4f` el guerrero, `1.7f` el lobo). |
| Orientación de los modelos | Constructores de `SkinnedModel` | Último parámetro: `glm::vec3(pitch, yaw, roll)` en grados. |
| `gDist`, `gYaw`, `gPitch` | Inicio del archivo | Distancia y ángulos iniciales de la cámara. |

## Controles

| Entrada | Acción |
|---|---|
| **Arrastrar con clic izquierdo** | Orbitar la cámara. |
| **Rueda del ratón** | Acercar / alejar. |
| **Tecla 1** | Suma 90° al valor X de la corrección del lobo (giro sobre su eje de avance). |
| **Tecla 2** | Suma 90° al valor Y (yaw). |
| **Tecla 3** | Suma 90° al valor Z. |

Cada vez que se pulsa 1, 2 o 3, la consola imprime los valores actuales, por ejemplo `Fix lobo = (270, 0, 90)`.

## Cómo funciona

1. **Carga**: Assimp importa el modelo con triangulación y límite de 4 pesos por vértice. Se recorre el árbol de nodos creando una malla de OpenGL por cada `aiMesh`.
2. **Normalización**: se calcula la caja envolvente con la corrección de orientación aplicada; el modelo se escala a la altura objetivo, se centra en X/Z y se apoya en `Y = 0`.
3. **Animación**: en cada fotograma se recorre la jerarquía de nodos interpolando posición (lineal), rotación (*slerp*) y escala; las matrices resultantes de cada hueso se envían al vertex shader (`uBones`).
4. **Recorrido del lobo**: la fase avanza en `[0, 1)` con `dt * velocidad / circunferencia`. Con ella se obtiene la posición sobre el círculo de Bézier y su tangente, y la matriz de orientación hace que la cabeza (`+X` local) apunte hacia donde corre.
5. **Dibujo**: suelo, curva naranja (opcional visualmente) y personajes con el mismo shader de malla.

## Ajuste de la orientación de un modelo

Los modelos glTF suelen venir con ejes distintos. El programa espera que, tras la corrección, el modelo quede **de pie sobre `Y` y con la cabeza hacia `+X`**.

1. Ejecuta el programa y observa cómo queda el lobo.
2. Pulsa **1**, **2** o **3** hasta que quede de pie y con la cabeza hacia adelante.
3. Copia los valores impresos en la consola al constructor:

```cpp
SkinnedModel hellWolf(wolfFile, 1.7f, true, glm::vec3(270.0f, 0.0f, 90.0f));
```

El orden de aplicación es: primero Z, luego Y y al final X, de modo que X siempre gira el modelo sobre su eje de avance.

## Solución de problemas

| Problema | Posible causa y solución |
|---|---|
| Ventana negra o cierre inmediato | Revisa las rutas de `WARRIOR_DIR` y `WOLF_DIR`; la consola muestra `[Assimp Error]` si no se pudo cargar. |
| Un modelo aparece sin textura | La textura no está junto al `.gltf`, o en su subcarpeta `textures/`. Revisa las rutas internas del archivo. |
| El guerrero aparece boca abajo | Cambia `-90.0f` por `90.0f` en la corrección de pitch del guerrero. |
| El lobo corre de lado, de cabeza o hacia atrás | Usa las teclas 1, 2 y 3 (ver arriba). |
| El lobo corre en sentido contrario al círculo | Cambia el signo del avance de `wolfPhase` y corrige el valor negativo con `if (wolfPhase < 0) wolfPhase += 1.0f;`. |
| Se ven costuras en el suelo | La textura no es *seamless*; baja las repeticiones de `createGround` o usa una textura "tileable". |
| Pies hundidos o flotando | Ajusta `GROUND_Y_OFFSET` o la altura de la malla del suelo. |
| Error de shader en consola | Verifica que tu GPU soporte OpenGL 3.3 y que los drivers estén actualizados. |
| Fallo al cerrar la ventana | Los modelos deben destruirse antes de `glfwTerminate()`; no saques su declaración del bloque `{ }` de `main()`. |

## Créditos

- Modelos del guerrero, el lobo y el suelo: *(agrega aquí el autor, la fuente y la licencia de cada modelo)*.
- Textura del suelo: *(agrega la fuente y la licencia)*.
- Librerías: GLFW, GLAD, GLM, Assimp y stb_image, cada una con su respectiva licencia.