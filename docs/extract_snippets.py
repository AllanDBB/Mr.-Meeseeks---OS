#!/usr/bin/env python3
"""Extrae fragmentos de src/meeseeks.c para el reporte (asi el codigo del PDF
siempre coincide con el codigo fuente). Genera docs/snippets/<nombre>.c y un
archivo lines.tex con la linea inicial de cada fragmento."""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "meeseeks.c")
OUT = os.path.join(ROOT, "docs", "snippets")
os.makedirs(OUT, exist_ok=True)
lines = open(SRC, encoding="utf-8").read().split("\n")
LINES = {}


def block_from(start_pat, name, include_comment=True, end_pat=r"^\}"):
    """Copia desde la linea que cumple start_pat hasta la primera linea que
    cumple end_pat (por defecto la llave de cierre en columna 0)."""
    rx = re.compile(start_pat)
    for i, l in enumerate(lines):
        if rx.search(l):
            s = i
            if include_comment:              # incluir comentario previo
                while s > 0 and (lines[s - 1].startswith("/*") or lines[s - 1].startswith(" *")
                                 or lines[s - 1].startswith(" */")):
                    s -= 1
            e = i
            erx = re.compile(end_pat)
            while not erx.search(lines[e]):
                e += 1
            open(os.path.join(OUT, name + ".c"), "w").write("\n".join(lines[s:e + 1]) + "\n")
            LINES[name] = s + 1
            return
    sys.exit(f"no encontrado: {start_pat}")


block_from(r"^typedef struct \{$", "msg_who", end_pat=r"^\} msg_t;")
block_from(r"^/\* Region compartida", "shared", include_comment=False, end_pat=r"^\} shared_t;")
block_from(r"^static double beta_4_2", "beta")
block_from(r"^static void emit\(", "emit")
block_from(r"^static void register_self", "register")
block_from(r"^static void vanish", "vanish")
block_from(r"^static int work_or_stop", "work")
block_from(r"^static double dilute", "dilute")
block_from(r"^static double self_success_p", "selfp")
block_from(r"^static int spawn_helper", "spawn")
block_from(r"^static int claim_victory", "claim")
block_from(r"^static void stop_all_kids", "stopall")
block_from(r"^static void handle_text", "text")
block_from(r"^static double p_mul", "pmul")
block_from(r"^static int evaluate", "evaluate")
block_from(r"^static void handle_run", "run")
block_from(r"^static void meeseeks_run", "mrun")
block_from(r"^static void shared_reset", "reset")
block_from(r"^static int run_task", "runtask")
with open(os.path.join(OUT, "lines.tex"), "w") as f:
    for k, v in LINES.items():
        f.write("\\expandafter\\def\\csname line@%s\\endcsname{%d}\n" % (k, v))
print("fragmentos en", OUT)
