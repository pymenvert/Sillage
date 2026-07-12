# Sillage — Tracking de personnes multi-LiDAR pour salles immersives

> **Sillage** : la trace qu'on laisse derrière soi. Un moteur de tracking temps réel qui suit
> plusieurs personnes dans un espace physique à l'aide d'un ou plusieurs LiDARs, avec des
> identifiants persistants, et diffuse leurs positions vers les logiciels créatifs
> (TouchDesigner, Unity, Unreal, Madmapper, Resolume…).

**Statut : phase de conception — voir la [roadmap](docs/07-roadmap.md).**

## Ce que fait Sillage

- **Tracking multi-personnes** en temps réel (position, vitesse, taille, trajectoire) avec des
  **IDs stables** qui survivent aux croisements et aux occultations.
- **Fusion multi-LiDAR** : plusieurs capteurs couvrent une même salle, fusionnés au niveau des
  points dans un référentiel commun — les angles morts d'un capteur sont couverts par les autres.
- **Calibration assistée** : positionnement manuel drag & drop + auto-calibration « en marchant ».
- **Sorties standard** : OSC (compatible protocole Augmenta), WebSocket JSON, TUIO, MQTT —
  compatible immédiatement avec l'écosystème créatif existant.
- **Interface web moderne** embarquée : configuration depuis n'importe quel navigateur,
  y compris une tablette sur site.
- **Enregistrement / replay** : rejouer une session capteur à l'identique pour déboguer,
  régler, ou valider une mise à jour.
- **Simulateur intégré** : scénarios synthétiques (croisements, groupes, files) avec vérité
  terrain, utilisés comme tests de non-régression du tracker en CI.
- **Multiplateforme** : Windows 10/11 et Ubuntu 22.04+, service système, fonctionnement headless.

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
| [ADRs](docs/adr/) | Décisions d'architecture actées |

## Organisation du dépôt (cible)

```
sillage/
├── engine/            # Cœur C++20 : acquisition, fusion, tracking, sorties, serveur API
│   ├── src/
│   ├── include/
│   ├── drivers/       # Plugins capteurs (RPLIDAR, Hokuyo, SICK, replay, simulateur…)
│   └── tests/
├── ui/                # Interface web React + TypeScript + WebGL
├── protocol/          # Schémas JSON partagés engine ↔ UI ↔ clients (source de vérité)
├── simulator/         # Scénarios de test et générateur de vérité terrain
├── datasets/          # Enregistrements réels de référence (Git LFS)
├── packaging/         # Installeurs Windows, paquet .deb, unités systemd / service Windows
└── docs/              # Ce dossier
```
