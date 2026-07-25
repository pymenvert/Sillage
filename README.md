# Sillage — Tracking de personnes multi-LiDAR pour salles immersives

> **Sillage** : la trace qu'on laisse derrière soi. Un moteur de tracking temps réel qui suit
> plusieurs personnes dans un espace physique à l'aide d'un ou plusieurs LiDARs, avec des
> identifiants persistants, et diffuse leurs positions vers les logiciels créatifs
> (TouchDesigner, Unity, Unreal, Madmapper, Resolume…).

**Statut : v1.0.1** — voir le [CHANGELOG](CHANGELOG.md), le
[guide de démarrage](docs/10-guide-demarrage.md) et les limitations connues (validation
sur matériel physique en tête de la feuille de route post-1.0).

**Licence : [PolyForm Noncommercial 1.0.0](LICENSE.md)** — usage non commercial libre
(artistes, écoles, recherche, évaluation) ; l'usage commercial requiert une licence.

```bash
cmake --preset windows-msvc && cmake --build --preset windows-msvc
build/windows-msvc/engine/Release/sillage-engine   # UI sur http://127.0.0.1:8080
```

## Ce que fait Sillage — livré en v1.0.1

- **Tracking multi-personnes** en temps réel (position, vitesse, taille, trajectoire) avec des
  **IDs stables** qui survivent aux croisements et aux occultations. Mesuré en continu par
  7 scénarios en CI : 0 échange d'ID sur croisements, demi-tours, côte-à-côte et occultations.
- **Fusion multi-LiDAR** : plusieurs capteurs couvrent une même salle, fusionnés au niveau des
  points dans un référentiel commun — les angles morts d'un capteur sont couverts par les autres.
  Chaque capteur apprend son fond indépendamment : un capteur absent réduit la couverture,
  il n'arrête pas le moteur.
- **Capteurs** : Hokuyo URG/UST (SCIP 2.2), SICK TiM (CoLa A), **pont UDP universel** rendant
  n'importe quel capteur compatible via de simples datagrammes JSON (script RPLIDAR fourni),
  plus simulateur et replay.
- **Sorties** : OSC compatible **protocole Augmenta** (plugins TouchDesigner/Unity/Unreal
  existants), **TUIO 1.1**, **ADM-OSC** (audio spatialisé : L-ISA, SPAT, d&b Soundscape),
  WebSocket JSON, et événements de zones en OSC.
- **Mode spectacle** : verrou de configuration, coupure d'urgence des sorties, réapprentissage
  du fond, bandeau d'alarme nommant la cause réelle d'un défaut.
- **Interface web embarquée** : scène live, éditeur de zones à la souris, réglages appliqués
  à chaud, heatmap, mode présentation — depuis n'importe quel navigateur.
- **Enregistrement / replay déterministe** (`.srec`) : rejouer une session à l'identique pour
  déboguer ou valider une mise à jour.
- **Simulateur + métriques MOT** (`--eval`, `--debug-scenario`) : la robustesse du tracker est
  une porte de CI, pas une promesse.
- **Multiplateforme** : Windows 10/11 (service SCM natif), Ubuntu 22.04+ (systemd),
  macOS 13+ universel (Apple Silicon et Intel), fonctionnement headless.

### Prévu, non livré

Ces éléments sont conçus et documentés mais **absents de la v1.0.1** : sortie **PSN**
(PosiStageNet) et **MQTT**, enregistrement au format **MCAP**/Foxglove (le format actuel est
`.srec`, natif), profilage **Tracy**, commande **`doctor`**, injection de pannes et export de
diagnostic, perception **3D** native, workflow de calibration dans l'interface (le solveur
existe et est testé, l'assistant guidé non). Les documents `docs/01`–`docs/09` décrivent la
**cible** M0→M6 ; le [CHANGELOG](CHANGELOG.md) fait foi sur ce qui est livré.

## Documentation de conception

| Document | Contenu |
|---|---|
| [01 — Vision et périmètre](docs/01-vision-et-perimetre.md) | Cas d'usage, exigences, positionnement vs Augmenta |
| [02 — Architecture](docs/02-architecture.md) | Pipeline, modules, modèle de données, threading |
| [03 — Tracking et fusion](docs/03-tracking-et-fusion.md) | Algorithmes : clustering, association, persistance des IDs, fusion multi-capteurs |
| [04 — Calibration](docs/04-calibration.md) | Calibration manuelle, auto-calibration, détection de dérive |
| [05 — Interface](docs/05-interface.md) | UI web : écrans, design system, flux temps réel |
| [06 — Protocoles et intégrations](docs/06-protocoles-et-integrations.md) | OSC Augmenta, WebSocket, TUIO, API REST |
| [07 — Roadmap](docs/07-roadmap.md) | Jalons M0→M6 avec critères d'acceptation |
| [08 — Ingénierie](docs/08-ingenierie.md) | Stack, CI/CD, tests, conventions, packaging |
| [09 — Outils de maintenance et debug](docs/09-outils-maintenance-debug.md) | Profilage, replay, diagnostic, injection de pannes |
| [10 — Guide de démarrage](docs/10-guide-demarrage.md) | Compiler, lancer, brancher un LiDAR, outils d'évaluation |
| [ADRs](docs/adr/) | Décisions d'architecture actées |

> **Note** : `docs/01` à `docs/09` sont des **documents de conception** décrivant la cible
> M0→M6. Ils ne décrivent pas l'état livré. Pour ce qui existe réellement :
> [CHANGELOG](CHANGELOG.md) et [guide de démarrage](docs/10-guide-demarrage.md).

## Organisation du dépôt

```
sillage/
├── engine/
│   ├── src/           # Cœur C++20 : pipeline, tracking, drivers, sorties, serveur HTTP/WS
│   │   ├── drivers/   # Hokuyo (SCIP 2.2), SICK (CoLa A), pont UDP universel
│   │   ├── eval/      # Scénarios MOT et métriques (portes de CI)
│   │   └── sim/       # Simulateur de salle et d'agents
│   └── tests/         # 68 tests (unitaires, intégration sockets, scénarios)
├── ui/dev/            # Interface web : un fichier HTML autonome, sans build
├── examples/          # Fichier projet d'exemple
├── tools/bridges/     # Ponts capteurs (RPLIDAR → UDP)
├── packaging/         # Installeur Inno Setup, service Windows, systemd, launchd, portable
└── docs/              # Conception (01–09) + guide de démarrage (10)
```
