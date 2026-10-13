/* Region compartida entre TODOS los procesos (contadores, no mensajes) */
typedef struct {
    sem_t  mutex;                     /* protege todo lo de abajo           */
    sem_t  slots;                     /* Meeseeks vivos permitidos          */
    sem_t  out;                       /* serializa la salida en pantalla    */
    long   created;                   /* Meeseeks creados en esta solicitud */
    long   alive;                     /* Meeseeks vivos ahora               */
    long   max_alive;                 /* pico de Meeseeks vivos             */
    long   failed_forks;              /* fork() que fallaron                */
    long   capped;                    /* veces que se toco el tope total    */
    int    max_level;                 /* nivel N mas profundo alcanzado     */
    long   level_count[MAX_LEVELS + 1];/* instancias creadas por nivel      */
    double sum_sleep;                 /* suma de esperas simuladas          */
    double sum_active;                /* suma de tiempo real sin esperas    */
    double max_path_sleep;            /* espera simulada del camino mas largo*/
    double root_d;                    /* dificultad efectiva de la raiz     */
    int    solved;                    /* 1 si alguien resolvio              */
    who_t  solver;                    /* datos del que resolvio             */
    double solver_path_sleep;         /* espera simulada en su camino       */
} shared_t;
