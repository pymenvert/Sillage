# ADR-005 — MCAP comme format d'enregistrement

**Statut** : accepté · **Date** : 2026-07-12

> **Note d'implémentation (2026-07-12)** : en attendant l'arrivée de vcpkg (et donc de la
> dépendance mcap+zstd), un format natif provisoire `.srec` (binaire versionné, replay
> déterministe testé) porte la fonctionnalité — voir `engine/src/record/`. La migration
> vers MCAP fournira un convertisseur `.srec` → `.mcap` ; le format `.srec` ne fait
> l'objet d'aucune garantie de stabilité au-delà.

## Contexte

La boîte noire et le replay déterministe ([02](../02-architecture.md), [09](../09-outils-maintenance-debug.md))
exigent un format de fichier : chunks compressés, index temporel, multi-canaux
(scans bruts par capteur, tracks, événements, métriques), écriture en flux append-only.
Le premier plan prévoyait un conteneur maison zstd.

## Décision

Utiliser **MCAP** (mcap.dev, projet open source de Foxglove, format par défaut des bags
ROS 2) avec sérialisation de nos propres schémas (binaire pour les scans, JSON/Protobuf
pour le reste) et compression zstd intégrée.

## Justification

- Exactement le cahier des charges : append-only streamable, chunks compressés, indexé,
  multi-canaux hétérogènes, bibliothèque C++ officielle légère.
- **Foxglove Studio lit nos enregistrements nativement** : timeline, nuages de points,
  courbes — un outil d'inspection pro offert, y compris pour les clients avancés.
- Format neutre et pérenne (« record once, read forever ») : Python/TS/Go/Rust/Swift pour
  l'analyse offline, écosystème robotique entier compatible.
- Un conteneur maison n'aurait aucun de ces avantages et le même coût d'implémentation.

## Conséquences

- Dépendance vcpkg `mcap` (header-only + lz4/zstd) — légère.
- Nos schémas de messages sont publiés dans `protocol/` pour que les fichiers soient
  auto-décrits (schéma embarqué dans le MCAP).
- Pour la visualisation riche dans Foxglove, publier aussi les canaux au format des
  schémas Foxglove (`PointCloud`, etc.) — optionnel, activable à l'enregistrement.
