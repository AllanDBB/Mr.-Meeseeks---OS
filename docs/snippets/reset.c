static void shared_reset(void)
{
    if (SEM_READY) {
        sem_destroy(&SH->mutex);
        sem_destroy(&SH->slots);
        sem_destroy(&SH->out);
    }
    memset(SH, 0, sizeof *SH);
    sem_init(&SH->mutex, 1, 1);                 /* pshared = 1: entre procesos */
    sem_init(&SH->slots, 1, (unsigned)CFG.max_alive);
    sem_init(&SH->out,   1, 1);
    SEM_READY = 1;
}
