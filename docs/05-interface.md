# 05 — Interface de configuration

Objectif : une UI **belle, moderne et efficace** — le premier contact avec le produit, et
l'outil de travail de l'installateur sur site. Web embarquée ([ADR-002](adr/002-ui-web-embarquee.md)) :
accessible sur `http://<machine>:8080` depuis n'importe quel navigateur, y compris une
tablette pendant qu'on règle les capteurs dans la salle.

## Stack

- **React 18 + TypeScript + Vite**, état : Zustand.
- **Rendu scène : WebGL2 en base, WebGPU en chemin progressif** — les nuages de points
  temps réel (10⁴–10⁵ pts) exigent le GPU ; le reste de l'UI est du DOM classique.
  WebGPU est désormais livré par défaut dans Chrome, Edge, Safari et Firefox (2025+), mais
  Linux reste en cours de déploiement → WebGL2 reste la base requise, même rendu visuel.
- Tailwind CSS + Radix UI (primitives accessibles), animations Framer Motion.
- Typo : Inter (UI) + JetBrains Mono (valeurs numériques, logs).
- Flux temps réel : WebSocket, canal `points` en **binaire** (Float32, décimation adaptative
  côté engine pour tenir < 2 Mo/s), canaux `tracks`/`events`/`health` en JSON.
- Types partagés générés depuis les schémas JSON de `protocol/` — l'UI et l'engine ne
  peuvent pas diverger silencieusement.

## Design system

- **Thème sombre par défaut** (usage en salle sombre — indispensable), thème clair dispo.
- Fond quasi-noir bleuté, panneaux en verre dépoli discret, une couleur d'accent
  (cyan/ambre) réservée aux éléments interactifs et à l'état « live ».
- Chaque capteur a sa couleur (palette qualitative stable) réutilisée partout : points,
  cônes FOV, santé, légendes.
- Tracks : trail dégradé (comète) + ID + vecteur vitesse. C'est l'image signature du
  produit — elle doit être superbe en démo plein écran (mode « presentation » sans chrome).

## Écrans

### 1. Scène (écran principal)

Vue du dessus GPU de la salle, tout en un :

- points live colorés par capteur, clusters, tracks avec trails et IDs, zones, masques ;
- pan/zoom fluide, grille métrique, outil mesure ;
- sélection d'un capteur → panneau latéral (pose, santé, fond, masques) ; drag & rotate
  directement dans la vue avec snapping ;
- calques activables : fond appris, carte de couverture, heatmap, résidus de calibration ;
- barre d'état : tick pipeline, latence, tracks actifs, état des sorties.

### 2. Assistant d'installation (wizard)

Premier lancement : dessiner la salle (dimensions ou import DXF/SVG simple) → découverte
automatique des capteurs (série + réseau) → positionnement approximatif → apprentissage du
fond (salle vide, 10 s) → test « marchez devant le capteur » → sortie par défaut activée.
**Objectif : premier tracking en < 10 minutes.**

### 3. Calibration

Workspace dédié (voir [04](04-calibration.md)) : mode marcheur guidé avec checklist des
recouvrements, résidus par paire en cm, score global, bouton « appliquer / recommencer ».

### 4. Tracking (réglages)

Paramètres du tracker groupés par effet observable (« sensibilité de détection », « tenue
des IDs », « réactivité vs douceur ») avec **effet visible en live** sur la scène et
**presets** : petite salle dense / hall / scène-performers / extérieur. Mode expert pour
les paramètres bruts, avec les valeurs par défaut qui vont bien.

### 5. Zones

Éditeur de polygones (dessin au clic, édition de sommets, duplication), comportements par
zone (enter/exit/dwell, capacité, ligne de comptage orientée), compteurs live.

### 6. Sorties

Une carte par sortie (OSC, WebSocket, TUIO, MQTT…) : état, destination, protocole, filtre
de zone, prédiction/lissage, **moniteur de messages live** (les N derniers messages envoyés,
formatés) — l'outil n°1 pour déboguer « pourquoi TouchDesigner ne reçoit rien ».

### 7. Santé (dashboard)

FPS et latence par capteur, budget latence bout-en-bout par étage, CPU/mémoire, historique
des alertes (capteur déconnecté, dérive de calibration, saturation), accès aux logs,
export d'un **rapport de diagnostic** (zip : logs + config + boîte noire) pour le support.

### 8. Enregistrements

Liste des enregistrements, lecture avec timeline (pause, vitesse, scrubbing), enregistrement
manuel + boîte noire en anneau, export/import de sessions.

## Analytique (v1 léger, extensible)

Heatmap d'occupation cumulée, comptages par zone/ligne avec historiques, export CSV.
(Dashboards riches et rétention longue = post-v1, voir roadmap.)
