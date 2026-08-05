# Changelog

## Non publié — Prêt pour le spectacle et pour la vente

### Corrections

- **Un capteur plus lent que le tick produit enfin des tracks**. Un SICK TiM tourne à
  15 Hz face au tick 60 Hz : ses détections arrivaient avec 3 ticks de trou, la probation
  du tracker (`tentativeMaxMiss`) tuait chaque piste avant confirmation — une salle
  couverte uniquement de capteurs lents ne trackait **personne** (reproduit en test :
  capteur connecté, 15 Hz, zéro erreur, zéro track). Le moteur re-présente désormais le
  dernier scan de chaque capteur entre deux révolutions, borné à 250 ms : un capteur
  débranché cesse de contribuer dans ce délai au lieu de figer un fantôme. La fusion
  multi-capteurs cesse au passage de scintiller au rythme du capteur le plus lent, et la
  calibration n'est nourrie que des trames fraîches.
- **Replay fidèle à la chronologie enregistrée**. L'enregistreur n'écrit que les ticks
  ayant reçu des trames ; le replay consommait un groupe par tick moteur en jetant le
  numéro de tick enregistré, compressant les trous — une session avec un capteur 15 Hz
  face à un tick 60 Hz se rejouait 4× trop vite, avec des vitesses que le direct n'a
  jamais vues. Les ticks vides du direct se rejouent désormais vides ; les
  enregistrements denses sont inchangés (déterminisme verrouillé par test).
- **Hokuyo : géométrie du balayage corrigée**. La résolution angulaire par défaut était
  celle d'un URG-04LX (2π/1024) alors que la plage de pas décrit un UST-10LX/20LX, ce qui
  étalait 1081 pas sur **379,7° au lieu de 270°**. Deux conséquences sur un vrai capteur —
  le type configuré par défaut : chaque point était placé à un azimut faux, et le balayage
  dépassant le tour complet, les pas des extrémités aliasaient sur les bins de fond de
  l'azimut opposé — les personnes proches des murs latéraux étaient soustraites comme du
  fond. Verrouillé par un test sur l'étendue du balayage.
- **Réapprentissage du fond : course entre le thread HTTP et le tick**.
  `POST /api/background/relearn` réinitialisait les modèles de fond depuis le thread de
  connexion pendant que le tick les lisait. Un réapprentissage partiellement appliqué
  laissait `learning()` à faux avec des bins vides : toute la salle passait en avant-plan
  **définitivement**, le chemin d'apprentissage ne repassant jamais dessus. La demande est
  désormais enregistrée puis consommée en tête du tick suivant, et deux appuis successifs
  ne coûtent qu'une réinitialisation.
- **Arrêt du serveur HTTP/WebSocket sous Linux : blocage définitif**. `stop()` fermait la
  socket d'écoute en comptant sur cette fermeture pour débloquer le thread parqué dans
  `accept()`. Windows et macOS se comportent ainsi, **Linux non** : le thread restait
  bloqué et `stop()` attendait indéfiniment une jonction qui ne pouvait aboutir que si
  quelqu'un se connectait. Le thread d'acceptation attend désormais par intervalles bornés
  et `stop()` le laisse se retirer avant de fermer la socket — ce qui supprime au passage
  la course sur le descripteur entre les deux threads. Conséquence directe : la CI Linux,
  **rouge depuis un mois** (l'étape de test consommait 4 h avant d'être tuée, alors que
  macOS et Windows passaient), repasse au vert ; la suite complète s'exécute en ~5 s.
- **Compilation sous GCC 13 (Ubuntu 24.04)** : `-Werror=maybe-uninitialized` transformait
  un faux positif sur `std::variant` en échec de build, alors que le README annonce
  « Ubuntu 22.04+ ». La CI ne pouvait pas le voir, sa matrice étant épinglée sur 22.04 ;
  elle couvre maintenant les deux LTS supportées.

### Tracking en foule dense (chantier M1, première tranche)
- **Confinement des tracks gelés** (docs/03 §3 étape 4, enfin implémenté) : dans un
  nœud d'occultation mutuelle, les tracks gelés restaient sur leur prédiction à vitesse
  constante — ils se traversaient dans le blob, en ressortaient inversés, ou en
  sortaient là où il n'y a personne et y semaient un **9ᵉ track fantôme dans une salle
  de 8** (soit une cue de zone qui part sur personne). Ils sont désormais confinés à
  l'emprise du blob (position bornée, vitesse d'évasion supprimée — le prior de
  direction vit dans l'ancre de continuité, elle extrapolée par la dernière vitesse
  mesurée). Sur `group_8_random` : échanges d'ID 15→10, IDF1 0,633→**0,759**, faux
  positifs **−78 %**, et exactement 8 identités pour 8 personnes. Les 4 scénarios
  sains restent à 0 échange.
- **Le plafond de quarantaine est resserré sur les nouveaux chiffres** : revenir au
  tracker d'avant fait échouer la CI sur trois portes. Coût assumé et documenté :
  ~0,035 IDF1 sur `stress_50` (50 personnes), dont les portes enregistrent l'échange —
  toute dégradation *supplémentaire* reste bloquante.

### Vers la calibration guidée
- **Panneau de calibration dans l'UI** : collecter (compteurs d'observations par capteur
  en direct, pour guider le marcheur), résoudre (résultats par capteur : position, angle,
  rmse en cm — ou le message d'échec du solveur), appliquer (à chaud, sans redémarrage).
  Vérifié en conditions réelles avec un navigateur piloté (rendu, garde des boutons,
  chemins d'erreur).
- **API de calibration** : `POST /api/calib/start` (collecte pendant qu'une personne
  marche dans les recouvrements), `GET /api/calib/status` (compteurs d'observations par
  capteur), `POST /api/calib/solve` (résolution ancrée sur la pose courante d'un capteur,
  exécutée sur une copie — jamais sur le chemin du tick), `POST /api/calib/apply` (poses
  résolues persistées et appliquées à chaud via le chemin de configuration ordinaire).
  Gardées par le verrou show. Testé de bout en bout en CI : deux drivers UDP réels, pose
  du capteur 2 fausse de 50 cm / 11°, récupérée à < 6 cm par requêtes HTTP, visible sur
  `/api/config` sans redémarrage.
- **Poses de capteurs appliquées à chaud** : modifier la pose d'un capteur (position,
  angle) via `POST /api/config` ne demande plus de redémarrage — le fond appris, stocké
  en polaire par capteur, survit tel quel. C'est le préalable qui rendait tout workflow
  de calibration inutilisable : ajuster une pose se terminait par « redémarrez le
  moteur », donc réapprendre le fond dans une salle qui n'est plus vide. Seul le
  câblage (ajout/retrait/adresse d'un capteur) demande encore un redémarrage.
- **Chaîne de calibration validée en CI sur le moteur réel** : un pipeline démarré avec
  des poses volontairement fausses (50 cm, 11°), nourri au collecteur par l'avant-plan
  fusionné (`SensorPose::toLocal`, inverse exact de la fusion), retrouve la vraie pose
  à < 5 cm / 1,4°. Le solveur n'est plus seulement validé en isolation.
- **Cliquets CI sur les scénarios** : `stress_50` reçoit de vraies portes MOT (mesuré
  IDsw 197 / IDF1 0,546, verrouillé à 240 / 0,50) et `group_8_random` un **plafond de
  quarantaine** (mesuré IDsw 15 / IDF1 0,633, plafond 20 / 0,58) : régresser au-delà
  fait échouer la CI même en quarantaine, là où un skip inconditionnel laissait passer
  n'importe quelle dégradation. `--eval` compte ces régressions dans son code de sortie.

### Spectacle vivant
- **Dégradation par capteur** : chaque LiDAR apprend son fond indépendamment. Un capteur
  absent ou en panne réduit la couverture au lieu d'arrêter tout le moteur.
- **Réapprentissage du fond** à la demande (bouton + `POST /api/background/relearn`) —
  indispensable quand le moteur a démarré avec du public déjà installé.
- **Verrou show** : configuration en lecture seule pendant la représentation.
- **Coupure d'urgence des sorties** : le tracking et l'écran continuent, plus rien n'est émis.
- **Bandeau d'alarme** nommant la cause réelle (capteur hors ligne, sorties coupées, verrou),
  lisible dans une salle sombre.
- Régulation du tick : un dépassement ne fait plus s'emballer le moteur.

### Exploitation
- **Service Windows réel** : le moteur expose un point d'entrée SCM (`--service`). Le script
  d'installation vérifie que le service démarre *et* répond, sinon il échoue explicitement.
- **macOS universel** (Apple Silicon + Intel, cible 13.0), sommes SHA256 publiées avec chaque
  release.

### Documentation
- README et docs alignés sur ce qui est **réellement livré**, avec une section explicite
  « prévu, non livré » ; les documents de conception portent un bandeau sans ambiguïté.

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
