/* Se registra en los contadores compartidos (seccion critica) */
static void register_self(void)
{
    sem_wait(&SH->mutex);
    SH->alive++;
    if (SH->alive > SH->max_alive) SH->max_alive = SH->alive;
    if (ME.N <= MAX_LEVELS) SH->level_count[ME.N]++;
    if (ME.N > SH->max_level) SH->max_level = ME.N;
    sem_post(&SH->mutex);
}
