/* Reclama la victoria en la memoria compartida (seccion critica).
 * Solo el primero gana; devuelve 1 si este Meeseeks es el ganador.
 * Si otro ya gano, *other recibe sus datos.                               */
static int claim_victory(who_t w, double path, who_t *other)
{
    int won = 0;
    sem_wait(&SH->mutex);
    if (!SH->solved) { SH->solved = 1; SH->solver = w; SH->solver_path_sleep = path; won = 1; }
    else if (other) *other = SH->solver;
    sem_post(&SH->mutex);
    return won;
}
