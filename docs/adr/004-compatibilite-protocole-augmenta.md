# ADR-004 — Compatibilité native avec le protocole Augmenta

**Statut** : accepté · **Date** : 2026-07-12

## Contexte

La valeur d'un tracker se réalise dans les logiciels de contenu (TouchDesigner, Unity,
Unreal, Madmapper…). Écrire et maintenir nos propres plugins pour chacun est un chantier
permanent. Augmenta a déjà un écosystème de plugins open source (MIT/BSD sur leur GitHub)
et un protocole OSC/WebSocket de facto standard dans le milieu.

## Décision

Les sorties OSC et WebSocket de Sillage implémentent le protocole Augmenta (legacy v1 et
V2), conformité validée en CI contre les SDKs clients et plugins officiels open source.
Nos extensions passent par des canaux propres (API REST/WS documentée), jamais par une
altération du protocole compatible.

## Justification

- Adoption immédiate : tout projet existant « parlant Augmenta » fonctionne avec Sillage
  en changeant une IP.
- Le protocole (messages OSC, JSON WS) n'est pas une œuvre protégée : l'interopérabilité
  par réimplémentation est légitime et courante. Nous réimplémentons depuis les specs et
  SDKs publiés, sans copier de code non libre.
- Nos plugins à nous deviennent inutiles en v1 → des semaines économisées.

## Conséquences

- Suivre les évolutions du protocole Augmenta (veille sur leurs repos).
- Positionnement commercial assumé de compatible/concurrent — cohérent avec un produit
  « bring your own LiDAR ».
- Les champs que nous ne pouvons pas remplir en 2D (height) suivent la convention du
  protocole (valeur par défaut documentée).
