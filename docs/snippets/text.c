static void handle_text(int in_fd, int out_fd, msg_t *t)
{
    /* Caso 2: la dificultad se deja a la suerte del Meeseeks creado */
    double d = t->difficulty;
    if (d < 0.0) {
        d = 100.0 * beta_4_2();
        say("Nadie me dijo que tan dificil es... lo dejo a mi suerte (Beta(4,2)): dificultad %.2f", d);
    }
    ME.d = d;
    if (ME.N == 1) { sem_wait(&SH->mutex); SH->root_d = d; sem_post(&SH->mutex); }

    /* Duracion de la simulacion: 0.5..5 s o la del usuario; con cada nivel
     * los Meeseeks se desesperan y tardan mas (mucho mas si d = 0).        */
    double base = t->duration > 0.0 ? t->duration : 0.5 + 4.5 * urand();
    double desp = (d <= 0.0) ? 1.0 + 0.5 * (ME.N - 1) : 1.0 + 0.1 * (ME.N - 1);
    double dur  = base * desp;

    say("Recibi por mi pipe la solicitud \"%s\" (dificultad %.2f). Ooooh, puedo hacerlo! Trabajando %.2f s...",
        t->text, d, dur);

    if (work_or_stop(in_fd, dur)) {
        say("Me avisan por el pipe que la solicitud ya fue cumplida. Adios! *puff*");
        vanish(0);
    }
    double path = t->path_sleep + dur;
    record_path(path);

    /* ----- Decision: lo resuelvo yo o pido ayuda ----- */
    int mine = 0;
    if (d > EASY_LIMIT) {
        mine = 1;
    } else if (d > 0.0) {
        double p = self_success_p(d), roll = urand();
        if (roll < p) {
            mine = 1;
            say("Tiro Meeseeks exitoso! (probabilidad %.4f, saque %.4f). Lo logre sin pedir ayuda.", p, roll);
        }
    }

    who_t me = ME;
    me.pid = getpid();
    me.ppid = getppid();

    if (mine) {
        who_t other;
        if (!claim_victory(me, path, &other)) {
            say("Lo logre, pero Mr. Meeseeks (pid %d, N=%d, i=%d) se me adelanto. Espero la orden de desaparecer.",
                other.pid, other.N, other.i);
            wait_for_stop(in_fd);
            say("Me avisan por el pipe que la solicitud ya fue cumplida. Adios! *puff*");
            vanish(0);
        }
        say("Solicitud completada: \"%s\"! Gracias por dejarme servirle. Adios! *puff*", t->text);
        if (ME.N == 1)
            say("INFORME FINAL: la solicitud \"%s\" fue resuelta por Mr. Meeseeks (pid %d, ppid %d, N=%d, i=%d).",
                t->text, me.pid, me.ppid, me.N, me.i);
        msg_t r;
        memset(&r, 0, sizeof r);
        r.type = M_SOLVED; r.kind = K_TEXT; r.solver = me; r.path_sleep = path; r.ok = 1;
        write_msg(out_fd, &r);
        vanish(0);
    }

    /* ----- Pedir ayuda: 5 ayudantes si d <= 45, 2 si 45 < d <= 85 ----- */
    int want = (d <= HARD_LIMIT) ? 5 : 2;
    if (d <= 0.0)
        say("Esta tarea es IMPOSIBLE (dificultad %.2f)! La existencia es dolor! Invoco %d Mr. Meeseeks mas (nivel %d)...",
            d, want, ME.N + 1);
    else
        say("Esto es mas dificil de lo que pense (dificultad %.2f). Necesito ayuda! Invoco %d Mr. Meeseeks (nivel %d)...",
            d, want, ME.N + 1);

    /* Deja de "trabajar" y pasa a esperar a sus ayudantes: libera su slot.
     * Sin esto, con tareas muy dificiles todos los procesos vivos terminan
     * siendo padres que esperan un slot que nadie libera (interbloqueo).   */
    sem_post(&SH->slots);
    HOLDS_SLOT = 0;

    int up[2];
    if (pipe(up) < 0) { say("No pude crear el pipe de mis ayudantes."); goto failed_noup; }

    kid_t kids[MAX_KIDS];
    int nk = 0, finished = 0, capped_said = 0;
    int result = 0;              /* 0 pendiente, 1 resuelto, 2 STOP, -1 fallo */
    msg_t win;
    memset(&win, 0, sizeof win);

    while (result == 0) {
        /* Crear ayudantes pendientes, si hay slot libre */
        if (nk < want) {
            double cd = dilute(d, want);
            int s = spawn_helper(t, cd, path, in_fd, out_fd, up, kids, nk);
            if (s == 1) { nk++; continue; }
            if (s < 0) {
                if (!capped_said) {
                    say("Contingencia: no puedo invocar mas Mr. Meeseeks (tope de %ld por solicitud o fork fallido).",
                        CFG.max_total);
                    capped_said = 1;
                }
                want = nk;
                if (nk == 0 || finished == nk) { result = -1; break; }
            }
        }

        struct pollfd p[2] = { { in_fd, POLLIN, 0 }, { up[0], POLLIN, 0 } };
        int r = poll(p, 2, nk < want ? 25 : -1);
        if (r < 0) { if (errno == EINTR) continue; break; }

        if (p[0].revents) {                       /* mensaje de mi padre */
            msg_t m;
            int k = read_msg(in_fd, &m);
            if (k <= 0 || m.type == M_STOP) { result = 2; break; }
        }
        if (p[1].revents & POLLIN) {              /* mensaje de un hijo  */
            msg_t m;
            if (read_msg(up[0], &m) == 1) {
                if (m.type == M_SOLVED) { win = m; result = 1; break; }
                if (m.type == M_FAILED) {
                    finished++;
                    for (int j = 0; j < nk; j++)
                        if (kids[j].alive && kids[j].pid == m.solver.pid) {
                            waitpid(kids[j].pid, NULL, 0);
                            kids[j].alive = 0;
                        }
                    if (nk == want && finished == nk) { result = -1; break; }
                }
            }
        }
    }

    if (result == 1) {
        say("Mi ayudante reporta que Mr. Meeseeks (pid %d, ppid %d, N=%d, i=%d) resolvio la solicitud! Aviso a mis hijos para que desaparezcan.",
            win.solver.pid, win.solver.ppid, win.solver.N, win.solver.i);
        stop_all_kids(kids, nk);
        say("Mi trabajo aqui termino. Gracias por permitirme servirle. Adios! *puff*");
        if (ME.N == 1)
            say("INFORME FINAL: la solicitud \"%s\" fue resuelta por Mr. Meeseeks (pid %d, ppid %d, N=%d, i=%d).",
                t->text, win.solver.pid, win.solver.ppid, win.solver.N, win.solver.i);
        win.type = M_SOLVED;
        write_msg(out_fd, &win);
        close(up[0]); close(up[1]);
        vanish(0);
    }
    if (result == 2) {
        stop_all_kids(kids, nk);
        say("Me avisan por el pipe que la solicitud ya fue cumplida. Adios! *puff*");
        close(up[0]); close(up[1]);
        vanish(0);
    }
    stop_all_kids(kids, nk);
    close(up[0]); close(up[1]);

failed_noup:
    say("Ninguno de mis ayudantes pudo con \"%s\". La existencia es dolor! Me rindo. *puff*", t->text);
    if (ME.N == 1)
        say("INFORME FINAL: la solicitud \"%s\" NO pudo completarse.", t->text);
    {
        msg_t f;
        memset(&f, 0, sizeof f);
        f.type = M_FAILED; f.kind = K_TEXT; f.solver = me; f.path_sleep = path;
        write_msg(out_fd, &f);
    }
    vanish(1);
}
