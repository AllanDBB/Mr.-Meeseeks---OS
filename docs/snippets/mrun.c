static void meeseeks_run(int in_fd, int out_fd, int N, int i);   /* adelanto */

/* Intenta invocar un ayudante. Devuelve 1 si lo creo, 0 si no hay slot
 * libre en este momento (reintentar), -1 si se alcanzo el tope o fork fallo.*/
static int spawn_helper(const msg_t *task, double child_d, double path,
                        int in_fd, int out_fd, int up[2],
                        kid_t *kids, int nk)
{
    /* Contingencia 1: semaforo contador de Meeseeks vivos */
    if (sem_trywait(&SH->slots) != 0) return 0;

    /* Contingencia 2: tope total de Meeseeks por solicitud */
    sem_wait(&SH->mutex);
    if (CFG.max_total > 0 && SH->created >= CFG.max_total) {
        SH->capped++;
        sem_post(&SH->mutex);
        sem_post(&SH->slots);
        return -1;
    }
    SH->created++;
    sem_post(&SH->mutex);

    int down[2];
    if (pipe(down) < 0) goto fail;

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { close(down[0]); close(down[1]); goto fail; }

    if (pid == 0) {
        /* Hijo: cierra los descriptores que no le pertenecen */
        close(in_fd);
        close(out_fd);
        close(up[0]);
        close(down[1]);
        for (int j = 0; j < nk; j++) if (kids[j].alive) close(kids[j].wfd);
        meeseeks_run(down[0], up[1], ME.N + 1, nk + 1);   /* N+1, i = 1..k */
        _exit(0);
    }

    /* Padre: le pasa la solicitud por su pipe privado */
    close(down[0]);
    msg_t m = *task;
    m.type       = M_TASK;
    m.difficulty = child_d;
    m.path_sleep = path;
    write_msg(down[1], &m);
    kids[nk].pid = pid; kids[nk].wfd = down[1]; kids[nk].alive = 1;
    return 1;

fail:
    sem_wait(&SH->mutex);
    SH->created--;
    SH->failed_forks++;
    sem_post(&SH->mutex);
    sem_post(&SH->slots);
    return -1;
}
