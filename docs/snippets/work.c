/* Duerme "dur" segundos simulando trabajo, pero sigue escuchando su pipe:
 * si llega M_STOP (o el padre desaparece) devuelve 1 de inmediato.
 * Se usa ppoll() porque poll() solo acepta milisegundos enteros y
 * redondeaba hacia arriba duraciones muy cortas.                           */
static int work_or_stop(int in_fd, double dur)
{
    double start = now_s(), end = start + dur;
    for (;;) {
        double rem = end - now_s();
        if (rem <= 0) break;
        struct pollfd p = { in_fd, POLLIN, 0 };
        struct timespec to;                 /* ppoll: resolucion de ns */
        to.tv_sec  = (time_t)rem;
        to.tv_nsec = (long)((rem - (double)to.tv_sec) * 1e9);
        int r = ppoll(&p, 1, &to, NULL);
        if (r < 0 && errno == EINTR) continue;
        if (r > 0) {
            msg_t m;
            int k = read_msg(in_fd, &m);
            if (k <= 0 || m.type == M_STOP) { SLEPT += now_s() - start; return 1; }
        }
    }
    SLEPT += now_s() - start;
    return 0;
}
