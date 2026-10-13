/* Duerme "dur" segundos simulando trabajo, pero sigue escuchando su pipe:
 * si llega M_STOP (o el padre desaparece) devuelve 1 de inmediato.         */
static int work_or_stop(int in_fd, double dur)
{
    double start = now_s(), end = start + dur;
    for (;;) {
        double rem = end - now_s();
        if (rem <= 0) break;
        struct pollfd p = { in_fd, POLLIN, 0 };
        int r = poll(&p, 1, (int)(rem * 1000.0) + 1);
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
