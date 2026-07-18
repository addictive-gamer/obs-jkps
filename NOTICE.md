# Créditos / Notice

Este proyecto (**obs-jkps**) es un *remake* conceptual de **[JKPS](https://github.com/Tonetfal/JKPS)**,
una aplicación independiente creada por **Tonetfal** que muestra teclas pulsadas, teclas por
segundo (KPS), total de pulsaciones y BPM estimado, pensada originalmente para juegos de
ritmo (osu!, Etterna, StepMania, etc.) y pensada para capturarse como una "fuente de juego"
dentro de OBS Studio.

JKPS original está licenciado bajo la **Licencia MIT**:

```
MIT License
Copyright (c) 2021 Tonetfal
```

**obs-jkps no reutiliza el código fuente C++/SFML de JKPS.** Es una reescritura completa,
desde cero, en C, implementada como una fuente nativa de OBS Studio (en vez de una ventana
independiente que hay que capturar), usando la API pública de plugins de OBS
(`obs-module.h`/`obs-source.h`) y GDI de Windows para el renderizado de texto. Al enlazar
contra `libobs` (con licencia GPLv2), el código de este plugin se distribuye bajo la
**GNU General Public License v2 (o posterior)** — ver [`LICENSE`](LICENSE).

Este remake conserva la idea, el propósito y varias convenciones de JKPS (por ejemplo, la
fórmula `BPM = KPS × 15`, asumiendo compases de 1/4, tal y como hace el proyecto original),
pero no pretende sustituir ni reclamar autoría sobre el proyecto original. Si buscas la
aplicación independiente original con todas sus funciones (temas, visualización de teclas,
greenscreen, etc.), visita el repositorio de Tonetfal enlazado arriba.
