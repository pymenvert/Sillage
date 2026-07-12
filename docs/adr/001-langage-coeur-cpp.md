# ADR-001 — Cœur en C++20

**Statut** : accepté · **Date** : 2026-07-12

## Contexte

Le moteur temps réel doit tourner sur Windows et Ubuntu, intégrer les SDKs des capteurs,
tenir un tick 60 Hz déterministe et rester maintenable des années.

## Options

- **C++20** : tous les SDKs LiDAR (Slamtec, Hokuyo, SICK, Ouster, Livox, Hesai) sont
  C/C++ ; écosystème temps réel mûr ; c'est aussi la stack d'Augmenta (JUCE) et du secteur
  créatif. Risques mémoire maîtrisables par sanitizers + conventions.
- **Rust** : sécurité mémoire supérieure, mais chaque SDK capteur demande un binding FFI à
  écrire et maintenir, et l'embauche/contribution dans le secteur cible est plus rare.
- **Go/C#** : GC = jitter sur le chemin chaud ; bindings capteurs également nécessaires.

## Décision

C++20, CMake + vcpkg, avec discipline : warnings-as-errors, clang-tidy, ASan/UBSan/TSan en
CI, pas d'allocation sur le chemin chaud.

## Conséquences

- Intégration directe des SDKs constructeurs, zéro couche FFI.
- La rigueur mémoire repose sur l'outillage et la revue — d'où leur place non négociable
  dans la CI ([08](../08-ingenierie.md)).
