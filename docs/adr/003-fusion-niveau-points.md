# ADR-003 — Fusion multi-capteurs au niveau des points

**Statut** : accepté · **Date** : 2026-07-12

## Contexte

Plusieurs LiDARs couvrent la même salle. Il faut choisir où fusionner : après tracking
(track-to-track) ou avant détection (points).

## Décision

Fusion **au niveau des points** : tous les points avant-plan sont projetés dans le repère
salle et le clustering/tracking opère sur la salle entière.

## Justification

L'échange d'ID naît de l'occultation. En track-to-track, chaque capteur subit seul ses
occultations et commet l'erreur avant la fusion. Au niveau des points, une personne masquée
pour un capteur reste mesurée par les autres : le problème disparaît structurellement dans
les zones à recouvrement — ce qui est exactement l'argument de vente du multi-LiDAR.

## Conséquences

- La précision de calibration devient critique (< 5 cm) → investissement calibration
  ([04](../04-calibration.md)) : auto-calibration, score de santé, détection de dérive.
- La compensation temporelle inter-capteurs est nécessaire (fréquences différentes).
- Le mode distribué futur transporte des points avant-plan compressés (débit faible :
  seuls les points de personnes voyagent), pas des tracks.
