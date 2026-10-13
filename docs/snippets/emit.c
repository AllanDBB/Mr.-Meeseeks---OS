/* ------------------------------------------------------------------------ */
/*  Salida: cada linea se arma completa y se escribe con un solo write(),    */
/*  protegida por el semaforo "out". Siempre incluye pid y ppid.             */
/* ------------------------------------------------------------------------ */
static void emit(const char *buf, size_t n)
{
    struct timespec dl;
    int locked = 0;
    if (SH) {
        clock_gettime(CLOCK_REALTIME, &dl);
        dl.tv_nsec += 200000000L;                 /* espera maxima 200 ms */
        if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
        locked = (sem_timedwait(&SH->out, &dl) == 0);
    }
    ssize_t w = write(STDOUT_FILENO, buf, n);
    (void)w;
    if (locked) sem_post(&SH->out);
}
