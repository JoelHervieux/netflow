<div align="center">

<img src="https://img.shields.io/badge/N-NetFlow-7c8cff?style=for-the-badge&logo=windows&logoColor=white&labelColor=0d0f14" alt="NetFlow" height="48">

# NetFlow

**Client terminal SSH & Serial natif pour Windows**
*Léger, rapide, soigné — C pur avec Win32 API.*

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2B-0d0f14?style=flat-square)](#)
[![Language](https://img.shields.io/badge/language-C99-7c8cff?style=flat-square)](#)
[![UI](https://img.shields.io/badge/UI-Win32%20%2B%20GDI-34d399?style=flat-square)](#)
[![SSH](https://img.shields.io/badge/SSH-libssh2-fbbf6b?style=flat-square)](#)
[![License](https://img.shields.io/badge/license-MIT-a4adba?style=flat-square)](LICENSE)

</div>

---

## Aperçu

NetFlow est un client terminal pour se connecter à des **switches, routeurs et équipements réseau** via SSH ou port COM série. Construit en C pur avec Win32 API et GDI — aucune dépendance lourde, démarrage instantané, empreinte mémoire minimale.

Pensé pour les ingés réseau qui jonglent entre dix sessions à la fois et veulent un terminal qui ne crie pas.

```
┌────────────────────────────────────────────────────────────┐
│ ▌ NetFlow      SSH  admin@192.168.1.1            ─  ▢  ✕   │
├────────────────────────────────────────────────────────────┤
│ [SSH] admin@192.168.1.1  ✕   [COM] COM3  switch-edge   +   │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  admin@switch-edge:~$ show interface brief                 │
│                                                            │
│  Gi1/0/1    connected     10     a-full   a-1000           │
│  Gi1/0/2    connected     20     a-full   a-1000           │
│  Gi1/0/3    notconnect    1      auto     auto             │
│                                                            │
│  admin@switch-edge:~$ █                                    │
│                                                            │
├────────────────────────────────────────────────────────────┤
│ ● connecté · SSH · admin@192.168.1.1            80 × 24    │
└────────────────────────────────────────────────────────────┘
```

## Fonctionnalités

- **SSH** via libssh2 — authentification password / clé publique
- **Serial** natif Win32 — détection automatique des ports COM, configuration baud / data / parité / stop bits
- **Onglets multiples** style Chrome — sessions parallèles, soulignement animé
- **Émulateur VT100/ANSI** — séquences CSI, 16 couleurs, scrollback configurable
- **Sélection & presse-papier** — clic-glisser, `Ctrl+Shift+C` / `Ctrl+Shift+V`
- **Thème sombre soigné** — palette slate calme, un seul accent indigo, pensée pour les sessions longues
- **Mouvement fluide** — animations GDI à 60 Hz par interpolation exponentielle, ~0 % CPU au repos
- **Chrome custom** — barre de titre, onglets et status bar maison, intégration drag / snap Windows
- **Plein écran** — `F11`
- **Persistance des sessions** entre redémarrages

## Stack technique

| | |
|---|---|
| **Langage**     | C99 |
| **OS**          | Windows 10+ |
| **UI**          | Win32 API + GDI (pas de CSS, pas de GPU compositing) |
| **SSH**         | [libssh2](https://www.libssh2.org/) |
| **Serial**      | Win32 `CreateFile` + DCB |
| **Terminal**    | parser VT100 / ANSI maison |
| **Polices**     | Segoe UI, Cascadia Code, Segoe MDL2 Assets (toutes intégrées à Windows 10+) |
| **Build**       | MSVC (Visual Studio) ou MinGW-w64 |

## Build

### Visual Studio

```powershell
git clone https://github.com/<vous>/netflow.git
cd netflow
msbuild netflow.sln /p:Configuration=Release /p:Platform=x64
```

### MinGW-w64

```bash
gcc -O2 -std=c99 -Wall \
    -o netflow.exe src/*.c \
    -lssh2 -lws2_32 -lgdi32 -lcomctl32 -lcomdlg32 -luser32 -lkernel32
```

Le binaire produit est statique vis-à-vis de Windows (aucune DLL système hors libssh2).

## Raccourcis clavier

| Combinaison           | Action                       |
|-----------------------|------------------------------|
| `Ctrl+N`              | Nouvelle session SSH         |
| `Ctrl+Shift+N`        | Nouvelle session Serial      |
| `Ctrl+W`              | Fermer l'onglet actif        |
| `Ctrl+Shift+C`        | Copier la sélection          |
| `Ctrl+Shift+V`        | Coller depuis le presse-papier |
| `Page Up` / `Page Dn` | Scrollback du terminal       |
| `F11`                 | Plein écran                  |
| `Esc`                 | Fermer une modale            |

## Structure du projet

```
src/
├── ui.h / ui.c             Tokens de design (palette, dimensions) + primitives GDI + UiAnim
├── window.h / window.c     Boucle d'événements, layout, paint principal, timer 60 Hz
├── tabs.h / tabs.c         Gestionnaire d'onglets, soulignement animé
├── terminal.h / terminal.c Parser VT100, grille de cellules, sélection, scrollback
├── ssh.h / ssh.c           Wrapper libssh2 + thread de lecture
├── serial.h / serial.c     Wrapper Win32 COM + énumération des ports
├── session.h / session.c   Persistance JSON-like des sessions
└── main.c                  Entry point
```

## Direction artistique

Toute la direction artistique (palette en RGB, dimensions exactes, ramp typographique, mockups au pixel près, table d'animations) est documentée dans **[`design/index.html`](design/index.html)** — ouvre le fichier dans n'importe quel navigateur.

Trois principes&nbsp;:

- **Surfaces calmes** — slate neutre, un seul indigo (`#7c8cff`), hiérarchie progressive de noir à noir-bleu
- **Précision typographique** — Segoe UI pour le chrome, Cascadia Code pour le terminal, labels en caps semibold 10 pt
- **Mouvement réaliste** — chase exponentiel `v += (target − v)·(1 − e^(−k·dt))`, pilotage par un unique `WM_TIMER` 60 Hz, sans thread

## Roadmap

- [ ] Authentification SSH par clé (en cours)
- [ ] Support des séquences ANSI 256 couleurs et 24-bit truecolor
- [ ] Recherche dans le scrollback (`Ctrl+F`)
- [ ] Profils de connexion sauvegardés avec snippets
- [ ] Mode "broadcast" — envoyer une commande à plusieurs onglets simultanément
- [ ] Configuration par fichier (couleurs, polices, raccourcis)

## Contribuer

Pull requests bienvenues. Pour les changements importants, ouvre d'abord une issue pour discuter de ce que tu veux changer.

Avant de soumettre&nbsp;:
- Garder le style C99 du projet (pas de C++)
- Garder l'absence de dépendances externes hors libssh2
- Vérifier que `Release x64` compile sans warning

## Licence

[MIT](LICENSE) — fais ce que tu veux, mais cite le projet.

## Crédits

- [libssh2](https://www.libssh2.org/) — couche SSH
- Inspirations visuelles&nbsp;: Windows Terminal, Warp, Arc Browser, VS Code

---

<div align="center">
<sub>Construit avec Win32 API · GDI · libssh2 · VT100</sub><br>
<sub>Pas de CSS, pas de GPU, juste des pixels.</sub>
</div>
