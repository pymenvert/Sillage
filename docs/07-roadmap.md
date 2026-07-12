# 07 — Roadmap

Principe : **squelette marchant d'abord** — dès M0, la chaîne complète capteur→sortie
existe de bout en bout, et chaque jalon l'épaissit. On peut faire une démo à la fin de
chaque jalon. Les durées supposent un développeur à temps plein assisté ; elles se lisent
comme des rapports d'effort plus que des dates.

## M0 — Fondations (≈ 1–2 semaines)

- Monorepo, CMake + vcpkg + presets (MSVC, GCC, Clang/AppleClang), CI GitHub Actions
  **verte sur Windows, Ubuntu et macOS dès le premier jour** (build + tests + lint).
- Squelette marchant : driver **simulateur** → clustering naïf → tracker minimal (plus
  proche voisin) → sortie OSC + page web affichant les points et tracks en WebGL.
- Format d'enregistrement + driver replay (les tests en dépendent).

**Acceptance** : cloner, `cmake --build`, ouvrir le navigateur, voir des agents simulés
trackés, recevoir l'OSC dans Protokol/Chataigne. Sur les trois OS.

## M1 — Un LiDAR, un tracker solide (≈ 3–4 semaines)

- Drivers réels : **RPLIDAR** (série) puis **Hokuyo SCIP 2.2** (Ethernet). Découverte auto,
  reconnexion, santé.
- Prétraitement complet : fond appris/adaptatif/manuel, masques, contour de salle.
- Tracker complet ([03](03-tracking-et-fusion.md)) : Kalman, Hungarian + gating,
  probation/coasting, splitting guidé par prédictions, test anti-échange, tombes de ré-ID.
- **Simulateur de scénarios + métriques MOT (IDF1, ID switches) en CI** — le harnais est
  dans ce jalon, pas après : tout le réglage du tracker se fait métriques à l'appui.

**Acceptance** : avec 1 LiDAR réel, scénarios croisement/occultation du
[03 §10](03-tracking-et-fusion.md#validation) au niveau mono-capteur ; suite simulateur
verte en CI ; latence de traitement < 10 ms mesurée.

## M2 — Multi-LiDAR : fusion et calibration (≈ 3–4 semaines)

- Fusion au niveau des points, compensation temporelle inter-capteurs.
- Calibration manuelle (drag + snapping RANSAC murs) et **auto-calibration en marchant**
  (paires + graphe de poses), résidus affichés.
- Score de santé de calibration, détection de dérive, auto-exclusion optionnelle.
- Scénarios multi-capteurs en CI (croisement derrière occultation, panne d'un capteur).

**Acceptance** : 2 LiDARs réels calibrés en < 5 min, erreur < 5 cm dans le recouvrement ;
croisement avec occultation totale côté capteur A → 0 échange d'ID ; capteur débranché à
chaud → dégradation propre + alerte.

## M3 — L'interface complète (≈ 3–4 semaines)

- Tous les écrans du [05](05-interface.md) : scène interactive finale, wizard, zones,
  réglages avec presets, dashboard santé, enregistrements.
- Config hot-apply, autosave, migrations de schéma.
- Design poli : c'est le jalon « sexy » — revue design dédiée, mode présentation.

**Acceptance** : une personne qui n'a jamais vu le produit installe 2 capteurs et obtient
un tracking calibré en < 30 min, UI seule, doc non ouverte.

## M4 — Écosystème (≈ 2–3 semaines)

- OSC Augmenta v1/V2 + WebSocket Augmenta **validés contre les plugins officiels**
  TouchDesigner / Unity / Unreal (harnais de conformité).
- TUIO, OSC générique, MQTT/webhooks, export CSV/JSONL.
- Prédiction (compensation de latence) et lissage One-Euro par sortie.
- **Driver Livox Mid-360 en mode « tranche 2D »** : le nuage 3D est découpé en bande de
  hauteur configurable et alimente le pipeline existant tel quel — premier capteur 3D
  supporté sans changer l'architecture, et la hauteur par personne enrichit la ré-ID.

**Acceptance** : la scène d'exemple TouchDesigner d'Augmenta fonctionne avec Sillage sans
modification ; démos Unity et Unreal enregistrées ; un Mid-360 au plafond tracke la salle
de test.

## M5 — Durcissement et distribution (≈ 2–3 semaines)

- Service systemd + Windows Service, watchdog, crash handler + rapport de diagnostic.
- **Soak test 48 h** : 2 capteurs + simulateur de charge, zéro fuite (RSS plat), zéro
  déconnexion non récupérée — c'est un test de CI nightly désormais.
- Installeurs : `.deb` (+ dépôt apt à terme), installeur Windows (Inno Setup), `.pkg`
  macOS universel (arm64 + x86_64, launchd ; signature/notarisation Apple dès qu'un compte
  développeur existe — les builds non signés restent utilisables via clic droit > Ouvrir).
  Versionnage semver + notes de release automatisées.
- Doc utilisateur (installation, calibration, intégrations) — site mkdocs.

**Acceptance** : installation sur machine vierge (les trois OS) en < 10 min ; survit à
48 h de soak ; **v1.0 taggée**.

## M6 — Au-delà (backlog priorisé, post-v1)

1. **Perception 3D complète** (Livox, Hesai JT16, Unitree L2, Ouster) : au-delà de la
   tranche 2D de M4 — clustering 3D, hauteur/posture, montage plafond, tranches multiples.
2. **Analytique** : heatmaps historisées, parcours, funnels de zones, rétention SQLite/Parquet.
3. **Architecture distribuée** `sillage-node` → engine central (grands sites), sync PTP.
4. **Ancrage d'identité par balise UWB** (performers) : fusion balise portée + track LiDAR —
   l'ID d'un danseur désigné ne peut jamais se perdre, le public reste tracké sans tag.
5. **Radar mmWave en capteur de présence complémentaire** (personnes immobiles, fumée
   scénique — là où le LiDAR est aveuglé ou le fond adaptatif absorbe).
6. Ré-ID par cadence de pas (expérimental, voir [03 §7](03-tracking-et-fusion.md)).
7. Suivi d'objets non-humains (chariots, robots) par profils de cluster.
8. API de plugins de sortie chargés dynamiquement.

## Risques identifiés et parades

| Risque | Impact | Parade |
|---|---|---|
| Accès limité au matériel pendant le dev | tracker réglé « pour le simulateur » | acheter 2 RPLIDAR dès M1 ; sessions physiques régulières enregistrées → la bibliothèque replay ancre le réel dans la CI |
| Spec du protocole Augmenta incomplète publiquement | compat imparfaite | conformité mesurée contre les SDKs/plugins open source officiels, pas contre la doc |
| Ports série/USB capricieux sous Windows | support pénible | privilégier les capteurs Ethernet en pro ; driver série robuste (timeouts, hotplug) testé tôt |
| Deux personnes réellement inséparables (collées) | attentes irréalistes | documenter la limite physique ; le test « côte à côte 40 cm » borne ce qu'on promet |
| Dérive du périmètre (3D, analytique, distribué trop tôt) | v1 qui ne sort pas | roadmap gelée jusqu'à M5 ; tout le reste va en M6 |
