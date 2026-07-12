# 03 — Tracking et fusion : conception détaillée

C'est le cœur de la valeur du produit. Objectif formel : **minimiser les échanges d'ID
(ID switches) et les fragmentations de trajectoires**, mesurés par les métriques MOT
standard (IDF1, nombre d'ID switches, MOTA) sur une bibliothèque de scénarios — pas « ça a
l'air de marcher », mais des chiffres en CI.

## 1. Pourquoi la fusion au niveau des points ([ADR-003](adr/003-fusion-niveau-points.md))

Deux stratégies existent pour multi-LiDAR :

- **Track-to-track** : chaque capteur tracke seul, on fusionne les trajectoires. Simple,
  mais chaque capteur seul est myope : lors d'un croisement, le capteur qui a l'occultation
  commet l'échange d'ID *avant* la fusion, qui ne peut plus le réparer proprement.
- **Au niveau des points (choisi)** : tous les points avant-plan sont projetés dans le
  repère salle, le clustering et le tracking voient la salle entière. Une personne masquée
  pour le capteur A reste vue par le capteur B : **l'occultation disparaît du problème** au
  lieu d'être réparée après coup. C'est la raison d'être du multi-LiDAR.

Conséquence assumée : la qualité de la calibration devient critique (deux capteurs mal
alignés créent des « personnes doubles »). D'où l'investissement du
[04 — Calibration](04-calibration.md) et le score de santé de calibration.

## 2. Gestion du temps

Les capteurs ont des fréquences différentes (10–40 Hz) et ne sont pas synchronisés. À chaque
tick (60 Hz) :

- chaque mesure porte son `t_capture` (estimé : arrivée − latence driver calibrée) ;
- les points d'un scan vieux de Δt sont **compensés en mouvement** : si un cluster est
  associé à un track de vitesse v, ses points sont décalés de v·Δt avant fusion. Premier
  ordre suffisant (Δt ≤ 100 ms, vitesse de marche ≤ 2,5 m/s ⇒ correction ≤ 25 cm, sinon
  une personne rapide se dédoublerait entre un capteur 10 Hz et un 40 Hz).

## 3. Clustering conscient des tracks

Clustering euclidien (grille de hachage, rayon de liaison ~0,35 m adapté à l'anatomie :
deux jambes d'une même personne fusionnent, deux personnes à > 50 cm ne fusionnent pas).

**Le problème des croisements** : quand deux personnes se rapprochent à < 50 cm, leurs
points forment *un seul* cluster. Un tracker naïf voit 2 tracks → 1 mesure → il en perd un,
et à la séparation les IDs repartent au hasard : c'est là que naissent les échanges d'ID.

**Solution — splitting guidé par les prédictions** :

1. À chaque tick, on prédit la position de chaque track confirmé (Kalman, voir §4).
2. Si un cluster est « gros » (emprise > seuil personne) **et** que k ≥ 2 prédictions de
   tracks tombent dans son emprise élargie, le cluster est **redécoupé en k sous-clusters**
   par k-means contraint, semé aux positions prédites (2–3 itérations suffisent).
3. Chaque sous-cluster devient une mesure distincte → les deux tracks continuent d'être
   mis à jour *pendant* le contact, avec une covariance de mesure gonflée (on sait que la
   séparation est incertaine).
4. Garde-fou anti-divergence : si les points ne supportent pas la séparation (clusters
   dégénérés, < N points chacun), on retombe en mode « mesure partagée » : les deux tracks
   coastent sur leur prédiction avec la contrainte de rester dans l'emprise du cluster.

C'est la technique qui fait la différence entre un tracker qui « marche en démo » et un
tracker qui tient une salle pleine.

## 4. Filtre par track

- **Kalman** état `[x, y, vx, vy]`, modèle vitesse constante, bruit de process réglé pour
  la marche humaine (accélérations ~1–2 m/s²). Extension post-v1 si besoin : IMM
  (vitesse constante + virage) — à ne faire que si les métriques le justifient.
- Attributs hors filtre, en moyenne glissante : emprise (ellipse), nombre de points,
  hauteur (3D uniquement), **signature de ré-identification** (voir §7).
- `orientation` = direction de la vitesse, hystérésis pour éviter le flip à l'arrêt.

## 5. Association mesures ↔ tracks

À chaque tick, matrice de coût tracks × mesures :

```
coût(i,j) = d²_Mahalanobis(prédiction_i, mesure_j)      // position + incertitude
          + λ_v · pénalité de cohérence de vitesse       // la mesure implique-t-elle un
                                                         // demi-tour instantané ? suspect
          + λ_s · dissimilarité de taille (emprise, nb de points)
```

- **Gating** : coût infini au-delà du seuil χ² (95 %) — jamais d'association aberrante.
- Résolution optimale par **Hungarian / Jonker-Volgenant** (n ≤ 100 ⇒ négligeable en CPU).
- **Test anti-échange** : pour toute paire de tracks proches (< 1 m) assignés à deux mesures,
  comparer le coût de l'assignation directe vs croisée **en incluant la continuité de
  vitesse** ; ne croiser que si l'écart dépasse une marge franche. Deux personnes qui se
  croisent conservent leur cap ; deux personnes qui se rencontrent et repartent chacune de
  leur côté ont des vitesses qui s'inversent — c'est précisément l'ambiguïté que ce test
  arbitre, et un cas de test dédié du simulateur.

Alternative évaluée puis écartée pour la v1 : JPDA / MHT complet. Coût de complexité élevé,
gain marginal une fois le splitting guidé (§3) et le test anti-échange en place. Réévaluation
sur métriques en M2 si nécessaire.

## 6. Cycle de vie des tracks

```
          n_hits ≥ M (sur N ticks)                 miss > T_coast
naissance ────────────────▶ Confirmed ────────────────▶ mort (→ tombe re-ID, §7)
    │  Tentative                ▲    │ miss                    
    │  (probation, non publié)  │    ▼                        
    └── miss précoce ⇒ mort     └─ Coasting (prédiction seule,
                                    covariance croissante, publié
                                    avec confidence décroissante)
```

- **Probation** M/N (ex. 3 hits sur 5 ticks) : le bruit ne crée pas de fantômes publiés.
- **Coasting** : T_coast ≈ 1–2 s selon config. Un track qui coaste est réassociable en
  priorité dès qu'une mesure réapparaît dans son gate (élargi avec le temps).
- Événements publiés : `personEntered` (à la confirmation), `personUpdated` (chaque tick),
  `personWillLeave` (à la mort) — sémantique alignée sur le protocole Augmenta.

## 7. Ré-identification après perte (la « mémoire »)

Quand un track meurt (sortie de champ, occultation trop longue), il entre dans une **tombe**
pendant T_reid (configurable, 2–5 s) avec : dernière position, vitesse, signature.

À la naissance d'un nouveau track, on cherche une tombe compatible :

- position plausible : distance ≤ ce que la vitesse de la tombe permet (cône de
  déplacement), pondérée par le temps écoulé ;
- signature compatible : emprise/taille de cluster ; en 3D : hauteur (très discriminante) ;
- **jamais** de ré-ID à travers une zone d'entrée/sortie de salle (une personne sortie est
  sortie — c'est une nouvelle personne qui entre).

Si match : le nouveau track **hérite de l'ID** (et l'événement `personWillLeave` n'ayant pas
encore été émis — il est retenu pendant T_reid_grace — l'extérieur ne voit qu'une continuité).

Signature v1 = géométrie (emprise, nb points normalisé par la distance). Piste expérimentale
notée pour plus tard : cadence de pas visible sur LiDAR à hauteur de jambes (période
d'oscillation des clusters de jambes) — discriminant démontré en recherche, à valider.

## 8. IDs et `oid`

- `id` : uint32 croissant, **jamais réutilisé** dans la session — les contenus peuvent
  l'utiliser comme clé sans risque de collision.
- `oid` : index ordonné compact (0..n-1 des tracks vivants) pour la compat Augmenta et les
  contenus qui mappent « la 3e personne » sur un effet.

## 9. Sorties : prédiction et lissage

- **Compensation de latence** : publier l'état Kalman extrapolé à `t + Δ_pred`
  (Δ_pred configurable par sortie, typ. 50–100 ms ≈ latence capteur + rendu du contenu).
  C'est le « highly efficient prediction algorithm » d'Augmenta — avec un Kalman propre,
  c'est une extrapolation quasi gratuite.
- **Lissage optionnel** One-Euro par sortie (compromis réactivité/douceur choisi par le
  créateur de contenu, pas par le tracker).

## 10. Validation — le contrat de robustesse {#validation}

Bibliothèque de scénarios simulés (vérité terrain exacte) + enregistrements réels annotés :

| Scénario | Critère CI (2 capteurs) |
|---|---|
| Croisement en X, 2 personnes, 0,8–2 m/s | 0 ID switch |
| Croisement + demi-tour au contact (le piège) | 0 ID switch |
| Marche côte à côte 30 s à 40 cm | 2 tracks distincts en continu |
| Groupe de 8 en déambulation aléatoire, 2 min | IDF1 ≥ 0,95 |
| File d'attente serrée + défilement | fragmentation < seuil |
| Occultation totale 1 s derrière pilier | même ID à la réapparition |
| Panne d'un capteur sur deux à t=60 s | dégradation propre, 0 faux tracks au failover |
| 50 personnes aléatoires (stress) | tick 60 Hz tenu, pas d'allocation non bornée |

Ces scénarios tournent en CI à chaque PR (replay déterministe). **Une régression de
métrique bloque le merge.** Les enregistrements réels enrichissent la bibliothèque à chaque
session de test physique.
