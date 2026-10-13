# SO-T3: Box de Mr. Meeseeks

IC-6600 Sistemas Operativos, Instituto Tecnológico de Costa Rica, II Semestre 2026.
Prof. M.Sc. Esteban Arias-Méndez. Tarea 3 (individual).

Programa en C sobre Linux que simula la Caja de Mr. Meeseeks: cada solicitud del
usuario es atendida por procesos creados con `fork()`, que se comunican por `pipe()`
y comparten contadores protegidos con semáforos POSIX (`sem_t`) en memoria compartida.

## Compilar

```sh
make            # genera ./meeseeks
```

## Uso

```sh
./meeseeks [-T seg] [-m max_total] [-a max_vivos] [-g ganancia] [-s semilla] [-q] [-x] [-k] [-o salida.csv]
```

| Opción | Significado | Defecto |
|---|---|---|
| `-T` | Tiempo máximo por solicitud antes del caos planetario (0 = sin límite) | 300 s |
| `-m` | Máximo de Mr. Meeseeks por solicitud (plan de contingencia) | 3000 |
| `-a` | Máximo de Mr. Meeseeks trabajando a la vez (semáforo contador) | 128 |
| `-g` | Ganancia de dilución de la dificultad | 0.5 |
| `-s` | Semilla del generador aleatorio | tiempo |
| `-q` | Modo silencioso (solo mensajes de la Box) | |
| `-x` | Modo experimento: al vencer `-T` se anuncia el caos pero no se termina | |
| `-k` | Tras un caos, la Box sigue aceptando solicitudes | |
| `-o` | Agrega una fila CSV por solicitud | |

Solicitudes dentro de la Box:

* Texto libre (`Quiero ser feliz`): la Box pregunta dificultad (0 a 100, Enter = suerte) y duración.
* `calc <expresión>`: cálculo aritmético o lógico, p. ej. `calc (3+4)*2 >= 14 && !0`.
* `run <comando>`: ejecuta un programa externo y usa su código de salida.
* `adios`: cierra la Box e imprime el informe final.
