# 02 — Architecture

## Vue d'ensemble

Un seul exécutable, `sillage-engine`, tourne en service (systemd sur Ubuntu, Windows Service)
ou en session interactive. Il embarque le pipeline temps réel **et** le serveur HTTP/WebSocket
qui sert l'UI web et l'API. Zéro dépendance d'installation côté client : on ouvre un navigateur.

```
                 ┌──────────────────────────── sillage-engine ────────────────────────────┐
 LiDAR 1 ──────▶ │ Driver ─┐                                                              │
 LiDAR 2 ──────▶ │ Driver ─┤   Prétraitement      Fusion        Détection     Tracking    │
 LiDAR n ──────▶ │ Driver ─┼─▶ (fond, masques, ─▶ (points en ─▶ (clustering ─▶ (Kalman,   │
 Replay ───────▶ │ Driver ─┤    repère salle)      repère        conscient     Hungarian,  │
 Simulateur ───▶ │ Driver ─┘                      commun)       des tracks)   ré-ID)      │
                 │                                                    │                    │
                 │                                             Zones & événements          │
                 │                                                    │                    │
                 │        ┌───────────────┬───────────────┬──────────┴───────┐            │
                 │        ▼               ▼               ▼                  ▼            │
                 │   OSC Augmenta    WebSocket JSON     TUIO            MQTT/Webhooks     │
                 │                                                                         │
                 │   Serveur HTTP+WS ◀── UI web (React, servie statiquement)               │
                 │   Enregistreur/Replay ── Config (JSON versionné) ── Logs/Métriques      │
                 └─────────────────────────────────────────────────────────────────────────┘
```

Langage cœur : **C++20** ([ADR-001](adr/001-langage-coeur-cpp.md)).
UI : **web embarquée** ([ADR-002](adr/002-ui-web-embarquee.md)).
Fusion : **au niveau des points**, pas des tracks ([ADR-003](adr/003-fusion-niveau-points.md)).

## Modules

### `drivers/` — acquisition capteurs

Interface plugin unique :

```cpp
struct ScanFrame {
    SensorId     sensor;
    TimePoint    t_capture;      // horloge monotone locale, corrigée de la latence driver
    TimePoint    t_arrival;
    std::span<const RangePoint> points;  // polaire capteur : angle, distance, intensité
    ScanQuality  quality;        // points invalides, taux de retour, etc.
};

class ISensorDriver {
public:
    virtual Expected<void> connect(const SensorConfig&) = 0;
    virtual void poll(ScanSink&) = 0;          // push des ScanFrame complets
    virtual SensorHealth health() const = 0;   // connecté, fps réel, erreurs, température si dispo
    virtual ~ISensorDriver() = default;
};
```

Drivers v1 :

| Driver | Transport | SDK / protocole | Licence SDK |
|---|---|---|---|
| RPLIDAR (A/C/S) | série USB, Ethernet (S) | Slamtec `rplidar_sdk` | BSD-2 |
| Hokuyo URG/UST | Ethernet, USB | protocole **SCIP 2.2** implémenté en interne (simple, documenté) ou `urg_library` (BSD) | — |
| SICK TiM 5xx/7xx | Ethernet | protocole **CoLa A/B** implémenté en interne | — |
| Replay | fichier | format d'enregistrement Sillage | — |
| Simulateur | in-process | générateur de scénarios (voir [08](08-ingenierie.md#simulateur)) | — |

Post-v1 : Ouster (`ouster_client`, Apache-2), Livox (SDK2, MIT), Hesai (SDK 2.0 — celui
qu'Augmenta a forké). L'interface prévoit dès la v1 un `PointCloudFrame` 3D dont la
projection 2D (tranche de hauteur) alimente le même pipeline.

Chaque driver tourne sur **son propre thread**, pousse dans une file lock-free vers le
pipeline. Reconnexion automatique avec backoff, état de santé publié en continu.

**Découverte automatique** : scan des ports série (VID/PID connus) + scan réseau ciblé
(ports UDP/TCP standard des capteurs) pour proposer les capteurs détectés dans l'UI.

### `scene/` — modèle de la salle

- **Repère salle** : origine configurable, X vers la droite, Y vers le fond, mètres, angles
  en radians. C'est LE référentiel de tout ce qui suit (une seule convention, documentée).
- **Pose capteur 2D** : `(x, y, θ)` + hauteur de montage (métadonnée). 3D : 6 DoF.
- **Masques** : polygones d'exclusion par capteur (reflets, vitres) et globaux (mobilier).
- **Zones** : polygones nommés avec comportements (voir plus bas).
- **Contour de salle** : polygone limite — tout point hors contour est ignoré.

### `pipeline/` — prétraitement et fusion

Cadencé par un **tick à fréquence fixe** (60 Hz par défaut). À chaque tick :

1. Récupérer les derniers `ScanFrame` de chaque capteur (les capteurs ont des fréquences
   différentes : 10–40 Hz ; on consomme le plus récent + interpolation temporelle des
   mesures vers l'instant du tick via la vitesse estimée des tracks).
2. **Filtre de fond par capteur** : modèle d'occupation par secteur angulaire
   (min-range appris avec marge), trois modes comme Augmenta : appris fixe, adaptatif lent
   (absorbe un meuble déplacé en ~minutes, jamais une personne immobile — constante de
   temps >> temps d'immobilité humain), manuel. Le fond appris est persisté avec la config.
3. Application des masques, du contour de salle, filtres de portée min/max, retrait des
   points isolés (bruit).
4. Transformation des points *avant-plan* dans le repère salle → **nuage fusionné** annoté
   (capteur d'origine, timestamp, intensité).

Le tick fixe rend le pipeline **déterministe en replay** : mêmes entrées ⇒ mêmes sorties,
condition nécessaire aux tests de régression.

### `detect/` — clustering

Sur le nuage fusionné 2D : clustering euclidien par grille de hachage (voisinage ~25–45 cm,
adapté au diamètre d'un torse/jambes). Sorties par cluster : centroïde, emprise (ellipse),
nombre de points, **liste des capteurs contributeurs** (sert au diagnostic de couverture).

Particularité clé : le clustering est **conscient des tracks** — voir
[03 — Tracking et fusion](03-tracking-et-fusion.md#clustering-conscient-des-tracks) pour la
gestion des clusters fusionnés lors des croisements.

### `tracking/` — le cœur

Détaillé dans [03 — Tracking et fusion](03-tracking-et-fusion.md). Contrat de sortie :

```cpp
struct Track {
    uint32_t   id;          // stable, jamais réutilisé dans la session
    uint32_t   oid;         // index ordonné compact (compat Augmenta)
    TimePoint  born, updated;
    Vec2       position;    // repère salle, filtré
    Vec2       velocity;
    float      orientation; // direction de déplacement
    Ellipse    extent;      // emprise estimée
    float      height;      // NaN en 2D pur ; renseigné en 3D
    TrackState state;       // Tentative | Confirmed | Coasting
    float      confidence;
};
```

Les tracks `Tentative` (période de probation) ne sont **pas** publiés vers les sorties —
pas de fantômes côté contenu.

### `logic/` — zones et événements

- Événements : `enter`, `exit`, `dwell(t)` par zone ; franchissement de ligne orienté
  (comptage entrées/sorties) ; capacité de zone (alerte si > N personnes).
- Compteurs et statistiques par zone, remis à zéro planifiable.
- Chaque sortie peut filtrer : « n'envoyer que les personnes de la zone Scène ».

### `io/` — sorties

Interface plugin `IOutput` alimentée par le snapshot de tracks de chaque tick + les
événements. Implémentations v1 : OSC Augmenta (legacy + V2), WebSocket JSON compatible
Augmenta, TUIO 1.1, OSC générique configurable, MQTT, webhooks HTTP, enregistrement
CSV/JSONL des trajectoires. Détails : [06 — Protocoles](06-protocoles-et-integrations.md).

Option par sortie : **prédiction** (publier l'état extrapolé à t+Δ pour compenser la latence
capteur+réseau+rendu, comme Augmenta) et **lissage** (filtre One-Euro paramétrable — les
positions Kalman sont déjà lisses, mais certains contenus veulent plus doux).

### `record/` — enregistrement et replay

- Enregistre les `ScanFrame` **bruts** (avant tout traitement) de tous les capteurs +
  snapshot de config, dans un conteneur à chunks compressés zstd, index temporel.
- Ordre de grandeur : un LiDAR 2D ≈ 1 000–4 000 points × 10–40 Hz ≈ quelques Mo/min
  compressé — enregistrement continu en anneau (« boîte noire », N dernières minutes)
  possible en permanence.
- Replay = driver comme un autre : tout le pipeline, l'UI et les sorties fonctionnent à
  l'identique. Vitesse ×0.1 à ×10, pause, scrubbing.

### `api/` — serveur HTTP + WebSocket

- **REST** : CRUD de configuration (validée par schéma JSON — voir `protocol/`),
  actions (apprendre le fond, démarrer calibration, snapshot), état de santé.
- **WebSocket**, canaux séparés abonnables :
  - `tracks` (JSON compact, chaque tick),
  - `points` (binaire, décimé et borné en débit — pour l'affichage temps réel UI),
  - `events` (zones, capteurs, alertes),
  - `health` (fps par capteur, budget latence, CPU).
- Sert l'UI web statique (embarquée dans le binaire ou dossier à côté).
- Auth simple v1 : token local + bind configurable (127.0.0.1 par défaut, LAN sur opt-in).

### `config/` — configuration

- Un **fichier projet** JSON unique : salle, capteurs, calibration, fond appris (référencé),
  zones, sorties, réglages tracker.
- Schéma **versionné avec migrations** (on ne casse jamais un projet client).
- Écriture **atomique** (write-temp + rename), autosave, N sauvegardes tournantes.
- **Hot-apply** : tout paramètre modifiable à chaud sans redémarrage du pipeline.

### Observabilité et robustesse d'exécution

- Logs structurés `spdlog` (rotation, niveaux à chaud).
- Endpoint `/metrics` Prometheus : fps par étage du pipeline, latences p50/p99, tracks
  actifs, mémoire.
- Watchdog interne (un étage bloqué > seuil ⇒ log + restart du pipeline) + supervision
  externe (systemd `Restart=on-failure` / Windows Service recovery).
- Handler de crash : minidump + dernières secondes de la boîte noire ⇒ un incident sur
  site devient un ticket reproductible.

## Modèle de threads

```
[Thread driver × n] → SPSC queue → [Thread pipeline (tick 60 Hz)] → snapshot immuable →
    ├─ [Thread sorties] (OSC/TUIO/MQTT — jamais bloqué par un client lent)
    ├─ [Thread serveur API/WS] (uWebSockets event-loop, backpressure par canal)
    └─ [Thread enregistreur] (I/O disque)
```

Le pipeline produit à chaque tick un **snapshot immuable** (structure figée partagée par
`shared_ptr`) consommé par les threads aval — aucun verrou sur le chemin chaud, un client
WebSocket lent ne peut pas ralentir le tracking.

Budget CPU cible : pipeline complet 8 capteurs 2D + 50 personnes < 25 % d'un cœur moderne
(les données sont petites : quelques dizaines de milliers de points/s — le design
data-oriented suffit largement, pas besoin de GPU).

## Évolution distribuée (post-v1, préparée dès la v1)

Le découpage driver → prétraitement → fusion est déjà celui d'Augmenta (nodes → fusion).
Pour les très grands espaces : un binaire `sillage-node` (acquisition + prétraitement +
envoi des points avant-plan compressés en UDP/QUIC) et le `sillage-engine` central qui fusionne.
Prérequis identifié : synchronisation d'horloges (PTP/NTP + estimation d'offset par
corrélation des tracks dans les recouvrements). Rien dans la v1 ne doit l'empêcher :
le repère salle, les `ScanFrame` timestampés et la fusion par points y sont déjà conformes.
