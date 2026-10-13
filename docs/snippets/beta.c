/* Beta(4,2) por estadisticos de orden: el 4.o menor de 5 uniformes.
 * Media 2/3: la mayoria de las solicitudes cotidianas son mas bien faciles,
 * pero la cola izquierda deja espacio para tareas muy dificiles.          */
static double beta_4_2(void)
{
    double u[5];
    for (int k = 0; k < 5; k++) u[k] = urand();
    for (int a = 1; a < 5; a++)              /* insercion: 5 elementos */
        for (int b = a; b > 0 && u[b - 1] > u[b]; b--) { double t = u[b]; u[b] = u[b - 1]; u[b - 1] = t; }
    return u[3];
}
