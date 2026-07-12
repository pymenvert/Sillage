# 01 — Vision et périmètre

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
2. **Scénographie / spectacle** — un performer déclenche lumières/son/vidéo. Contraintes :
   fiabilité absolue pendant le show, mode dégradé si un capteur tombe.
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
| Ouster OS0/OS1 (post-v1) | 3D Ethernet | 50–120 m | 10–20 Hz | ~6 000 €+ | grandes salles, hauteur |

Achat minimum recommandé pour développer sérieusement : **2 × RPLIDAR** (pour travailler la
fusion dès le début) puis **1 × Hokuyo UST** (pour valider le driver pro et la fréquence 40 Hz).
Montage horizontal entre la cheville et la taille selon la salle (voir [04](04-calibration.md)).
