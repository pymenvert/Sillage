# Changelog

## v1.0.1 — 2026-07-12 — Fiabilité et durcissement

Passe de robustesse issue d'une revue de code approfondie. Aucun changement
fonctionnel : mise à jour recommandée pour toute installation.

- **Serveur HTTP/WebSocket** : gestion des connexions revue en profondeur (isolation par
  connexion, délais d'attente, files d'envoi bornées, recyclage des ressources). Stabilité
  validée par test d'endurance : 1500 cycles de connexion/déconnexion, aucune fuite.
- **Robustesse des entrées** : validation renforcée sur toutes les entrées externes
  (configuration, réseau, fichiers).
- **Fiabilité disque** : les écritures de configuration et d'enregistrement détectent les
  erreurs (disque plein) sans jamais corrompre un fichier existant.
- **Interface** : affichage durci, résilience aux messages malformés, gardes de
  chargement. Polish visuel (favicon, dégradé de fond, rendu des tracks, typographie).
- **Moteur** : gardes défensives sur les paramètres limites de configuration.
- 9 tests de régression ajoutés (65 au total).

## v1.0.0 — 2026-07-12

Première version complète, multiplateforme (Windows 10/11, Ubuntu 22.04+, macOS 13+).

### Tracking
- Pipeline multi-capteurs : fond appris par capteur, fusion au niveau des points,
  clustering avec splitting guidé par prédictions, mesures partagées en superposition.
- Tracker anti-croisement : Kalman + assignation optimale hiérarchisée, deux passes
  (clusters faibles), gate adaptatif, coasting, tombes de ré-identification.
- Validé par 7 scénarios MOT en portes de CI : croisements, demi-tours au contact,
  côte-à-côte 45 cm, occultation pilier, panne capteur — **0 échange d'ID** (IDF1 ≥ 0,999) ;
  stress 50 personnes / 3 capteurs à ~144 µs/tick.

### Capteurs
- Hokuyo URG/UST (SCIP 2.2), SICK TiM (CoLa A), **pont UDP universel** (tout capteur via
  datagrammes JSON — script RPLIDAR fourni), simulateur scénarisable, replay.
- Santé par capteur, reconnexion automatique, types mélangeables.

### Sorties
- OSC compatible Augmenta, TUIO 1.1, ADM-OSC (audio objet), événements de zones OSC,
  prédiction compensatrice de latence + lissage One-Euro par configuration.

### Configuration & interface
- Fichier projet JSON versionné (sauvegarde atomique, migrations), API REST GET/POST
  avec application à chaud (zones, sorties, conditionnement).
- UI web embarquée : scène live (points, trails, zones, heatmap, cônes capteurs),
  éditeur de zones à la souris, panneau de réglages, mode présentation, panneaux
  santé/personnes/événements, sparkline de charge.

### Robustesse & outillage
- Enregistrement/replay déterministe (.srec), `--eval` (métriques MOT + verdicts),
  `--debug-scenario` (traces), 56 tests dont intégrations sur vraies sockets.
- Portable ~330 Ko (zip/tgz), installeur Windows un fichier (service optionnel),
  unités systemd/launchd/service Windows, CI 3 OS + release automatique sur tag.

### Limitations connues (assumées)
- Drivers Hokuyo/SICK validés contre capteurs simulés conformes — la validation sur
  matériel physique est le premier point de la feuille de route post-1.0.
- Foule dense vue de 2 capteurs seulement (`group_8_random`) : ~17 échanges d'ID sur
  2 min — scénario en quarantaine CI, chantier documenté (un 3e capteur résout).
- Calibration automatique par marche : solveur validé en simulation, workflow UI à venir.
- PSN (PosiStageNet) et perception 3D native : prévus post-1.0.
