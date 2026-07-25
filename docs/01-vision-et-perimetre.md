# 01 — Vision et périmètre

> ⚠️ **Document de conception.** Il décrit la cible des jalons M0→M6, **pas**
> l'état livré en v1.0.1. Pour ce qui existe réellement : [CHANGELOG](../CHANGELOG.md)
> et [guide de démarrage](10-guide-demarrage.md).

## Le produit

Un logiciel de tracking de personnes pour espaces physiques (salles immersives, musées,
scénographies, retail, événementiel) basé sur des LiDARs. Il transforme un ou plusieurs
capteurs en un flux temps réel de **personnes identifiées** : `id, position, vitesse,
orientation, taille, trajectoire, zone`.

Le modèle de référence est **Augmenta** (https://augmenta.tech) : nodes de détection LiDAR,
fusion multi-capteurs, sorties OSC/WebSocket vers l'écosystème créatif. Sillage vise le même
périmètre fonctionnel en logiciel pur (bring your own LiDAR), multiplateforme Windows/Ubuntu,
avec plusieurs différenciateurs (voir plus bas).

## Cas d'usage cibles

1. **Salle immersive / art numérique** — le contenu projeté réagit à la position des visiteurs.
   Contraintes : latence faible (< 1 frame de contenu), IDs stables (un effet suit *une*
   personne), pénombre (le LiDAR s'en moque, avantage vs caméras).
2. **Scénographie / spectacle** — un performer déclenche lumières/son/vidéo, poursuite
   lumière automatique (via PSN vers les consoles), son objet qui suit le performer
   (via ADM-OSC vers L-ISA / SPAT / d&b Soundscape). Contraintes : fiabilité absolue
   pendant le show, mode dégradé si un capteur tombe.
3. **Musée / exposition** — zones interactives, comptage, analytique de parcours.
4. **Retail / événementiel** — heatmaps, files d'attente, comptage entrées/sorties.

## Exigences produit

| Exigence | Cible v1 |
|---|---|
| Nombre de personnes trackées simultanément | ≥ 30 (objectif 100 en charge dégradée gracieuse) |
| Capteurs par salle | 1 à 8 LiDARs 2D (3D en post-v1) |
| Surface couverte | jusqu'à ~30 × 30 m (limité par la portée capteur) |
| Fréquence de sortie | tick pipeline 60 Hz (configurable 30–120) |
| Latence de traitement (hors capteur) | < 10 ms sur machine cible |
| Latence bout-en-bout | latence capteur (25–100 ms selon modèle) + traitement, avec **compensation prédictive** configurable |
| Précision position (après calibration) | ± 5 cm dans les zones bien couvertes |
| Persistance des IDs | 0 échange d'ID sur les scénarios de croisement standard multi-capteurs ; fenêtre de ré-identification configurable après perte |
| Robustesse | fonctionnement 24/7, reconnexion capteur automatique, service système avec watchdog |
| Plateformes | Windows 10/11, Ubuntu 22.04+, macOS 13+ (Apple Silicon prioritaire, Intel supporté) — mêmes fonctionnalités partout |
| Vie privée | aucune image, aucune donnée biométrique — anonyme par conception (argument RGPD) |

## Ce que fait Augmenta (état de l'art à égaler)

D'après la [doc officielle](https://docs.augmenta.tech) et les
[dépôts GitHub](https://github.com/Augmenta-tech) :

- **Workflow** : installation physique → configuration des *nodes* (masques, détection de
  fond automatique/manuelle/adaptative, paramètres de détection et de tracking) →
  configuration de la *fusion* (scène en mètres et pixels, nommage des nodes) →
  calibration de la fusion (visuelle et non visuelle) → vérifications finales (traversées
  en zigzag, zones de recouvrement, vitesses réalistes, détection d'enfants).
- **Données par personne** : `frame, id, oid, age, centroid, velocity, orientation,
  boundingRect (pos/size/rotation), height`.
- **Sorties** : OSC (protocole Augmenta v1/v2), WebSocket (SDKs C++/C#/Unity),
  plugins TouchDesigner, Unity, Unreal, notch, vvvv.
- **Points forts revendiqués** : prédiction pour compenser la latence, calibration rapide,
  architecture distribuée scalable, capteurs indoor/outdoor (IP67).

## Différenciateurs de Sillage

1. **Logiciel pur, matériel ouvert** — Augmenta vend du hardware dédié ; Sillage supporte des
   LiDARs du commerce (RPLIDAR ~400 €, Hokuyo, SICK) sur un PC standard.
2. **Compatibilité protocole Augmenta** — les sorties OSC/WebSocket parlent le protocole
   Augmenta : tous les plugins existants (TouchDesigner, Unity, Unreal…) fonctionnent
   sans modification. Adoption immédiate. (Voir [ADR-004](adr/004-compatibilite-protocole-augmenta.md).)
3. **Tracker validé en continu** — simulateur de scénarios avec vérité terrain + métriques
   MOT (IDF1, échanges d'ID) comme portes de CI. La robustesse aux croisements n'est pas une
   promesse, c'est un test qui passe. (Voir [03](03-tracking-et-fusion.md#validation).)
4. **Boîte noire replay** — tout incident sur site est rejouable à l'identique au bureau.
5. **Détection de dérive de calibration** — un capteur bousculé est détecté et signalé
   (écart persistant entre fond appris et fond observé), au lieu de dégrader silencieusement
   le tracking.
6. **UI web embarquée** — configuration depuis une tablette sur site, aucun logiciel à
   installer côté client, même UI sur Windows et Ubuntu.

## Paysage concurrentiel (au-delà d'Augmenta)

Le tracking de personnes par LiDAR 3D existe côté industrie/smart-city :
[Outsight](https://www.outsight.ai/) (flux de personnes dans les aéroports),
[Seoul Robotics SENSR](https://seoulrobotics.tech/) (précision annoncée ~4 cm),
[Blickfeld Percept](https://www.blickfeld.com/lidar-software/) (analytique de foule embarquée
sur capteur). Tous visent l'industriel : tarification enterprise, intégrations BI/sécurité,
aucun ne parle OSC ni ne s'intègre à l'écosystème créatif. **La niche
immersif/scénographie/événementiel, avec du matériel ouvert et la compatibilité Augmenta,
est libre** — et ces acteurs valident la pertinence technique du LiDAR 3D pour la foule.

Côté spectacle vivant, [Naostage](https://www.naostage.com/) (K SYSTEM) est la référence
« beaconless » : capteur KAPTA multi-modal (1 caméra visible + 2 proche-IR + 2 thermiques
+ projecteurs IR, monté à ~10 m, ~20×15 m couverts), serveur IA KORE, 16–64 cibles,
précision centimétrique, ré-identification visuelle des performers, module UWB BEAKON en
complément hors ligne de vue. Enseignements pour Sillage : (a) leur module UWB valide
notre ancrage d'identité par balise (M6) ; (b) leur marché parle **PSN** et **ADM-OSC**
— intégrés à notre M4 ; (c) leur approche caméras+IA capture l'apparence (ré-ID
visuelle possible mais données d'images, RGPD plus lourd, nuit totale gérée par IR
embarqué) là où le LiDAR reste anonyme par nature et insensible à la lumière — les deux
approches sont complémentaires, pas identiques. Sillage ne cherche pas à cloner KAPTA :
il apporte le tracking anonyme, ouvert et abordable, avec les mêmes protocoles de sortie
vers les mêmes consoles.

## Technologies capteurs évaluées

- **LiDAR 2D** (choix v1) : le meilleur ratio précision/coût/simplicité pour une salle ; tout
  le pipeline v1 est construit dessus.
- **LiDAR 3D compact nouvelle génération** (intégration progressive dès M4) : la génération
  2024–2026 change la donne — [Livox Mid-360](https://www.livoxtech.com/mid-360)
  (360°×59°, 70 m, 200 k pts/s, ~750 €), [Hesai JT16](https://www.hesaitech.com/product/jt16/)
  (360°×40°, ~650 €), [Unitree L2](https://www.unitree.com/L2/) (360°×96°, ~400 €). Prix d'un
  LiDAR 2D milieu de gamme, mais : hauteur par personne (ré-identification bien plus fiable),
  montage au plafond possible (moins d'occultations), immunité aux interférences multi-capteurs.
  Stratégie : le pipeline v1 consomme leurs nuages par **tranches de hauteur** (aucun changement
  d'architecture), la perception 3D complète vient en M6.
- **Radar mmWave** (TI IWR6843 et similaires, ~50–150 €) : détecte les personnes **immobiles**
  (micro-mouvements respiratoires) là où le fond LiDAR peut les absorber, insensible
  poussière/fumée (effets scéniques !). Nuage trop épars pour du tracking d'ID précis →
  évalué comme **capteur complémentaire de présence** (post-v1), pas comme capteur principal.
- **Caméras profondeur/stéréo** (Orbbec Femto, ZED) : riches mais FOV étroit, interférences
  entre unités, et réintroduisent l'image (RGPD) → hors périmètre.
- **Balises UWB portées** : précis et ID garanti mais nécessite un tag par personne → noté en
  M6 comme *ancrage d'identité* optionnel pour performers (fusion balise + track LiDAR : l'ID
  d'un danseur ne se perd jamais).

## Hors périmètre v1 (assumé)

- LiDARs 3D (Ouster, Livox, Hesai) → M6. L'architecture les prévoit (interface driver à
  nuages de points 3D, le pipeline 2D projette).
- Squelette / pose / gestes (nécessite caméras de profondeur — autre produit).
- Multi-salles fédérées et architecture distribuée multi-nodes → post-v1, mais le découpage
  des modules la prépare (voir [02](02-architecture.md#évolution-distribuée)).
- Certification / marquage pour usage sécurité des personnes (jamais : nous ne sommes pas
  un système de sécurité).

## Matériel de référence pour le développement

| Capteur | Type | Portée | Fréquence | Prix indicatif | Usage |
|---|---|---|---|---|---|
| Slamtec RPLIDAR C1 | 2D 360° série | 12 m | 10 Hz | ~100 € | démarrage dev |
| Slamtec RPLIDAR S3 | 2D 360° série | 40 m | 20 Hz | ~600 € | dev + petites salles |
| Hokuyo UST-10LX / 20LX | 2D 270° Ethernet | 10/20 m | 40 Hz | ~1 500–2 500 € | standard pro |
| SICK TiM781 | 2D 270° Ethernet | 25 m | 15 Hz | ~2 000 € | standard pro |
| Livox Mid-360 (dès M4) | 3D 360°×59° Ethernet | 70 m | 200 k pts/s | ~750 € | plafond, hauteur, grandes salles |
| Hesai JT16 (M6) | 3D 360°×40° Ethernet | 30–100 m | 48 k pts/s | ~650 € | alternative compacte |
| Unitree L2 (M6) | 3D 360°×96° Ethernet | 30 m | 64 k pts/s | ~400 € | 3D d'entrée de gamme |
| Ouster OS0/OS1 (M6) | 3D Ethernet | 50–120 m | 10–20 Hz | ~6 000 €+ | très grandes salles |

Achat minimum recommandé pour développer sérieusement : **2 × RPLIDAR** (pour travailler la
fusion dès le début), **1 × Livox Mid-360** (pour préparer la tranche 3D dès M4 et enregistrer
des datasets), puis **1 × Hokuyo UST** (pour valider le driver pro et la fréquence 40 Hz).
Montage horizontal entre la cheville et la taille selon la salle (voir [04](04-calibration.md)).
