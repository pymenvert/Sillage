#!/bin/sh
# Sillage portable launcher: starts the engine, then opens the UI.
cd "$(dirname "$0")" || exit 1
echo "Démarrage de Sillage... (Ctrl+C pour arrêter le moteur)"
(
    sleep 2
    if command -v xdg-open >/dev/null 2>&1; then xdg-open http://127.0.0.1:8080
    elif command -v open >/dev/null 2>&1; then open http://127.0.0.1:8080
    fi
) &
exec ./sillage-engine "$@"
