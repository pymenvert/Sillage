# 08 — Ingénierie

## Stack engine (C++20)

| Besoin | Choix | Pourquoi |
|---|---|---|
| Build | CMake ≥ 3.27 + presets | standard, IDE-friendly, multiplateforme |
| Dépendances | vcpkg (mode manifest) | reproductible, binaire-cache en CI, MSVC+GCC |
| Algèbre | Eigen | Kalman, moindres carrés, transformations |
| Logs | spdlog | structurés, rotation, quasi gratuit |
| JSON | nlohmann/json (config) | ergonomie ; passer à simdjson si un profil le justifie |
| HTTP/WS | uWebSockets | très rapide, event-loop unique, backpressure |
| OSC | implémentation interne (~300 lignes) | l'OSC est trivial ; contrôle total du format Augmenta |
| Tests | GoogleTest + Google Benchmark | standard |
| Enregistrement | MCAP (+ zstd) | format standard robotique, lisible dans Foxglove ([ADR-005](adr/005-enregistrement-mcap.md)) |
| Profilage | Tracy (opt-in compile) | vue frame-par-frame du pipeline, quasi zéro coût désactivé |
| Backtraces | backward-cpp / minidumps | crashs exploitables sur les trois OS |

Interdits sur le chemin chaud : allocation non bornée, verrous, I/O. Structures
data-oriented (SoA pour les points), buffers réutilisés.

## Qualité

- `-Wall -Wextra -Werror` (et `/W4 /WX`), clang-format + clang-tidy imposés (hook + CI).
- Jobs CI dédiés **ASan/UBSan** (tests) et **TSan** (tests de threading du pipeline).
- Code, commentaires et messages de commit **en anglais** (docs produit en français).
- Tout algorithme du [03](03-tracking-et-fusion.md) a ses tests unitaires **et** ses
  scénarios simulateur ; un bug de terrain devient systématiquement un replay dans la suite.

## Simulateur {#simulateur}

Composant de première classe (pas un mock) :

- agents avec modèle de marche réaliste (accélérations bornées, évitement simple,
  comportements scriptables : croisement, demi-tour, groupe, file) ;
- capteurs virtuels : raycasting 2D contre agents (2 jambes = 2 cercles) + murs, bruit de
  distance réaliste, taux de drop, fréquences différentes par capteur, désynchronisation ;
- **vérité terrain exportée** → calcul MOTA/IDF1/ID-switches en sortie de pipeline ;
- scénarios en fichiers YAML versionnés dans `simulator/scenarios/`.

Il sert : aux tests CI, au réglage du tracker, aux démos sans matériel, et à l'UI de
placement ([04 §1](04-calibration.md)).

## CI/CD (GitHub Actions)

| Workflow | Déclencheur | Contenu |
|---|---|---|
| `build-test` | PR, push main | matrice {windows-latest, ubuntu-22.04, macos-15 (arm64)} × {Debug, Release} : build, tests unitaires, lint C++ et TS, build UI |
| `tracking-metrics` | PR, push main | suite simulateur + replays réels ; **échec si une métrique régresse** (seuils versionnés dans le repo) |
| `sanitizers` | PR | ASan/UBSan + TSan sur Ubuntu |
| `nightly` | cron | soak court (1 h), benchmarks avec suivi de tendance, scan dépendances |
| `release` | tag `v*` | installeurs Windows + .deb, UI embarquée, notes de release, artefacts GitHub Release |

## Conventions de travail

- **Trunk-based** : `main` protégée, PRs courtes revues, squash merge.
- **Conventional commits** (`feat:`, `fix:`, `perf:`…) → changelog automatique, semver.
- **ADRs** dans `docs/adr/` : toute décision structurante y passe (format court : contexte,
  décision, conséquences). Les quatre premières sont écrites.
- Issues/milestones GitHub alignés sur M0–M6 ; labels `tracker`, `drivers`, `ui`,
  `calibration`, `io`, `infra`.
- Datasets réels : Git LFS, nommage `YYYY-MM-DD_<lieu>_<scenario>`.

## Packaging et exécution

| | Windows | Ubuntu | macOS |
|---|---|---|---|
| Format | installeur Inno Setup | paquet `.deb` | `.pkg` universel (arm64 + x86_64) |
| Service | Windows Service (recovery auto) | unité systemd (`Restart=on-failure`, hardening : User dédié, ProtectSystem) | launchd (`KeepAlive`) |
| Accès série/réseau | driver USB-série constructeur documenté | groupe `dialout`, règles udev fournies | drivers série intégrés à l'OS |
| Données | `%ProgramData%\Sillage` | `/var/lib/sillage` | `/Library/Application Support/Sillage` |
| UI | navigateur → `http://localhost:8080` | idem | idem |
| Signature | Authenticode si certificat | — | Developer ID + notarisation (compte Apple 99 $/an requis ; builds dev non signés utilisables) |

## Licences (à décider avant publication — voir première réunion projet)

Options : cœur open source (AGPL) + services payants, open core, ou propriétaire avec SDKs
ouverts. **Décision non prise** — n'engage rien tant que le repo est privé, mais bloque
toute publication. Les licences des SDKs capteurs utilisés (BSD/MIT/Apache) sont
compatibles avec tous ces scénarios.
