# 06 — Protocoles et intégrations

> ⚠️ **Document de conception.** Il décrit la cible des jalons M0→M6, **pas**
> l'état livré en v1.0.1. Pour ce qui existe réellement : [CHANGELOG](../CHANGELOG.md)
> et [guide de démarrage](10-guide-demarrage.md).

Stratégie ([ADR-004](adr/004-compatibilite-protocole-augmenta.md)) : **parler nativement le
protocole Augmenta** en OSC et WebSocket. Tout l'écosystème existant (plugins TouchDesigner,
Unity, Unreal, notch, vvvv, exemples communautaires) fonctionne alors avec Sillage sans
qu'on écrive un seul plugin. On ajoute nos propres canaux pour ce qui dépasse ce protocole.

## Sorties v1

### OSC Augmenta (legacy v1 + V2)

- Messages par personne (`personEntered` / `personUpdated` / `personWillLeave`) et scène,
  avec les champs du modèle Augmenta : `frame, id, oid, age, centroid, velocity,
  orientation, boundingRect (pos/size/rotation), height`.
- Implémentation validée **contre les plugins officiels** (TouchDesigner, Unity, Unreal)
  utilisés comme harnais de test de conformité : le plugin doit afficher nos données sans
  aucune modification. La spec exacte des messages est extraite des SDKs open source
  d'Augmenta (AugmentaClientSDK-cpp/CS, plugins GitHub) en M4.
- Unicast/broadcast/multicast, plusieurs destinations, cadence découplée du tick.

### WebSocket Augmenta

Même approche : conformité mesurée contre `AugmentaClientSDK-cpp` / `-CS` (les SDKs clients
officiels sont nos tests d'intégration).

### TUIO 1.1

Profil `2Dcur` (curseurs) et `2Dblb` (blobs avec emprise) — couvre les vieux logiciels
interactifs et le mapping multitouch géant.

### PSN — PosiStageNet

Le protocole standard du spectacle vivant (ouvert et sans royalties, VYV + MA Lighting,
UDP multicast) : trackers avec position/vitesse/orientation 3D. Consommé nativement par
**grandMA3** (poursuite lumière automatique !), **disguise**, WATCHOUT, Stage Precision,
vvvv… Implémentation de référence C++ open source
([vyv/psn-cpp](https://github.com/vyv/psn-cpp)) et spec publique
([posistage.net](https://posistage.net)). C'est la sortie qui ouvre le marché
scène/théâtre/tournées — celui de Naostage — à coût d'implémentation faible.

### ADM-OSC — audio spatialisé

Dictionnaire OSC standardisé ([immersive-audio-live/ADM-OSC](https://github.com/immersive-audio-live/ADM-OSC),
porté par L-Acoustics, Flux::, d&b, DiGiCo, Meyer Sound…) pour les positions d'objets
audio. Une personne trackée = un objet audio qui la suit dans **L-ISA, SPAT Revolution,
d&b Soundscape** — le son immersif qui suit les visiteurs, cas d'usage phare des salles
immersives. S'appuie sur notre encodeur OSC existant : mapping track → objet configurable
dans l'UI (plage d'objets, zone, normalisation des coordonnées).

### OSC générique configurable

Gabarit d'adresse et de champs définissable dans l'UI (ex. `/person/{id}/pos ff`) — pour
les intégrations custom sans attendre une release.

### MQTT + Webhooks

Événements de zones (enter/exit/dwell/comptage) et alertes santé vers la domotique,
la supervision ou le cloud client. QoS 1, reconnexion automatique.

### Enregistrement de trajectoires

CSV / JSONL (une ligne par tick et par personne) pour l'analyse offline (notebooks,
tableurs). Parquet en post-v1 si la demande analytique se confirme.

## API propre (engine ↔ UI ↔ clients avancés)

- **REST** : configuration complète (CRUD validé par schéma), actions, santé. C'est l'API
  qu'utilise notre UI — elle est donc complète par construction, documentée OpenAPI.
- **WebSocket** : canaux `tracks`, `points` (binaire), `events`, `health` — abonnement
  sélectif. Un client tiers peut consommer `tracks` directement (JSON simple documenté).

## `protocol/` — source de vérité

Tous les schémas (config projet, messages WS, événements) vivent dans `protocol/` en JSON
Schema. Génération de code : types TypeScript (UI) + structs C++ (engine). Un changement de
protocole est un diff lisible dans un seul dossier, versionné semver.

## Intégrations testées en continu (M4)

| Cible | Via | Test |
|---|---|---|
| TouchDesigner | plugin Augmenta officiel (OSC/WS) | scène de test qui affiche les tracks |
| Unity | AugmentaUnityWSClient | scène d'exemple |
| Unreal | AugmentaUnreal (OSC) | niveau d'exemple |
| Madmapper / Resolume | OSC générique | mapping de test |
| Chataigne | OSC + WS | module de routage |
| grandMA3 | PSN | poursuite d'un track par un projecteur motorisé |
| disguise / WATCHOUT | PSN | objet 3D suivant un track |
| L-ISA / SPAT Revolution / d&b Soundscape | ADM-OSC | objet audio suivant un track |
