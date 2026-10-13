#!/usr/bin/env bash
# ============================================================================
#  run_experiments.sh - Experimentos de la seccion 0x02 (Tarea 3, IC-6600)
#  Cada experimento alimenta la Box por stdin en modo silencioso (-q) y
#  guarda una fila CSV por solicitud (-o). Uso:
#     ./experiments/run_experiments.sh [E1 E2 E3 E4 E5 E6 E7]   (defecto: todos)
# ============================================================================
set -u
cd "$(dirname "$0")/.."
make -s meeseeks
BIN=./meeseeks
OUT=experiments/data
mkdir -p "$OUT"

# texto_req <texto> <dificultad> <duracion> <repeticiones>
texto_req() { for ((r = 0; r < $4; r++)); do printf '%s\n%s\n%s\n' "$1" "$2" "$3"; done; }

# E1: dificultad vs. costo (duracion fija 0.1 s para aislar la dificultad)
E1() {
    rm -f $OUT/e1.csv
    for d in 100 95 90 86 85 80 70 60 50 46 45 40 30 20 10 5 2 1 0.5 0.1 0.01 0; do
        { texto_req "E1 d=$d" "$d" 0.1 20; echo adios; } | $BIN -q -T 120 -o $OUT/e1.csv > /dev/null
        echo "E1 d=$d listo"
    done
}

# E2: duracion aleatoria (0.5-5 s) frente a duracion fija igual a su media (2.75 s)
E2() {
    rm -f $OUT/e2.csv
    for d in 90 50 20; do
        { texto_req "E2 d=$d aleatoria" "$d" "" 10; texto_req "E2 d=$d fija" "$d" 2.75 10; echo adios; } \
            | $BIN -q -T 300 -o $OUT/e2.csv > /dev/null
        echo "E2 d=$d listo"
    done
}

# E3: costo real de los tres casos (A con un solo Meeseeks, B, C)
E3() {
    rm -f $OUT/e3.csv
    {
        texto_req "E3 texto d=100" 100 0.000001 200
        for ((r = 0; r < 50; r++)); do
            echo "calc 2+3*4"
            echo "calc ((1+2)*(3+4)-5)/2 ^ 2 % 7 >= 1 && !(3 == 4) || false"
            echo "calc sqrt(16) + abs(-3) * 2"
            echo "calc 1/0"
        done
        for ((r = 0; r < 200; r++)); do echo "run true"; done
        for ((r = 0; r < 50; r++)); do echo "run false"; done
        for ((r = 0; r < 50; r++)); do echo "run ls /usr/bin > /dev/null"; done
        for ((r = 0; r < 20; r++)); do echo "run sleep 0.25"; done
        echo adios
    } | $BIN -q -o $OUT/e3.csv > /dev/null
    echo "E3 listo"
}

# E4: plan de contingencia. (a) semaforo de slots con d=10; (b) tope total con d=0
E4() {
    rm -f $OUT/e4a.csv $OUT/e4b.csv
    for a in 2 4 8 16 32 64 128 512; do
        { texto_req "E4a a=$a" 10 0.1 15; echo adios; } | $BIN -q -a $a -T 300 -o $OUT/e4a.csv > /dev/null
        echo "E4a a=$a listo"
    done
    for m in 100 250 500 1000 2000 3000 5000; do
        { texto_req "E4b m=$m" 0 0.1 5; echo adios; } | $BIN -q -m $m -T 300 -o $OUT/e4b.csv > /dev/null
        echo "E4b m=$m listo"
    done
}

# E5: sensibilidad a la ganancia de dilucion g
E5() {
    rm -f $OUT/e5.csv
    for g in 0.25 0.5 1.0; do
        for d in 50 20 5; do
            { texto_req "E5 g=$g d=$d" "$d" 0.1 20; echo adios; } | $BIN -q -g $g -m 20000 -T 300 -o $OUT/e5.csv > /dev/null
            echo "E5 g=$g d=$d listo"
        done
    done
}

# E6: tareas muy dificiles que superan el tiempo maximo (modo -x, a escala)
#     Planeta "pequeno": 16 slots, duracion 0.25 s, tiempo maximo 30 s.
E6() {
    rm -f $OUT/e6.csv
    for d in 1 0.1 0.01 0.001 0.0001; do
        { texto_req "E6 d=$d" "$d" 0.25 3; echo adios; } | $BIN -q -x -a 16 -m 30000 -T 30 -o $OUT/e6.csv > /dev/null
        echo "E6 d=$d listo"
    done
}

# E7: escala real con la configuracion por defecto (T = 300 s, duraciones 0.5-5 s)
#     (a) d = 0 sin tope que lo detenga: el caos planetario termina todo a los 300 s
#     (b) d = 0.001 en modo -x con 32 slots: cuanto tiempo adicional necesita
E7() {
    rm -f $OUT/e7.csv
    { texto_req "E7 imposible" 0 "" 1; echo adios; } | $BIN -q -m 100000 -o $OUT/e7.csv > runs/e7_caos.txt
    echo "E7a listo"
    { texto_req "E7 d=0.001" 0.001 "" 1; echo adios; } | $BIN -q -x -a 32 -m 30000 -o $OUT/e7.csv > runs/e7_extra.txt
    echo "E7b listo"
}

LIST=${*:-"E1 E2 E3 E4 E5 E6 E7"}
for e in $LIST; do echo "== $e =="; $e; done
