# ADR-002 — UI web embarquée (pas de client natif)

**Statut** : accepté · **Date** : 2026-07-12

## Contexte

Il faut une interface de configuration moderne et visuelle, identique sur Windows et
Ubuntu, utilisable sur site (souvent depuis une tablette, pendant qu'on manipule les
capteurs) et à distance (machine headless dans une régie).

## Options

- **Qt/JUCE natif** : performant, mais double travail de design, distribution par OS, pas
  d'accès distant naturel. (JUCE = choix d'Augmenta.)
- **Electron/Tauri** : web packagé, mais impose une app à installer et n'apporte rien ici —
  l'engine a de toute façon besoin d'un serveur pour le mode headless.
- **Web embarquée dans l'engine (choisi)** : React servie par l'engine, WebSocket temps
  réel. Zéro installation client, tablette OK, distant OK, un seul design.

## Décision

React + TypeScript + WebGL, servie par le serveur HTTP embarqué de l'engine. Un wrapper
desktop (Tauri) reste possible plus tard si un besoin « app » apparaît — c'est un emballage,
pas une réécriture.

## Conséquences

- Le flux de points vers l'UI doit être décimé/borné (bande passante navigateur).
- Sécurité d'accès réseau à traiter explicitement (bind localhost par défaut, token).
- Le rendu scène exige du soin WebGL — c'est aussi ce qui rend le produit démontrable.
