/* Se despide del mundo: actualiza estadisticas, libera su "slot" y muere.
 * Se usa _exit para no vaciar dos veces buffers de stdio heredados.        */
static void vanish(int code)
{
    double life = now_s() - BORN_AT;
    sem_wait(&SH->mutex);
    SH->alive--;
    SH->sum_sleep  += SLEPT;
    SH->sum_active += (life - SLEPT > 0 ? life - SLEPT : 0);
    sem_post(&SH->mutex);
    if (HOLDS_SLOT) sem_post(&SH->slots);
    _exit(code);
}
