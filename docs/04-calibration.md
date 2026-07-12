# 04 — Calibration multi-capteurs

La fusion au niveau des points exige des poses capteur précises (< 5 cm d'erreur dans les
zones de recouvrement, sinon une personne vue par deux capteurs devient deux clusters).
La calibration doit être **rapide sur site** (un installateur, pas un ingénieur) et sa
qualité doit être **mesurée et surveillée**, pas supposée.

## 1. Placement assisté (avant même de calibrer)

L'éditeur de scène de l'UI permet de dessiner la salle et de poser les capteurs
virtuellement : cônes de FOV, portées, zones d'ombre projetées par les obstacles dessinés,
**carte de couverture** (combien de capteurs voient chaque m² — 2+ requis là où les
croisements sont attendus). On valide le plan d'implantation *avant* de percer.
(Équivalent du « designer » d'Augmenta, mais intégré.)

## 2. Calibration manuelle (toujours disponible)

- Le nuage de points brut de chaque capteur s'affiche dans la vue scène, une couleur par
  capteur.
- L'installateur fait glisser/tourner chaque capteur jusqu'à ce que les murs se superposent
  au plan et entre capteurs. Snapping sur les segments droits détectés (RANSAC de lignes
  sur le fond statique : les murs sont des repères naturels).
- Précision atteignable : ~5–10 cm, suffisant pour valider une installation.

## 3. Auto-calibration « en marchant » (le différenciateur)

Principe : une personne marche dans la salle en passant dans les zones de recouvrement ;
sa trajectoire, vue par plusieurs capteurs, contraint leurs poses relatives.

1. Mode calibration : chaque capteur tracke **seul** (tracking mono-capteur suffisant ici).
2. Le marcheur traverse chaque zone de recouvrement en diagonale et en zigzag
   (l'UI guide : « zone A∩B couverte ✓, passez en B∩C »).
3. Pour chaque paire de capteurs, on apparie les trajectoires simultanées et on résout la
   transformation rigide 2D (x, y, θ) par moindres carrés (Umeyama/Horn) avec RANSAC
   contre les appariements aberrants. L'offset temporel entre capteurs est estimé
   conjointement (corrélation des trajectoires) — il tombe gratuitement.
4. Optimisation globale : graphe de poses (capteurs = nœuds, transformations par paires =
   arêtes pondérées par leur covariance), moindres carrés non linéaire, un capteur de
   référence fixe l'origine. Petit graphe (≤ 8 nœuds) : Ceres serait surdimensionné,
   Gauss-Newton maison sur Eigen suffit.
5. Résultat affiché avec **résidu par paire** (RMS en cm). Résidu élevé = recouvrement
   insuffisant ou capteur instable → l'UI le dit explicitement.

Durée cible : **< 5 minutes pour 4 capteurs**, sans cible ni mètre ruban.

Raffinement optionnel ensuite : ICP entre les fonds statiques des paires calibrées
(affine la rotation via les murs, insensible au bruit de trajectoire).

## 4. Score de santé et détection de dérive

En exploitation, deux vérifications continues, peu coûteuses :

- **Cohérence de fond** : écart persistant entre le fond appris et le fond observé sur des
  secteurs entiers ⇒ capteur bousculé ou déplacé. Alerte immédiate (événement + UI +
  MQTT/webhook), proposition de re-calibration ciblée du capteur concerné.
- **Résidu de fusion** : distance moyenne entre les contributions des différents capteurs
  au même track dans les recouvrements. Dérive lente ⇒ score de calibration qui se dégrade,
  visible sur le dashboard *avant* que le tracking se dégrade.

Un capteur en dérive franche peut être **auto-exclu de la fusion** (option) : mieux vaut
perdre de la couverture que polluer toute la salle — et l'alerte a déjà été émise.

## 5. Hauteur de montage et géométrie

- LiDAR 2D horizontal : hauteur genoux/cuisses (~40–70 cm) — bon compromis silhouette
  stable (le plan coupe les deux jambes ou le bassin) / immunité au mobilier bas. Hauteur
  chevilles possible (esthétique, discret) au prix de clusters bimodaux (2 jambes) — le
  rayon de liaison du clustering est calibré pour.
- La hauteur de montage est une métadonnée de pose : utile pour interpréter l'emprise
  attendue d'une personne et pour la future projection des LiDARs 3D.
- Doc d'installation : viser l'horizontalité (un LiDAR 2D incliné voit le sol comme un arc
  au loin — détecté par le RANSAC de lignes et signalé « capteur incliné »).
