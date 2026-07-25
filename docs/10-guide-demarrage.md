# 10 — Guide de démarrage

## Installer (sans compiler)

Deux formats, produits par la CI de release à chaque tag `v*` :

- **Installeur Windows en un fichier** — `sillage-setup-<version>.exe` : installe dans
  Program Files, raccourcis menu Démarrer, option « service Windows 24/7 » cochable à
  l'installation, désinstalleur propre. (Script : `packaging/windows/sillage.iss`.)
- **Version portable** — `sillage-<version>-portable-<os>.zip|.tar.gz` (~300 Ko) :
  décompressez n'importe où, lancez `run-sillage.bat` (Windows) ou `./run-sillage.sh`
  (Linux/macOS) — le moteur démarre et l'interface s'ouvre. Aucun droit admin, aucune
  trace hors du dossier. Idéal clé USB / machine de prestation.

```bash
# construire le portable localement :
cmake --preset windows-msvc && cmake --build --preset windows-msvc
cd build/windows-msvc && cpack -C Release
```

## Compiler

Prérequis : CMake ≥ 3.24 et un compilateur C++20 (MSVC 2022, GCC 11+, AppleClang 14+).
Aucune dépendance à installer — GoogleTest est téléchargé à la configuration.

```bash
git clone <repo> sillage && cd sillage
cmake --preset windows-msvc   # ou: linux / macos
cmake --build --preset windows-msvc
ctest --preset windows-msvc   # 31 tests + scénarios MOT
```

Le binaire : `build/<preset>/engine/Release/sillage-engine[.exe]` (dossier `ui/` copié à côté).

## Lancer en mode démo (sans matériel)

```bash
sillage-engine
```

- **UI** : http://127.0.0.1:8080 — scène live (molette = zoom, glisser = pan,
  double-clic = recadrer), panneau capteurs, liste des personnes, temps de tick.
- **OSC Augmenta** : envoyé sur `127.0.0.1:12000` (testez avec Protokol ou Chataigne :
  messages `/au/personEntered|personUpdated|personWillLeave|scene`).
- Deux capteurs virtuels en coins opposés trackent 3 agents simulés (2 croiseurs + 1
  aléatoire). `--agents 5` pour plus de monde, `--seed` pour varier.

Le moteur apprend d'abord le fond (~1 s, salle vide) — bannière dans l'UI.

## Capteurs supportés

| Type | Protocole | Flag / config | Statut |
|---|---|---|---|
| Hokuyo URG/UST | SCIP 2.2, TCP 10940 | `--hokuyo host[@x,y,θ]` / `"type":"hokuyo"` | validé contre capteur simulé |
| SICK TiM 5xx/7xx | CoLa A, TCP 2112 | `--sick host[@x,y,θ]` / `"type":"sick"` | validé contre capteur simulé |
| **Pont UDP universel** | JSON/UDP | `--udp-sensor port[@x,y,θ]` / `"type":"udp"` | validé en réel |
| RPLIDAR (série) | via le pont UDP | `tools/bridges/rplidar_bridge.py` | script fourni |

Le **pont UDP** rend n'importe quel capteur compatible : un script envoie des datagrammes
`{"a0":<rad>,"da":<rad>,"d":[mm,...]}` (un tour par datagramme) et Sillage le traite
comme un capteur natif (santé, fusion, reconnexion). Exemple RPLIDAR complet dans
[tools/bridges/](../tools/bridges/rplidar_bridge.py).

## Brancher un vrai LiDAR (Hokuyo URG/UST, Ethernet)

```bash
# host[:port][@x,y,theta] — pose du capteur dans le repère salle (mètres, radians)
sillage-engine --no-sim --room 12x9 --hokuyo 192.168.0.10@0.2,0.2,0.785
sillage-engine --no-sim --room 12x9 \
    --hokuyo 192.168.0.10@0.2,0.2,0.785 \
    --hokuyo 192.168.0.11@11.8,8.8,-2.356   # fusion 2 capteurs
```

Reconnexion automatique en cas de coupure ; état visible dans le panneau Capteurs et sur
`GET /api/status`. La calibration assistée (drag & drop + marche) arrive en M2 — en
attendant, mesurez les poses au mètre.

> Le driver SCIP 2.2 est validé contre un capteur simulé (tests) ; la validation sur
> matériel physique est en cours (voir roadmap M1).

## Options

| Option | Effet |
|---|---|
| `--http-port <p>` | port UI/API (8080) |
| `--osc-host/--osc-port` | destination OSC Augmenta (127.0.0.1:12000) |
| `--room WxH` | dimensions salle en mètres (10x8) |
| `--hokuyo host[:port][@x,y,θ]` | ajoute un capteur réel (répétable) |
| `--no-sim` | désactive le simulateur de démo |
| `--headless` | sans serveur HTTP |
| `--ticks n` | s'arrête après n ticks (tests) |

## Le fichier projet

Toute l'installation tient dans un JSON versionné (salle, capteurs, zones, sorties) —
voir [examples/demo-project.json](../examples/demo-project.json) :

```bash
sillage-engine --save-config monprojet.json   # génère un fichier de départ
sillage-engine --config monprojet.json        # le charge (les flags CLI ont priorité)
```

**Zones** : polygones nommés dans le repère salle. Chaque entrée/sortie émet un événement
OSC (`/sillage/zone/<nom>/enter|exit <id>`), alimente les compteurs (occupants, entrées
totales), le panneau Zones de l'UI et le fil d'événements.

**Sorties** (activables par fichier projet) :
| Sortie | Destination type | Consommateurs |
|---|---|---|
| `augmentaOsc` | 12000/udp | TouchDesigner, Unity, Unreal (plugins Augmenta) |
| `tuio` (2Dcur) | 3333/udp | mapping, multitouch, vieux logiciels interactifs |
| `admOsc` | 4001/udp | L-ISA, SPAT Revolution, d&b Soundscape (objets audio) |

**Conditionnement** : `predictionSeconds` (compense la latence capteur+rendu en
extrapolant les positions) et `smoothing` (filtre One-Euro — plus doux, un peu moins
réactif). Appliqués aux sorties uniquement, jamais au tracker.

## Lancer en service (24/7)

**Windows** — depuis un PowerShell **administrateur** :

```powershell
.\packaging\windows\install-service.ps1 -BinaryPath "C:\Program Files\Sillage\sillage-engine.exe" -ConfigPath "C:\ProgramData\Sillage\project.json"
```

Le script enregistre le service avec le drapeau `--service` (le moteur devient alors un
vrai service SCM), configure le redémarrage automatique en cas de panne, puis **vérifie**
que le service tourne *et* que `GET /api/status` répond — sinon il échoue en affichant la
commande enregistrée et où lire le journal d'événements. L'installeur propose la même
opération via une case à cocher.

**Ubuntu / macOS** — unité **systemd** durcie et **launchd** dans
[packaging/](../packaging/).

`--http-bind 0.0.0.0` expose l'interface sur le réseau local (tablette en régie).

> ⚠️ L'API n'a pas d'authentification : n'exposez le moteur que sur un réseau de confiance
> (VLAN régie), jamais directement sur Internet.

## Mode spectacle

Trois commandes conçues pour être utilisées sous pression, dans le noir — barre en bas
de l'interface, ou API pour un pupitre externe :

| Action | Effet | API |
|---|---|---|
| 🔒 **Verrou show** | La configuration passe en lecture seule : plus aucune modification possible (ni clic, ni POST). Le tracking continue normalement. | `POST /api/show/lock` · `/unlock` |
| ⏹ **Couper les sorties** | Coupure d'urgence : OSC, TUIO et ADM cessent instantanément. Le tracking et l'interface continuent, pour garder l'œil sur la salle. | `POST /api/show/mute` · `/unmute` |
| ↻ **Réapprendre le fond** | Réapprend le décor. **La salle doit être vide** (~1 s). Indispensable si le moteur a démarré avec du public déjà installé. | `POST /api/background/relearn` |

**Bandeau d'alarme** : un capteur hors ligne, tous les capteurs perdus, les sorties
coupées ou le verrou actif s'affichent en pleine largeur, en haut, en contraste fort —
avec la cause réelle nommée (un capteur débranché n'est plus confondu avec « salle vide »).

**Dégradation par capteur** : chaque capteur apprend son fond indépendamment. Un LiDAR
absent, en panne ou lent à démarrer ne bloque plus le moteur — les autres continuent de
tracker, et le bandeau signale la couverture réduite.

## Enregistrer et rejouer (boîte noire)

```bash
sillage-engine --record session.srec       # enregistre les scans bruts en parallèle
sillage-engine --replay session.srec       # rejoue à l'identique (UI + OSC compris)
```

Le replay est **déterministe** : mêmes scans ⇒ mêmes tracks (testé). Un problème sur
site se capture avec `--record` et se rejoue au bureau, réglages et debug compris.

## Outils de validation et de debug

```bash
sillage-engine --eval                      # bibliothèque de scénarios MOT + verdicts
sillage-engine --debug-scenario crossing_x # trace naissances/morts/échanges d'ID
```

`--eval` retourne un code de sortie non nul si un gate non-quarantainé échoue — c'est la
commande exécutée en CI. Chaque colonne : ID switches, MOTA, IDF1, misses, faux positifs,
IDs distincts, pic de tracks simultanés.

## Recevoir les données dans vos logiciels

- **TouchDesigner / Unity / Unreal** : utilisez les plugins Augmenta officiels pointés sur
  l'IP du moteur, port 12000 (conformité complète validée en M4 — les messages legacy
  fonctionnent déjà pour les cas simples).
- **Chataigne** : module Augmenta ou OSC brut.
- **Custom** : consommez le WebSocket `ws://host:8080/ws` (JSON par tick : `points`,
  `tracks` avec `id/x/y/vx/vy/r/coast/age`) ou `GET /api/status` pour la santé.
