/* Identidad de un Meeseeks: (pid, ppid, N, i) */
typedef struct {
    pid_t  pid, ppid;
    int    N, i;
    double d;            /* dificultad con la que trabajo */
} who_t;

/* Mensaje de tamano fijo (< PIPE_BUF = 4096 bytes => escritura atomica) */
typedef struct {
    int    type;          /* mtype_t                                         */
    int    kind;          /* kind_t                                          */
    double difficulty;    /* dificultad (ya diluida); < 0 = "a la suerte"    */
    double duration;      /* duracion base definida por el usuario (<=0 rand)*/
    double path_sleep;    /* espera simulada acumulada desde la raiz         */
    int    ok;            /* M_RESULT: 1 exito, 0 fallo                      */
    who_t  solver;        /* quien resolvio / quien envia                    */
    char   text[MAX_TEXT];/* solicitud (M_TASK) o resultado (M_RESULT)       */
} msg_t;
