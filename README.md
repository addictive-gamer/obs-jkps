# obs-jkps

**JKPS - Keys Per Second**, como una fuente **nativa** de OBS Studio para Windows, con
instalador incluido — en vez de una aplicación aparte que hay que capturar como "juego".

> Remake conceptual de [JKPS](https://github.com/Tonetfal/JKPS) (por Tonetfal). Ver
> [`NOTICE.md`](NOTICE.md) para créditos y licencias.

## ¿Qué hace?

Añade una fuente nueva ("JKPS - Keys Per Second") al menú **Fuentes → Añadir** de OBS
Studio. Esa fuente dibuja directamente sobre tu escena:

- Hasta **8 teclas o botones del ratón** configurables, cada una con su propia casilla
  que cambia de color al presionarla.
- **KPS** (teclas por segundo), **Total** de pulsaciones y **BPM** estimado.
- Colores, tamaños, fuente y disposición (horizontal/vertical) totalmente configurables
  desde las propiedades de la fuente, como cualquier otra fuente de OBS.
- Un botón (y un hotkey de OBS asignable) para **reiniciar estadísticas**.
- Fondo con canal alfa real: puedes dejarlo transparente y no necesitas croma ni capturar
  nada como "juego" — es una fuente normal, compuesta con transparencia de verdad.

## Por qué es distinto del JKPS original

| | JKPS original | obs-jkps |
|---|---|---|
| Tipo | Aplicación de escritorio (SFML) | Fuente nativa de OBS (plugin) |
| Cómo se añade a OBS | Captura de ventana/juego | Fuentes → Añadir → JKPS |
| Transparencia | Requiere greenscreen | Alfa real |
| Configuración | Menú propio (Ctrl+A) | Panel de propiedades de OBS |
| Plataformas | Windows y Linux | Windows (ver [Limitaciones](#notas-técnicas--limitaciones)) |

## Instalación (usuarios)

1. Ve a la pestaña **[Releases](https://github.com/addictive-gamer/obs-jkps/releases)**
   de este repositorio.
2. Descarga `obs-jkps-<version>-Setup.exe`.
3. Cierra OBS Studio y ejecuta el instalador. Detecta automáticamente tu instalación de
   OBS Studio (por registro) y copia el plugin ahí.
4. Abre OBS Studio → **Fuentes → +  → JKPS - Keys Per Second**.

> El instalador requiere tener **OBS Studio ya instalado**. Si no lo encuentra, te avisará.

## Configuración de la fuente

Cada tecla tiene su propio grupo desplegable ("Tecla 1".."Tecla 8") con:

- **Activada**: si se muestra o no esa casilla.
- **Tecla / botón del ratón**: de una lista (letras, números, flechas, modificadores,
  numpad, M1-M5 de ratón...).
- **Etiqueta personalizada**: texto a mostrar en la casilla (si se deja vacío, usa el
  nombre por defecto de la tecla).

Por defecto vienen activadas **D F J K** (el layout clásico de 4 teclas de osu!mania /
mania), y S L A ; desactivadas como plantilla para 6K/7K/8K.

También puedes ajustar tamaño y separación de las casillas, tamaño de fuente, colores
(casilla en reposo, casilla presionada, texto, fondo con transparencia), y qué
estadísticas mostrar (KPS / Total / BPM) con su propio color y tamaño de fuente.

## Notas técnicas / limitaciones

- **Solo Windows.** La detección de teclas usa `GetAsyncKeyState` de la API de Windows,
  y el texto se renderiza con GDI (con una técnica de "texto blanco sobre negro" para
  obtener anti-aliasing con canal alfa real). En Linux/macOS el plugin carga pero no
  detecta pulsaciones (para mantener esas plataformas compilando en la CI).
- El **BPM** es una aproximación simplificada del algoritmo original: `BPM = KPS suavizado
  × 15` (60 segundos ÷ 4, asumiendo compases de 1/4, igual que hace JKPS), aplicado sobre
  un promedio móvil exponencial en vez del acumulador de 60 ticks fijos del original.
- La detección de teclas es *global* (funciona aunque OBS no tenga el foco), igual que el
  JKPS original — ten esto en cuenta si compartes pantalla con información sensible.

## Compilar desde el código fuente

Este repo usa la
[plantilla oficial de plugins de OBS Studio](https://github.com/obsproject/obs-plugintemplate),
así que el flujo de compilación es el estándar de cualquier plugin de OBS:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo
```

Esto genera `release/RelWithDebInfo/obs-jkps/bin/64bit/obs-jkps.dll` y su carpeta `data/`.

### Generar el instalador (Setup.exe)

Con [Inno Setup](https://jrsoftware.org/isinfo.php) instalado:

```powershell
ISCC installer\windows\obs-jkps-setup.iss
```

El `.exe` resultante queda en `release/installer/`.

### CI/CD (recomendado)

No necesitas compilar nada localmente: el workflow
[`.github/workflows/windows-installer.yml`](.github/workflows/windows-installer.yml)
compila el plugin y genera el instalador automáticamente en GitHub Actions:

- Al hacer push de un tag de versión (`1.0.0`, `1.1.0`, ...), publica el instalador como
  asset de un Release.
- También se puede lanzar manualmente desde la pestaña **Actions** (workflow_dispatch),
  quedando el instalador disponible como artefacto descargable.

El resto de workflows heredados de la plantilla (`build-project.yaml`, `push.yaml`,
`pr-pull.yaml`, `check-format.yaml`) siguen compilando el plugin en Windows/macOS/Linux
como de costumbre (empaquetado en `.zip`), útiles para verificar que el código compila en
cada plataforma aunque el instalador solo tenga sentido en Windows.

## Estructura del repositorio

```
src/                    Código fuente del plugin (C)
  plugin-main.c          Punto de entrada del módulo de OBS
  jkps-source.c/.h        Fuente de OBS: propiedades, tick, render, hotkey
  jkps-input.c/.h         Sondeo de teclas (GetAsyncKeyState) y cálculo de KPS/Total/BPM
  jkps-render.c/.h        Render GDI de casillas y texto a un buffer RGBA
  jkps-keynames.c/.h      Tabla de teclas/botones disponibles en las propiedades
data/locale/            Textos de la interfaz (en-US, es-ES)
installer/windows/      Script de Inno Setup del instalador de Windows
cmake/, CMakeLists.txt  Sistema de compilación (heredado de obs-plugintemplate)
.github/workflows/      CI: compilación multiplataforma + generación del instalador
```

## Licencia

GPLv2 (o posterior) — ver [`LICENSE`](LICENSE). Ver [`NOTICE.md`](NOTICE.md) para los
créditos al proyecto original JKPS (MIT, por Tonetfal).
