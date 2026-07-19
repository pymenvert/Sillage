# Changelog

## v1.0.1 — 2026-07-12 — Durcissement pré-commercial

Passe de robustesse guidée par une revue de code adversariale multi-agents (6 dimensions,
chaque constat contre-vérifié). 21 défauts confirmés corrigés, 9 tests de régression
ajoutés (65 tests au total). Aucun changement fonctionnel.

### Sécurité & réseau (serveur HTTP/WebSocket réécrit)
- **Path traversal corrigé**, y compris le cas Windows spécifique (`/C:/…`, backslash,
  deux-points) : les fichiers hors du dossier racine ne sont plus servis (défense en
  profondeur : normalisation + vérification de confinement). Testé.
- **Slowloris / déni de service éliminé** : chaque connexion a un timeout de réception et
  son propre thread — un client muet ne peut plus figer le plan de contrôle ni bloquer
  l'arrêt du moteur. Testé.
- **Course use-after-close supprimée** : `broadcast()` (thread temps réel) n'effectue plus
  aucune opération socket ; il empile dans une file par client, seul le thread propriétaire
  ferme son socket. Files bornées (drop-oldest), threads recyclés, plafond de clients.
  Validé par soak : 1500 cycles connexion/déconnexion, 0 fuite.
- Crash sur ligne de requête vide corrigé (400 au lieu de `std::terminate`).
- Connexion driver bornée (plus d'attente du timeout OS complet à l'arrêt).

### Robustesse entrée hostile & I/O
- **Parseur JSON** : garde de profondeur (64 niveaux) contre le débordement de pile.
- **Adresses OSC** : noms de zones assainis (grammaire OSC 1.0) avant envoi.
- **Broadcast WS** : noms de zones échappés en JSON (un guillemet ne casse plus le flux).
- **Sauvegarde config** : vérification de l'écriture avant le rename (disque plein ne
  remplace plus le bon fichier par un tronqué).
- **Enregistrement** : retours de `fwrite` vérifiés (disque plein arrête proprement).
- `restartRequired` détecte désormais toute édition de capteur (pas seulement le nombre).
- Gardes défensives : bins de fond, distance de liaison, `dt` et clusters vides ne peuvent
  plus produire de division par zéro ni de NaN, même sur config éditée à la main.

### Interface
- **XSS corrigé** : toutes les chaînes issues de la config/API sont échappées avant
  affichage. Validé en réel (payload `<img onerror>` rendu en texte inerte).
- Un message WebSocket malformé ne peut plus vider l'affichage (dernière image conservée).
- Gardes contre l'usage de la config avant chargement ; resynchronisation après POST.
- Polish : favicon, fond en dégradé radial, cœurs de tracks pleins, typo affinée.

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
