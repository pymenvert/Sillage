# 09 — Outils de maintenance et de debug

Un tracker en production échoue toujours *sur site*, loin du développeur, dans des
conditions non reproductibles. Tout cet outillage vise une seule chose : **transformer
n'importe quel problème terrain en cas reproductible au bureau**, et donner à
l'installateur les moyens de se dépanner seul.

## 1. Boîte noire MCAP + Foxglove Studio

- L'engine enregistre en continu (anneau des N dernières minutes, opt-out) les scans bruts,
  les tracks, les événements et les métriques au format **MCAP**
  ([ADR-005](adr/005-enregistrement-mcap.md)).
- N'importe quel fichier s'ouvre dans **Foxglove Studio** (gratuit, Windows/Linux/mac) :
  timeline, nuages de points 3D, courbes de métriques, inspection message par message —
  un inspecteur professionnel qu'on n'a pas eu à développer.
- Le replay dans Sillage étant déterministe, un enregistrement joint à un ticket **est** le
  bug : on le rejoue, on le corrige, il devient un test de régression.

## 2. `sillage doctor` — diagnostic d'environnement

Commande CLI (et bouton UI) qui vérifie et explique en langage clair :

- ports série visibles et permissions (groupe `dialout` sur Ubuntu, droits sur mac/Windows) ;
- capteurs joignables (scan mDNS/réseau/série) et firmwares identifiés ;
- pare-feu : ports OSC/WS/HTTP ouverts ou bloqués ;
- horloge système, espace disque pour les enregistrements, droits d'écriture config ;
- versions engine/UI/config cohérentes.

Sortie : OK / avertissements / actions à faire, copiable dans un ticket.

## 3. Rapport de diagnostic en un clic

Bouton UI « Exporter un diagnostic » → zip : config (secrets caviardés), logs récents,
boîte noire des dernières minutes, sortie de `doctor`, versions et plateformes.
C'est la première demande du support, automatisée dès le premier jour.

## 4. Profilage Tracy intégré

Chaque étage du pipeline est instrumenté (zones nommées : acquisition, fond, fusion,
clustering, association, sorties). Build `-DSILLAGE_TRACY=ON` : on connecte le client Tracy
et on voit **chaque tick frame par frame** — la microcoupure de 3 ms toutes les 10 s se
voit, au lieu de se deviner. Coût nul compilé désactivé (macro vide).

## 5. Inspecteur de pipeline (dans l'UI)

Mode debug de la vue scène : afficher chaque étage intermédiaire —
points bruts / fond appris / avant-plan / clusters (avec confiance) / prédictions /
associations (liens mesure↔track) / gates. C'est l'outil qui répond à « pourquoi il ne le
voit pas ? » en 10 secondes : on *voit* à quel étage la personne disparaît.

## 6. Moniteur de sorties

Par sortie (OSC/WS/TUIO/MQTT) : les N derniers messages décodés, compteurs d'envoi,
erreurs réseau, et un mode « sonde » qui vérifie qu'un port UDP de destination répond
(ICMP/écoute). Répond à « TouchDesigner ne reçoit rien » sans Wireshark.

## 7. Injection de pannes (chaos testing)

Le simulateur et les drivers de test savent injecter : déconnexion capteur, trames
corrompues, latence/jitter réseau, dérive d'horloge, scan partiel, capteur bousculé
(offset de pose soudain). Utilisé en CI (les scénarios de panne de la
[suite de validation](03-tracking-et-fusion.md#validation)) et à la main avant une
installation critique. La robustesse se teste, elle ne se constate pas.

## 8. Harnais de soak et de charge

`tools/soak/` : lance l'engine avec un scénario de charge (50–200 agents simulés,
multi-capteurs), surveille RSS, latences p99, descripteurs de fichiers, et échoue si une
tendance dérive. Tourne en nightly CI (1 h) et en pré-release (48 h).

## 9. Journaux exploitables

- Logs structurés avec identifiants stables (`sensor=lidar-2 stage=fusion`), niveaux
  modifiables à chaud par module, visionneuse intégrée à l'UI (filtres, follow).
- Les erreurs orientées utilisateur ont un code et une page de doc associée
  (`E-SER-003 : port série occupé → voir …`).

## 10. Garde-fous de configuration

- Validation par schéma + **linting sémantique** : « le capteur B est hors du contour de
  salle », « zones qui ne se recouvrent pas avec la couverture », « fond appris vieux de
  6 mois ». Avertissements dans l'UI, bloquants uniquement si incohérence dure.
- Toute modification de config est journalisée (diff), rollback en un clic vers les N
  dernières versions — l'erreur de manipulation de 23 h avant l'ouverture des portes se
  répare en 10 secondes.
