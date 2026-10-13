#!/usr/bin/env bash
# Copia las corridas de runs/ a docs/corridas/, recortando las muy largas
# (se conserva el inicio y el final; el archivo completo esta en runs/).
cd "$(dirname "$0")/.."
D=docs/corridas
cut_file() {  # cut_file <origen> <destino> <lineas_inicio> <lineas_final>
    local n; n=$(wc -l < "$1")
    if [ "$n" -le $(( $3 + $4 + 5 )) ]; then cp "$1" "$2"; return; fi
    { head -n "$3" "$1"; echo "      [... $(( n - $3 - $4 )) lineas omitidas; salida completa en ${1} ...]"; tail -n "$4" "$1"; } > "$2"
}
for f in caso_A_texto caso_A_suerte caso_B_calc caso_C_run; do cp runs/$f.txt $D/$f.txt; done
cut_file runs/caso_A_imposible.txt $D/caso_A_imposible.txt 45 40
cut_file runs/caos_T20.txt $D/caos_T20.txt 30 45
