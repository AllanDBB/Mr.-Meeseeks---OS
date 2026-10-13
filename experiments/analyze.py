#!/usr/bin/env python3
"""Analisis de los experimentos 0x02: genera figuras (docs/figs/*.pdf) y
tablas LaTeX (docs/tablas/*.tex) directamente desde los CSV de la Box."""
import os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "experiments", "data")
FIGS = os.path.join(ROOT, "docs", "figs")
TABS = os.path.join(ROOT, "docs", "tablas")
os.makedirs(FIGS, exist_ok=True)
os.makedirs(TABS, exist_ok=True)

C1, C2, C3, C4 = "#2a78d6", "#eb6834", "#1baf7a", "#eda100"   # orden categorico fijo
INK, INK2, GRID = "#0b0b0b", "#52514e", "#e4e3df"
plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 9, "axes.edgecolor": INK2,
    "axes.labelcolor": INK, "xtick.color": INK2, "ytick.color": INK2,
    "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True,
    "grid.color": GRID, "grid.linewidth": 0.6, "axes.axisbelow": True,
    "lines.linewidth": 2, "legend.frameon": False, "figure.dpi": 150,
})


def load(name):
    p = os.path.join(DATA, name)
    return pd.read_csv(p) if os.path.exists(p) else None


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(FIGS, name), bbox_inches="tight")
    plt.close(fig)


def fmt(x, nd=3):
    return f"{x:.{nd}f}"


def write_tab(name, header, rows, align):
    with open(os.path.join(TABS, name), "w") as f:
        f.write("\\begin{tabular}{%s}\n\\toprule\n" % align)
        f.write(" & ".join(header) + " \\\\\n\\midrule\n")
        for r in rows:
            f.write(" & ".join(str(c) for c in r) + " \\\\\n")
        f.write("\\bottomrule\n\\end{tabular}\n")


def q(s, p):
    return float(np.percentile(s, p))


summary = {}

# --------------------------------------------------------------------- E1
e1 = load("e1.csv")
if e1 is not None:
    g = e1.groupby("dificultad")
    t = g.agg(n=("id", "size"), ok=("estado", lambda s: (s == 1).mean()),
              mee=("meeseeks", "median"), mee25=("meeseeks", lambda s: q(s, 25)),
              mee75=("meeseeks", lambda s: q(s, 75)), mmax=("meeseeks", "max"),
              niv=("nivel_max", "median"), pico=("pico_vivos", "median"),
              tt=("t_total", "median"), ts=("t_simulado", "median"),
              tr=("t_real", "median"), cpu=("cpu", "median")).sort_index(ascending=False)
    rows = []
    for d, r in t.iterrows():
        rows.append([f"{d:g}", f"{100*r.ok:.0f}\\,\\%", f"{r.mee:.0f}", f"{r.mee25:.0f}--{r.mee75:.0f}",
                     f"{r.mmax:.0f}", f"{r.niv:.0f}", fmt(r.tt), fmt(r.ts, 2), fmt(r.tr, 4), fmt(r.cpu, 4)])
    write_tab("e1.tex", ["$d$", "Éxito", "Meeseeks", "RIC", "Máx.", "Niv.", "$T_{total}$", "$T_{sim}$",
                         "$T_{real}$", "CPU"], rows, "rrrrrrrrrr")

    pos = t[t.index > 0]
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.9))
    ax = axes[0]
    ax.fill_between(pos.index, pos.mee25, pos.mee75, color=C1, alpha=0.18, lw=0)
    ax.plot(pos.index, pos.mee, color=C1, marker="o", ms=4)
    ax.axhline(3000, color=C2, lw=1.2, ls="--")
    ax.text(0.012, 3000 * 0.55, "tope -m 3000 (d = 0 siempre lo agota)", color=INK2, fontsize=7.5)
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("dificultad d (escala log)"); ax.set_ylabel("Mr. Meeseeks creados")
    ax.set_title("Meeseeks por solicitud (mediana y RIC)", fontsize=9, color=INK)
    ax = axes[1]
    ax.plot(pos.index, pos.niv, color=C1, marker="o", ms=4)
    ax.set_xscale("log")
    ax.set_xlabel("dificultad d (escala log)"); ax.set_ylabel("nivel máximo N (mediana)")
    ax.set_title("Profundidad del árbol", fontsize=9, color=INK)
    save(fig, "e1_costo.pdf")

    # Tiempo real vs Meeseeks creados: ajuste solo con solicitudes resueltas
    ok = e1[e1.estado == 1]
    imp = e1[e1.estado != 1]
    x, y = ok.meeseeks.values, ok.t_real.values
    m, b = np.polyfit(x, y, 1)
    r2 = np.corrcoef(x, y)[0, 1] ** 2
    mc, bc = np.polyfit(ok.meeseeks.values, ok.cpu.values, 1)
    summary["e1_fit"] = (m, b, r2, mc)
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.9))
    ax = axes[0]
    ax.scatter(x, y * 1000, s=10, color=C1, alpha=0.55, lw=0, label="solicitudes resueltas (d > 0)")
    xs = np.linspace(0, x.max(), 50)
    ax.plot(xs, (m * xs + b) * 1000, color=C2, lw=1.5, label=f"ajuste: {m*1e6:.0f} µs por Meeseeks (R² = {r2:.2f})")
    ax.set_xlabel("Mr. Meeseeks creados"); ax.set_ylabel("T real (ms)")
    ax.set_title("Tareas posibles", fontsize=9)
    ax.legend(fontsize=7, loc="upper left")
    ax = axes[1]
    ax.scatter(ok.meeseeks, ok.cpu, s=10, color=C1, alpha=0.55, lw=0, label="d > 0")
    ax.scatter(imp.meeseeks, imp.cpu, s=16, color=C2, alpha=0.8, lw=0, label="d = 0 (tope 3000)")
    ax.set_xlabel("Mr. Meeseeks creados"); ax.set_ylabel("CPU de los descendientes (s)")
    ax.set_title("Costo de CPU", fontsize=9)
    ax.legend(fontsize=7, loc="upper left")
    save(fig, "e1_overhead.pdf")
    summary["e1_imp"] = (imp.t_real.median(), imp.cpu.median(), imp.t_total.median())
    with open(os.path.join(TABS, "e1_fit.tex"), "w") as f:
        f.write("\\newcommand{\\EfitUs}{%.0f}\\newcommand{\\EfitR}{%.2f}\\newcommand{\\EfitB}{%.2f}"
                "\\newcommand{\\EfitCpuUs}{%.0f}\n" % (m * 1e6, r2, b * 1000, mc * 1e6))

# --------------------------------------------------------------------- E2
e2 = load("e2.csv")
if e2 is not None:
    e2["modo"] = np.where(e2.dur_usuario > 0, "fija 2.75 s", "aleatoria 0.5-5 s")
    e2["d"] = e2.solicitud.str.extract(r"d=(\d+)").astype(float)
    rows = []
    for d in sorted(e2.d.unique(), reverse=True):
        for mo in ["aleatoria 0.5-5 s", "fija 2.75 s"]:
            s = e2[(e2.d == d) & (e2.modo == mo)]
            rows.append([f"{d:g}", mo, f"{s.t_total.mean():.2f} $\\pm$ {s.t_total.std():.2f}",
                         f"{s.t_simulado.mean():.2f}", f"{1000*s.t_real.mean():.1f} $\\pm$ {1000*s.t_real.std():.1f}",
                         f"{s.meeseeks.median():.0f}", f"{s.nivel_max.median():.0f}"])
    write_tab("e2.tex", ["$d$", "Duración", "$T_{total}$ (s)", "$T_{sim}$ (s)", "$T_{real}$ (ms)",
                         "Meeseeks", "Niv."], rows, "rlrrrrr")
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.8))
    ds = sorted(e2.d.unique(), reverse=True)
    for k, (col, lab, sc) in enumerate([("t_total", "T total (s)", 1), ("t_real", "T real (ms)", 1000)]):
        ax = axes[k]
        data, posx, cols = [], [], []
        for j, d in enumerate(ds):
            for h, (mo, c) in enumerate([("aleatoria 0.5-5 s", C1), ("fija 2.75 s", C2)]):
                data.append(e2[(e2.d == d) & (e2.modo == mo)][col].values * sc)
                posx.append(j * 3 + h)
                cols.append(c)
        bp = ax.boxplot(data, positions=posx, widths=0.7, patch_artist=True, showfliers=True,
                        medianprops=dict(color=INK, lw=1.2), flierprops=dict(ms=3))
        for patch, c in zip(bp["boxes"], cols):
            patch.set_facecolor(c); patch.set_alpha(0.35); patch.set_edgecolor(c)
        ax.set_xticks([j * 3 + 0.5 for j in range(len(ds))])
        ax.set_xticklabels([f"d = {d:g}" for d in ds])
        ax.set_ylabel(lab)
    axes[0].plot([], [], color=C1, lw=6, alpha=0.35, label="duración aleatoria 0.5-5 s")
    axes[0].plot([], [], color=C2, lw=6, alpha=0.35, label="duración fija 2.75 s")
    axes[0].legend(fontsize=7.5, loc="upper left")
    save(fig, "e2_aleatorio.pdf")

# --------------------------------------------------------------------- E3
e3 = load("e3.csv")
if e3 is not None:
    def grupo(r):
        s = r.solicitud
        if r.tipo == "texto": return "A: texto d=100 (1 Meeseeks)"
        if r.tipo == "calc": return "B: calc (error)" if s.strip() == "1/0" else "B: calc (válido)"
        if s.startswith("true"): return "C: run true"
        if s.startswith("false"): return "C: run false"
        if s.startswith("ls"): return "C: run ls /usr/bin"
        if s.startswith("sleep"): return "C: run sleep 0.25"
        return "otro"
    e3["grupo"] = e3.apply(grupo, axis=1)
    order = ["A: texto d=100 (1 Meeseeks)", "B: calc (válido)", "B: calc (error)", "C: run true",
             "C: run false", "C: run ls /usr/bin", "C: run sleep 0.25"]
    rows = []
    for gname in order:
        s = e3[e3.grupo == gname]
        if s.empty: continue
        tr = s.t_real * 1000
        rows.append([gname.replace("_", "\\_"), len(s), f"{100*(s.estado==1).mean():.0f}\\,\\%",
                     f"{tr.mean():.3f}", f"{tr.median():.3f}", f"{q(tr,95):.3f}", f"{1000*s.cpu.mean():.3f}"])
    write_tab("e3.tex", ["Caso", "n", "Éxito", "Media (ms)", "Mediana (ms)", "P95 (ms)", "CPU (ms)"],
              rows, "lrrrrrr")
    fig, ax = plt.subplots(figsize=(7.0, 2.9))
    data = [e3[e3.grupo == gname].t_real.values * 1000 for gname in order]
    colors = [C1, C2, C2, C3, C3, C3, C3]
    bp = ax.boxplot(data, orientation="horizontal", patch_artist=True, widths=0.6,
                    medianprops=dict(color=INK, lw=1.2), flierprops=dict(ms=2.5))
    for patch, c in zip(bp["boxes"], colors):
        patch.set_facecolor(c); patch.set_alpha(0.35); patch.set_edgecolor(c)
    ax.set_yticks(range(1, len(order) + 1)); ax.set_yticklabels(order)
    ax.invert_yaxis(); ax.set_xscale("log"); ax.set_xlabel("T real (ms, escala log)")
    save(fig, "e3_casos.pdf")

# --------------------------------------------------------------------- E4
e4a = load("e4a.csv")
if e4a is not None:
    e4a["a"] = e4a.solicitud.str.extract(r"a=(\d+)").astype(int)
    g = e4a.groupby("a").agg(tt=("t_total", "median"), tt25=("t_total", lambda s: q(s, 25)),
                             tt75=("t_total", lambda s: q(s, 75)), tr=("t_real", "median"),
                             mee=("meeseeks", "median"), pico=("pico_vivos", "median"),
                             niv=("nivel_max", "median"), cpu=("cpu", "median"))
    rows = [[a, fmt(r.tt), fmt(r.tr * 1000, 1), f"{r.mee:.0f}", f"{r.pico:.0f}", f"{r.niv:.0f}", fmt(r.cpu * 1000, 1)]
            for a, r in g.iterrows()]
    write_tab("e4a.tex", ["Slots (\\texttt{-a})", "$T_{total}$ (s)", "$T_{real}$ (ms)", "Meeseeks", "Pico vivos",
                          "Niv.", "CPU (ms)"], rows, "rrrrrrr")
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.8))
    ax = axes[0]
    ax.fill_between(g.index, g.tt25, g.tt75, color=C1, alpha=0.18, lw=0)
    ax.plot(g.index, g.tt, color=C1, marker="o", ms=4)
    ax.set_xscale("log", base=2); ax.set_xlabel("slots de trabajo (-a)"); ax.set_ylabel("T total (s)")
    ax.set_title("Tiempo para resolver d = 10", fontsize=9)
    ax = axes[1]
    ax.plot(g.index, g.mee, color=C1, marker="o", ms=4, label="Meeseeks creados")
    ax.plot(g.index, g.pico, color=C2, marker="s", ms=4, label="pico de procesos vivos")
    ax.set_xscale("log", base=2); ax.set_xlabel("slots de trabajo (-a)"); ax.set_ylabel("procesos")
    ax.legend(fontsize=7.5)
    ax.set_title("Procesos en la solicitud", fontsize=9)
    save(fig, "e4a_slots.pdf")

e4b = load("e4b.csv")
if e4b is not None:
    e4b["m"] = e4b.solicitud.str.extract(r"m=(\d+)").astype(int)
    g = e4b.groupby("m").agg(tt=("t_total", "median"), tr=("t_real", "median"), mee=("meeseeks", "median"),
                             pico=("pico_vivos", "median"), niv=("nivel_max", "median"), cpu=("cpu", "median"),
                             ok=("estado", lambda s: (s == 1).mean()))
    rows = [[m, f"{100*r.ok:.0f}\\,\\%", fmt(r.tt, 2), fmt(r.tr, 3), f"{r.mee:.0f}", f"{r.pico:.0f}", f"{r.niv:.0f}",
             fmt(r.cpu, 2)] for m, r in g.iterrows()]
    write_tab("e4b.tex", ["Tope (\\texttt{-m})", "Éxito", "$T_{total}$ (s)", "$T_{real}$ (s)", "Meeseeks",
                          "Pico vivos", "Niv.", "CPU (s)"], rows, "rrrrrrrr")
    fig, ax = plt.subplots(figsize=(4.6, 2.8))
    ax.plot(g.index, g.tt, color=C1, marker="o", ms=4, label="T total hasta rendirse")
    ax.set_xlabel("tope de Meeseeks (-m)"); ax.set_ylabel("segundos")
    ax.set_title("Tarea imposible (d = 0): tiempo hasta rendirse", fontsize=9)
    save(fig, "e4b_tope.pdf")

# --------------------------------------------------------------------- E5
e5 = load("e5.csv")
if e5 is not None:
    e5["g"] = e5.ganancia
    e5["d"] = e5.solicitud.str.extract(r"d=(\d+)").astype(float)
    rows = []
    for d in sorted(e5.d.unique(), reverse=True):
        for gg in sorted(e5.g.unique()):
            s = e5[(e5.d == d) & (e5.g == gg)]
            rows.append([f"{d:g}", f"{gg:g}", f"{100*(s.estado==1).mean():.0f}\\,\\%", f"{s.meeseeks.median():.0f}",
                         f"{s.meeseeks.max():.0f}", f"{s.nivel_max.median():.0f}", fmt(s.t_total.median()),
                         fmt(1000 * s.t_real.median(), 1)])
    write_tab("e5.tex", ["$d$", "$g$", "Éxito", "Meeseeks (med.)", "Máx.", "Niv.", "$T_{total}$ (s)",
                         "$T_{real}$ (ms)"], rows, "rrrrrrrr")
    fig, ax = plt.subplots(figsize=(4.8, 2.9))
    for c, d in zip([C1, C2, C3], sorted(e5.d.unique(), reverse=True)):
        s = e5[e5.d == d].groupby("g").meeseeks.median()
        ax.plot(s.index, s.values, color=c, marker="o", ms=4, label=f"d = {d:g}")
    ax.set_yscale("log"); ax.set_xlabel("ganancia de dilución g"); ax.set_ylabel("Meeseeks creados (mediana)")
    ax.set_xticks(sorted(e5.g.unique()))
    ax.legend(fontsize=7.5)
    save(fig, "e5_ganancia.pdf")

# --------------------------------------------------------------------- E6
e6 = load("e6.csv")
if e6 is not None:
    e6["d"] = e6.dificultad
    rows = []
    for _, r in e6.sort_values(["d", "id"], ascending=[False, True]).iterrows():
        extra = r.t_total - r.max_time if r.caos_en >= 0 and r.estado == 1 else 0.0
        rows.append([f"{r.d:g}", "sí" if r.estado == 1 else "no", fmt(r.t_total, 1), fmt(extra, 1),
                     f"{r.meeseeks:.0f}", f"{r.nivel_max:.0f}", f"{r.pico_vivos:.0f}", fmt(r.cpu, 2)])
    write_tab("e6.tex", ["$d$", "Resuelta", "$T_{total}$ (s)", "Extra sobre 30 s", "Meeseeks", "Niv.",
                         "Pico vivos", "CPU (s)"], rows, "rrrrrrrr")
    g = e6.groupby("d").agg(tt=("t_total", "mean"), tmin=("t_total", "min"), tmax=("t_total", "max"),
                            mee=("meeseeks", "mean"))
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.8))
    ax = axes[0]
    ax.scatter(e6.d, e6.t_total, color=C1, s=14, zorder=3, label="corridas")
    ax.plot(g.index, g.tt, color=C1, lw=1.5)
    ax.axhline(30, color=C2, ls="--", lw=1.2)
    ax.text(g.index.min(), 31.5, "tiempo máximo (-T 30)", color=INK2, fontsize=7.5)
    ax.set_xscale("log"); ax.set_xlabel("dificultad d (escala log)"); ax.set_ylabel("T total (s)")
    ax.invert_xaxis()
    ax = axes[1]
    ax.scatter(e6.d, e6.meeseeks, color=C1, s=14, zorder=3)
    ax.plot(g.index, g.mee, color=C1, lw=1.5)
    ax.set_xscale("log"); ax.set_xlabel("dificultad d (escala log)"); ax.set_ylabel("Meeseeks creados")
    ax.invert_xaxis()
    save(fig, "e6_extra.pdf")

# --------------------------------------------------------------------- E7
e7 = load("e7.csv")
if e7 is not None:
    rows = []
    for _, r in e7.iterrows():
        est = {1: "completada", 0: "no completada", 2: "caos planetario"}[int(r.estado)]
        extra = r.t_total - r.max_time if r.caos_en >= 0 and r.estado == 1 else float("nan")
        rows.append([r.solicitud, f"{r.dificultad:g}", est, fmt(r.t_total, 1),
                     "--" if np.isnan(extra) else fmt(extra, 1), f"{r.meeseeks:.0f}", f"{r.nivel_max:.0f}",
                     f"{r.pico_vivos:.0f}", fmt(r.cpu, 2)])
    write_tab("e7.tex", ["Solicitud", "$d$", "Estado", "$T_{total}$ (s)", "Extra (s)", "Meeseeks", "Niv.",
                         "Pico", "CPU (s)"], rows, "lrlrrrrrr")

print("listo:", sorted(os.listdir(FIGS)), sorted(os.listdir(TABS)))
if "e1_fit" in summary:
    m, b, r2, mc = summary["e1_fit"]
    print(f"E1 ajuste: {m*1e6:.1f} us/Meeseeks, b = {b*1000:.2f} ms, R2 = {r2:.3f}, CPU {mc*1e6:.1f} us/Meeseeks")
