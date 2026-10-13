/* Atiende UNA solicitud: crea al Meeseeks raiz y espera su resultado.
 * Devuelve 1 si se decreto caos planetario y la Box debe cerrarse.       */
static int run_task(int kind, const char *text, double d, double dur)
{
    task_rec_t *R = &TASKS[NT < MAX_TASKS ? NT++ : MAX_TASKS - 1];
    memset(R, 0, sizeof *R);
    R->id = NT; R->kind = kind; R->chaos_at = -1.0;
    snprintf(R->text, sizeof R->text, "%s", text);
    R->d = d; R->d_random = (kind == K_TEXT && d < 0); R->dur_user = dur;

    shared_reset();
    sem_wait(&SH->slots);              /* la raiz ocupa un slot         */
    SH->created = 1;

    int down[2], up[2];
    if (pipe(down) < 0 || pipe(up) < 0) { perror("pipe"); return 0; }

    struct rusage r0, r1;
    getrusage(RUSAGE_CHILDREN, &r0);
    double t0 = now_s();

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 0; }
    if (pid == 0) {
        setpgid(0, 0);                 /* grupo propio: facilita el caos */
        signal(SIGINT, SIG_IGN);
        close(down[1]); close(up[0]);
        if (CFG.csv) { /* el hijo no debe tocar el FILE* de la Box */ }
        meeseeks_run(down[0], up[1], 1, 1);
        _exit(0);
    }
    setpgid(pid, pid);
    close(down[0]); close(up[1]);

    say("Boton presionado: Mr. Meeseeks raiz (pid %d) atendera la solicitud #%d [%s] \"%s\"",
        pid, R->id, kind_name(kind), text);

    msg_t m;
    memset(&m, 0, sizeof m);
    m.type = M_TASK; m.kind = kind; m.difficulty = d; m.duration = dur;
    snprintf(m.text, sizeof m.text, "%s", text);
    write_msg(down[1], &m);            /* mensaje privado via pipe        */

    msg_t res;
    int have = 0, chaos = 0, killed = 0;
    double t_res = 0;
    for (;;) {
        int to = -1;
        if (CFG.max_time > 0 && !chaos) {
            double rem = t0 + CFG.max_time - now_s();
            to = rem <= 0 ? 0 : (int)(rem * 1000.0) + 1;
        }
        struct pollfd p = { up[0], POLLIN, 0 };
        int r = poll(&p, 1, to);
        if (r < 0 && errno == EINTR) {
            if (INTERRUPTED) { killed = 1; break; }
            continue;
        }
        if (r == 0) {                                  /* vencio el tiempo */
            chaos = 1;
            R->chaos_at = now_s() - t0;
            say("*** CAOS PLANETARIO! *** La solicitud #%d supero el tiempo maximo de %.1f s con %ld Mr. Meeseeks creados.",
                R->id, CFG.max_time, SH->created);
            if (!CFG.experiment) { killed = 1; break; }
            say("Modo experimento (-x): no se termina; se mide cuanto tiempo adicional necesitan los Meeseeks.");
            continue;
        }
        int k = read_msg(up[0], &res);
        t_res = now_s();
        have = (k == 1);
        break;
    }

    if (killed) {
        say("Terminando el grupo de procesos %d (SIGTERM y luego SIGKILL)...", pid);
        kill(-pid, SIGTERM);
        usleep(300000);
        kill(-pid, SIGKILL);
        t_res = now_s();
    }

    int st;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) ;
    /* La Box es subreaper: recoge huerfanos que hayan quedado */
    if (killed) { while (waitpid(-1, NULL, 0) > 0 || errno == EINTR) ; }
    else        { while (waitpid(-1, NULL, WNOHANG) > 0) ; }
    getrusage(RUSAGE_CHILDREN, &r1);

    R->wall = t_res - t0;
    R->cpu  = (r1.ru_utime.tv_sec - r0.ru_utime.tv_sec) + (r1.ru_utime.tv_usec - r0.ru_utime.tv_usec) / 1e6
            + (r1.ru_stime.tv_sec - r0.ru_stime.tv_sec) + (r1.ru_stime.tv_usec - r0.ru_stime.tv_usec) / 1e6;
    R->created      = SH->created;
    R->max_alive    = SH->max_alive;
    R->max_level    = SH->max_level;
    R->failed_forks = SH->failed_forks;
    R->capped       = SH->capped;
    R->sum_sleep    = SH->sum_sleep;
    R->sum_active   = SH->sum_active;
    if (kind == K_TEXT) R->d = SH->root_d;

    if (have && (res.type == M_SOLVED || (res.type == M_RESULT && res.ok))) R->status = 1;
    else if (killed) R->status = 2;
    else R->status = 0;

    if (have && res.type == M_SOLVED) { R->solver = res.solver; R->sim = res.path_sleep; }
    else R->sim = SH->max_path_sleep;
    if (have && res.type == M_RESULT) { snprintf(R->result, sizeof R->result, "%s", res.text); R->solver = res.solver; }
    R->real = R->wall - R->sim;
    if (R->real < 0) R->real = 0;

    close(down[1]); close(up[0]);

    const char *est = R->status == 1 ? "COMPLETADA" : R->status == 2 ? "CAOS PLANETARIO" : "NO COMPLETADA";
    say("Solicitud #%d %s. Tiempo total %.3f s | espera simulada %.3f s | tiempo real %.3f s | CPU %.3f s",
        R->id, est, R->wall, R->sim, R->real, R->cpu);
    say("   Mr. Meeseeks creados: %ld | nivel maximo: %d | pico simultaneo: %ld%s",
        R->created, R->max_level, R->max_alive, R->capped ? " | se activo el tope de contingencia" : "");
    if (R->chaos_at >= 0 && R->status == 1)
        say("   Caos declarado a los %.1f s; los Meeseeks necesitaron %.3f s adicionales para cumplir.",
            R->chaos_at, R->wall - CFG.max_time);
    if (R->result[0]) say("   Resultado: %s", R->result);
    csv_row(R);

    return killed && !CFG.keep_going;
}
