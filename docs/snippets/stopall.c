/* Envia M_STOP por el pipe privado de cada hijo vivo y espera a que mueran */
static void stop_all_kids(kid_t *kids, int nk)
{
    msg_t s;
    memset(&s, 0, sizeof s);
    s.type = M_STOP;
    for (int j = 0; j < nk; j++)
        if (kids[j].alive) write_msg(kids[j].wfd, &s);   /* EPIPE se ignora */
    for (int j = 0; j < nk; j++) {
        if (kids[j].alive) {
            while (waitpid(kids[j].pid, NULL, 0) < 0 && errno == EINTR) ;
            kids[j].alive = 0;
        }
        close(kids[j].wfd);
    }
}
