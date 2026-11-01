#!/boot/system/bin/env bash

export HAIKU_REVISION=PiratiDelFrico-rev11
export HAIKU_IMAGE_SIZE=1280

mkdir -p .logs
ERROR_LOG=".logs/build_error.log"
> "$ERROR_LOG"

# Se non siamo in una sessione tmux, ne avviamo una dedicata
if [ -z "$TMUX" ]; then
    tmux new-session -s haiku_build "$0 $*"
    exit 0
fi

# Configurazione tmux
tmux set -g mouse on
tmux set-option -g allow-rename off

# ATTENZIONE QUI: usiamo -h (horizontal split -> crea due colonne affiancate)
# -p 30 dice che il nuovo pannello (destra) prende il 30% dello spazio
tmux split-window -h -p 30 -- sh -c "tail -n 100 -f '$ERROR_LOG'"

# Rimettiamo il focus sul pannello principale a sinistra (dove gira jam)
tmux select-pane -t 0

echo "=== Avvio compilazione Haiku (${HAIKU_REVISION}) ==="

# Eseguiamo jam: a sinistra gira la compilazione normale, a destra gli errori in tempo reale
time stdbuf -e0 jam -j$(nproc) \
    -sHAIKU_INCLUDE_TRADEMARKS="#" \
    -sLOVE_IS_IN_THE_AIR=1 \
    -q @nightly-anyboot \
    2> >(tee -a "$ERROR_LOG" >&2)
BUILD_EXIT_CODE=$?

echo ""
echo "─────────────────────────────────────────────────────────────"
if [ $BUILD_EXIT_CODE -eq 0 ]; then
    echo -e "\033[1;32m ✔ COMPILAZIONE COMPLETATA CON SUCCESSO! \033[0m"
else
    echo -e "\033[1;31m ✖ COMPILAZIONE FALLITA (Exit code: $BUILD_EXIT_CODE) \033[0m"
    echo " Controlla i dettagli in: $ERROR_LOG"
fi
echo "─────────────────────────────────────────────────────────────"
echo "Premi un tasto qualsiasi per chiudere questa sessione tmux..."
read -n 1
