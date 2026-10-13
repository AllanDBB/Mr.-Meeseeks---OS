/*
 * ============================================================================
 *  Archivo     : meeseeks.c
 *  Proyecto    : Tarea 3 - Box de Mr. Meeseeks
 *  Curso       : IC-6600 Sistemas Operativos, II Semestre 2026
 *  Institucion : Instituto Tecnologico de Costa Rica, sede Cartago
 *  Escuela     : Ingenieria en Computadores
 *  Profesor    : M.Sc. Esteban Arias-Mendez
 *  Estudiante  : Nicolas Florez Jimenez
 *  Carne       : [completar]
 *  Fecha       : Octubre 2026
 *  Lenguaje    : C (gnu11) sobre Linux
 *  Compilacion : gcc -Wall -Wextra -O2 -std=gnu11 -o meeseeks meeseeks.c -pthread -lm
 * ----------------------------------------------------------------------------
 *  Descripcion general
 *
 *  Un unico programa que implementa la "Caja de Mr. Meeseeks". El proceso
 *  original (la Box) lee solicitudes del usuario y, por cada una, crea con
 *  fork() un Mr. Meeseeks raiz (N=1, i=1). Todos los procesos ejecutan este
 *  mismo codigo.
 *
 *  Tipos de solicitud:
 *    A. Texto libre  -> simulacion con dificultad 0..100 y duracion aleatoria;
 *                       si la tarea es dificil el Meeseeks invoca ayudantes
 *                       (arbol de procesos) y la dificultad se diluye.
 *    B. calc <expr>  -> un solo Meeseeks evalua una expresion aritmetica o
 *                       logica con un analizador recursivo descendente.
 *    C. run <cmd>    -> un solo Meeseeks ejecuta un programa externo y usa su
 *                       codigo de salida (0 = exito).
 *
 *  Mecanismos de IPC y sincronizacion:
 *    - Pipes: cada Meeseeks recibe la solicitud por un pipe privado de su
 *      padre (pipe "down") y reporta resultados por un pipe compartido entre
 *      hermanos hacia el padre (pipe "up"). Mensajes de tamano fijo menores
 *      que PIPE_BUF, por lo que cada write() es atomico.
 *    - Memoria compartida (mmap MAP_SHARED|MAP_ANONYMOUS) con contadores de
 *      niveles, instancias y estadisticas. NO se usa para pasar mensajes.
 *    - Semaforos POSIX (sem_t, pshared=1) administrados por el kernel:
 *        mutex : exclusion mutua sobre los contadores compartidos.
 *        slots : semaforo contador que limita los Meeseeks TRABAJANDO a
 *                la vez (contingencia contra la explosion de procesos);
 *                el total de procesos lo acota el tope -m por solicitud.
 *        out   : serializa las lineas que se imprimen en pantalla.
 *    - Senales: la Box es "subreaper" y, si vence el tiempo maximo, decreta
 *      el caos planetario y termina el grupo de procesos de la solicitud.
 * ============================================================================
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <poll.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------------ */
/*  Constantes                                                               */
/* ------------------------------------------------------------------------ */
#define MAX_TEXT     256   /* largo maximo de una solicitud o resultado      */
#define MAX_LEVELS   64    /* niveles registrados en el contador compartido  */
#define MAX_TASKS    256   /* solicitudes que recuerda la Box para el informe*/
#define MAX_KIDS     8     /* ayudantes maximos por Meeseeks                 */

#define EASY_LIMIT   85.0  /* (85, 100]  : lo resuelve un solo Meeseeks      */
#define HARD_LIMIT   45.0  /* [0, 45]    : pide 5 ayudantes; (45,85]: pide 2 */

/* Tipo de solicitud */
typedef enum { K_TEXT = 1, K_CALC = 2, K_RUN = 3 } kind_t;

/* Tipo de mensaje que viaja por los pipes */
typedef enum {
    M_TASK = 1,   /* padre -> hijo : solicitud a atender                    */
    M_STOP,       /* padre -> hijo : la tarea ya se resolvio, desaparece     */
    M_SOLVED,     /* hijo -> padre : la tarea fue resuelta (por mi o mi rama)*/
    M_FAILED,     /* hijo -> padre : mi rama no pudo (contingencia)          */
    M_RESULT      /* Meeseeks de calc/run -> Box : resultado del trabajo     */
} mtype_t;

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

/* Configuracion (solo lectura despues de arrancar; se copia con fork) */
static struct {
    double        max_time;   /* -T: segundos antes del caos (0 = infinito) */
    long          max_total;  /* -m: tope de Meeseeks por solicitud         */
    int           max_alive;  /* -a: Meeseeks vivos simultaneos             */
    double        gain;       /* -g: ganancia de dilucion                   */
    int           quiet;      /* -q                                         */
    int           experiment; /* -x: no terminar al vencer el tiempo        */
    int           keep_going; /* -k: seguir despues de un caos              */
    unsigned long seed;       /* -s                                         */
    int           seeded;
    FILE         *csv;        /* -o                                         */
} CFG = { 300.0, 3000, 128, 0.5, 0, 0, 0, 0, 0, NULL };

static shared_t *SH = NULL;      /* puntero a la memoria compartida        */
static who_t     ME;             /* identidad de este proceso              */
static int       IS_BOX = 1;     /* 1 en el proceso Box, 0 en los Meeseeks */

/* ------------------------------------------------------------------------ */
/*  Utilidades de tiempo                                                     */
/* ------------------------------------------------------------------------ */
static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ------------------------------------------------------------------------ */
/*  Generador aleatorio por proceso (xorshift64*)                            */
/*  Cada Meeseeks lo re-siembra al nacer para no repetir la secuencia del    */
/*  padre (fork copia el estado).                                            */
/* ------------------------------------------------------------------------ */
static unsigned long long RNG = 0x9E3779B97F4A7C15ULL;

static void rng_seed(unsigned long long s)
{
    RNG = s ? s : 0x9E3779B97F4A7C15ULL;
    for (int k = 0; k < 8; k++) { RNG ^= RNG >> 12; RNG ^= RNG << 25; RNG ^= RNG >> 27; }
}

static double urand(void)                     /* uniforme en [0,1) */
{
    RNG ^= RNG >> 12; RNG ^= RNG << 25; RNG ^= RNG >> 27;
    return ((RNG * 0x2545F4914F6CDD1DULL) >> 11) * (1.0 / 9007199254740992.0);
}

static double gauss(void)                     /* normal estandar (Box-Muller) */
{
    double u1 = urand(), u2 = urand();
    if (u1 < 1e-300) u1 = 1e-300;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

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

/* Mensaje de un Meeseeks (se suprime con -q) */
static void say(const char *fmt, ...)
{
    if (CFG.quiet && !IS_BOX) return;
    char buf[1024];
    int n;
    if (IS_BOX)
        n = snprintf(buf, sizeof buf, "[Box    pid %d | ppid %d] ", getpid(), getppid());
    else
        n = snprintf(buf, sizeof buf, "[pid %d | ppid %d | N=%d i=%d] ",
                     getpid(), getppid(), ME.N, ME.i);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    emit(buf, n);
}

/* ------------------------------------------------------------------------ */
/*  E/S robusta sobre pipes                                                  */
/* ------------------------------------------------------------------------ */
/* Devuelve 1 si leyo un mensaje completo, 0 si EOF, -1 si error */
static int read_msg(int fd, msg_t *m)
{
    size_t got = 0;
    while (got < sizeof *m) {
        ssize_t r = read(fd, (char *)m + got, sizeof *m - got);
        if (r == 0) return got == 0 ? 0 : -1;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        got += (size_t)r;
    }
    return 1;
}

static int write_msg(int fd, const msg_t *m)
{
    size_t put = 0;
    while (put < sizeof *m) {
        ssize_t w = write(fd, (const char *)m + put, sizeof *m - put);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        put += (size_t)w;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  Ciclo de vida compartido de un Meeseeks                                  */
/* ------------------------------------------------------------------------ */
static double BORN_AT;           /* instante en que nacio este Meeseeks    */
static double SLEPT;             /* espera simulada que hizo este Meeseeks */
static int    HOLDS_SLOT;        /* 1 mientras ocupa un slot de trabajo    */

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

/* ------------------------------------------------------------------------ */
/*  A. Solicitudes de texto: dificultad, dilucion y ayudantes                */
/* ------------------------------------------------------------------------ */

/* "Ley de la Dilucion Meeseeks" (inspirada en el envejecimiento/aging):
 *   d' = [ d + r*d*(1 - d/100) ] * exp(0.08*Z),   r = g*sqrt(k)
 * Es un crecimiento logistico: mientras mas ayudantes (k) se reparten la
 * tarea, mas rapido sube la "facilidad" de cada parte. El ruido es
 * multiplicativo, por lo que d = 0 es un punto fijo: lo imposible jamas se
 * vuelve posible, sin importar cuantos Meeseeks se invoquen.              */
static double dilute(double d, int k)
{
    if (d <= 0.0) return 0.0;
    double r  = CFG.gain * sqrt((double)k);
    double dn = (d + r * d * (1.0 - d / 100.0)) * exp(0.08 * gauss());
    if (dn > 100.0) dn = 100.0;
    return dn;
}

/* "Tiro Meeseeks": probabilidad de que un Meeseeks, solo, logre una tarea
 * de dificultad d <= 85 sin pedir ayuda. Cubica: castiga mucho lo dificil. */
static double self_success_p(double d)
{
    double x = d / 100.0;
    return x * x * x;
}

typedef struct { pid_t pid; int wfd; int alive; } kid_t;

static void meeseeks_run(int in_fd, int out_fd, int N, int i);   /* adelanto */

/* Intenta invocar un ayudante. Devuelve 1 si lo creo, 0 si no hay slot
 * libre en este momento (reintentar), -1 si se alcanzo el tope o fork fallo.*/
static int spawn_helper(const msg_t *task, double child_d, double path,
                        int in_fd, int out_fd, int up[2],
                        kid_t *kids, int nk)
{
    /* Contingencia 1: semaforo contador de Meeseeks vivos */
    if (sem_trywait(&SH->slots) != 0) return 0;

    /* Contingencia 2: tope total de Meeseeks por solicitud */
    sem_wait(&SH->mutex);
    if (CFG.max_total > 0 && SH->created >= CFG.max_total) {
        SH->capped++;
        sem_post(&SH->mutex);
        sem_post(&SH->slots);
        return -1;
    }
    SH->created++;
    sem_post(&SH->mutex);

    int down[2];
    if (pipe(down) < 0) goto fail;

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { close(down[0]); close(down[1]); goto fail; }

    if (pid == 0) {
        /* Hijo: cierra los descriptores que no le pertenecen */
        close(in_fd);
        close(out_fd);
        close(up[0]);
        close(down[1]);
        for (int j = 0; j < nk; j++) if (kids[j].alive) close(kids[j].wfd);
        meeseeks_run(down[0], up[1], ME.N + 1, nk + 1);   /* N+1, i = 1..k */
        _exit(0);
    }

    /* Padre: le pasa la solicitud por su pipe privado */
    close(down[0]);
    msg_t m = *task;
    m.type       = M_TASK;
    m.difficulty = child_d;
    m.path_sleep = path;
    write_msg(down[1], &m);
    kids[nk].pid = pid; kids[nk].wfd = down[1]; kids[nk].alive = 1;
    return 1;

fail:
    sem_wait(&SH->mutex);
    SH->created--;
    SH->failed_forks++;
    sem_post(&SH->mutex);
    sem_post(&SH->slots);
    return -1;
}

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

/* Espera bloqueado en su pipe hasta recibir M_STOP (o EOF) */
static void wait_for_stop(int in_fd)
{
    msg_t m;
    for (;;) { int k = read_msg(in_fd, &m); if (k <= 0 || m.type == M_STOP) return; }
}

/* Lleva el maximo de espera simulada acumulada por cualquier camino */
static void record_path(double path)
{
    sem_wait(&SH->mutex);
    if (path > SH->max_path_sleep) SH->max_path_sleep = path;
    sem_post(&SH->mutex);
}

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

static void handle_text(int in_fd, int out_fd, msg_t *t)
{
    /* Caso 2: la dificultad se deja a la suerte del Meeseeks creado */
    double d = t->difficulty;
    if (d < 0.0) {
        d = 100.0 * beta_4_2();
        say("Nadie me dijo que tan dificil es... lo dejo a mi suerte (Beta(4,2)): dificultad %.2f", d);
    }
    ME.d = d;
    if (ME.N == 1) { sem_wait(&SH->mutex); SH->root_d = d; sem_post(&SH->mutex); }

    /* Duracion de la simulacion: 0.5..5 s o la del usuario; con cada nivel
     * los Meeseeks se desesperan y tardan mas (mucho mas si d = 0).        */
    double base = t->duration > 0.0 ? t->duration : 0.5 + 4.5 * urand();
    double desp = (d <= 0.0) ? 1.0 + 0.5 * (ME.N - 1) : 1.0 + 0.1 * (ME.N - 1);
    double dur  = base * desp;

    say("Recibi por mi pipe la solicitud \"%s\" (dificultad %.2f). Ooooh, puedo hacerlo! Trabajando %.2f s...",
        t->text, d, dur);

    if (work_or_stop(in_fd, dur)) {
        say("Me avisan por el pipe que la solicitud ya fue cumplida. Adios! *puff*");
        vanish(0);
    }
    double path = t->path_sleep + dur;
    record_path(path);

    /* ----- Decision: lo resuelvo yo o pido ayuda ----- */
    int mine = 0;
    if (d > EASY_LIMIT) {
        mine = 1;
    } else if (d > 0.0) {
        double p = self_success_p(d), roll = urand();
        if (roll < p) {
            mine = 1;
            say("Tiro Meeseeks exitoso! (probabilidad %.4f, saque %.4f). Lo logre sin pedir ayuda.", p, roll);
        }
    }

    who_t me = ME;
    me.pid = getpid();
    me.ppid = getppid();

    if (mine) {
        who_t other;
        if (!claim_victory(me, path, &other)) {
            say("Lo logre, pero Mr. Meeseeks (pid %d, N=%d, i=%d) se me adelanto. Espero la orden de desaparecer.",
                other.pid, other.N, other.i);
            wait_for_stop(in_fd);
            say("Me avisan por el pipe que la solicitud ya fue cumplida. Adios! *puff*");
            vanish(0);
        }
        say("Solicitud completada: \"%s\"! Gracias por dejarme servirle. Adios! *puff*", t->text);
        if (ME.N == 1)
            say("INFORME FINAL: la solicitud \"%s\" fue resuelta por Mr. Meeseeks (pid %d, ppid %d, N=%d, i=%d).",
                t->text, me.pid, me.ppid, me.N, me.i);
        msg_t r;
        memset(&r, 0, sizeof r);
        r.type = M_SOLVED; r.kind = K_TEXT; r.solver = me; r.path_sleep = path; r.ok = 1;
        write_msg(out_fd, &r);
        vanish(0);
    }

    /* ----- Pedir ayuda: 5 ayudantes si d <= 45, 2 si 45 < d <= 85 ----- */
    int want = (d <= HARD_LIMIT) ? 5 : 2;
    if (d <= 0.0)
        say("Esta tarea es IMPOSIBLE (dificultad %.2f)! La existencia es dolor! Invoco %d Mr. Meeseeks mas (nivel %d)...",
            d, want, ME.N + 1);
    else
        say("Esto es mas dificil de lo que pense (dificultad %.2f). Necesito ayuda! Invoco %d Mr. Meeseeks (nivel %d)...",
            d, want, ME.N + 1);

    /* Deja de "trabajar" y pasa a esperar a sus ayudantes: libera su slot.
     * Sin esto, con tareas muy dificiles todos los procesos vivos terminan
     * siendo padres que esperan un slot que nadie libera (interbloqueo).   */
    sem_post(&SH->slots);
    HOLDS_SLOT = 0;

    int up[2];
    if (pipe(up) < 0) { say("No pude crear el pipe de mis ayudantes."); goto failed_noup; }

    kid_t kids[MAX_KIDS];
    int nk = 0, finished = 0, capped_said = 0;
    int result = 0;              /* 0 pendiente, 1 resuelto, 2 STOP, -1 fallo */
    msg_t win;
    memset(&win, 0, sizeof win);

    while (result == 0) {
        /* Crear ayudantes pendientes, si hay slot libre */
        if (nk < want) {
            double cd = dilute(d, want);
            int s = spawn_helper(t, cd, path, in_fd, out_fd, up, kids, nk);
            if (s == 1) { nk++; continue; }
            if (s < 0) {
                if (!capped_said) {
                    say("Contingencia: no puedo invocar mas Mr. Meeseeks (tope de %ld por solicitud o fork fallido).",
                        CFG.max_total);
                    capped_said = 1;
                }
                want = nk;
                if (nk == 0 || finished == nk) { result = -1; break; }
            }
        }

        struct pollfd p[2] = { { in_fd, POLLIN, 0 }, { up[0], POLLIN, 0 } };
        int r = poll(p, 2, nk < want ? 25 : -1);
        if (r < 0) { if (errno == EINTR) continue; break; }

        if (p[0].revents) {                       /* mensaje de mi padre */
            msg_t m;
            int k = read_msg(in_fd, &m);
            if (k <= 0 || m.type == M_STOP) { result = 2; break; }
        }
        if (p[1].revents & POLLIN) {              /* mensaje de un hijo  */
            msg_t m;
            if (read_msg(up[0], &m) == 1) {
                if (m.type == M_SOLVED) { win = m; result = 1; break; }
                if (m.type == M_FAILED) {
                    finished++;
                    for (int j = 0; j < nk; j++)
                        if (kids[j].alive && kids[j].pid == m.solver.pid) {
                            waitpid(kids[j].pid, NULL, 0);
                            kids[j].alive = 0;
                        }
                    if (nk == want && finished == nk) { result = -1; break; }
                }
            }
        }
    }

    if (result == 1) {
        say("Mi ayudante reporta que Mr. Meeseeks (pid %d, ppid %d, N=%d, i=%d) resolvio la solicitud! Aviso a mis hijos para que desaparezcan.",
            win.solver.pid, win.solver.ppid, win.solver.N, win.solver.i);
        stop_all_kids(kids, nk);
        say("Mi trabajo aqui termino. Gracias por permitirme servirle. Adios! *puff*");
        if (ME.N == 1)
            say("INFORME FINAL: la solicitud \"%s\" fue resuelta por Mr. Meeseeks (pid %d, ppid %d, N=%d, i=%d).",
                t->text, win.solver.pid, win.solver.ppid, win.solver.N, win.solver.i);
        win.type = M_SOLVED;
        write_msg(out_fd, &win);
        close(up[0]); close(up[1]);
        vanish(0);
    }
    if (result == 2) {
        stop_all_kids(kids, nk);
        say("Me avisan por el pipe que la solicitud ya fue cumplida. Adios! *puff*");
        close(up[0]); close(up[1]);
        vanish(0);
    }
    stop_all_kids(kids, nk);
    close(up[0]); close(up[1]);

failed_noup:
    say("Ninguno de mis ayudantes pudo con \"%s\". La existencia es dolor! Me rindo. *puff*", t->text);
    if (ME.N == 1)
        say("INFORME FINAL: la solicitud \"%s\" NO pudo completarse.", t->text);
    {
        msg_t f;
        memset(&f, 0, sizeof f);
        f.type = M_FAILED; f.kind = K_TEXT; f.solver = me; f.path_sleep = path;
        write_msg(out_fd, &f);
    }
    vanish(1);
}

/* ------------------------------------------------------------------------ */
/*  B. Calculos: analizador recursivo descendente                            */
/*                                                                           */
/*  Gramatica (de menor a mayor precedencia):                                */
/*    expr    := or                                                          */
/*    or      := and   ( "||" and )*                                         */
/*    and     := eq    ( "&&" eq )*                                          */
/*    eq      := rel   ( ("==" | "!=") rel )*                                */
/*    rel     := add   ( ("<" | "<=" | ">" | ">=") add )*                    */
/*    add     := mul   ( ("+" | "-") mul )*                                  */
/*    mul     := unary ( ("*" | "/" | "%") unary )*                          */
/*    unary   := ("-" | "+" | "!") unary | power                             */
/*    power   := primary ( "^" unary )?          (asociativa a la derecha)   */
/*    primary := numero | "(" expr ")" | "true" | "false"                    */
/*             | ("sqrt" | "abs") "(" expr ")"                               */
/*  Los operadores logicos y relacionales devuelven 1 o 0; cualquier valor   */
/*  distinto de 0 es verdadero. Errores: sintaxis, division o modulo entre   */
/*  cero, raiz de negativo y resultados no finitos.                          */
/* ------------------------------------------------------------------------ */
typedef struct { const char *s, *start; int err; char emsg[160]; } parser_t;

static double p_or(parser_t *p);

static void p_fail(parser_t *p, const char *what)
{
    if (p->err) return;
    p->err = 1;
    snprintf(p->emsg, sizeof p->emsg, "%s (posicion %ld)", what, (long)(p->s - p->start) + 1);
}

static void p_ws(parser_t *p) { while (isspace((unsigned char)*p->s)) p->s++; }

static int p_eat(parser_t *p, const char *tok)          /* consume tok si esta */
{
    p_ws(p);
    size_t n = strlen(tok);
    if (strncmp(p->s, tok, n) == 0) { p->s += n; return 1; }
    return 0;
}

static double p_primary(parser_t *p)
{
    p_ws(p);
    if (p->err) return 0;
    if (p_eat(p, "(")) {
        double v = p_or(p);
        if (!p_eat(p, ")")) p_fail(p, "falta ')'");
        return v;
    }
    if (strncasecmp(p->s, "true", 4) == 0)  { p->s += 4; return 1; }
    if (strncasecmp(p->s, "false", 5) == 0) { p->s += 5; return 0; }
    if (strncasecmp(p->s, "sqrt", 4) == 0 || strncasecmp(p->s, "abs", 3) == 0) {
        int is_sqrt = (tolower((unsigned char)p->s[0]) == 's');
        p->s += is_sqrt ? 4 : 3;
        if (!p_eat(p, "(")) { p_fail(p, "se esperaba '(' despues de la funcion"); return 0; }
        double v = p_or(p);
        if (!p_eat(p, ")")) { p_fail(p, "falta ')'"); return 0; }
        if (is_sqrt && v < 0) { p_fail(p, "raiz cuadrada de un numero negativo"); return 0; }
        return is_sqrt ? sqrt(v) : fabs(v);
    }
    if (isdigit((unsigned char)*p->s) || *p->s == '.') {
        char *end;
        double v = strtod(p->s, &end);
        if (end == p->s) { p_fail(p, "numero mal formado"); return 0; }
        p->s = end;
        return v;
    }
    if (*p->s == 0) p_fail(p, "expresion incompleta");
    else {
        char m[64];
        snprintf(m, sizeof m, "simbolo inesperado '%c'", *p->s);
        p_fail(p, m);
    }
    return 0;
}

static double p_unary(parser_t *p);

static double p_power(parser_t *p)
{
    double b = p_primary(p);
    if (p_eat(p, "^")) return pow(b, p_unary(p));
    return b;
}

static double p_unary(parser_t *p)
{
    p_ws(p);
    if (*p->s == '!' && p->s[1] != '=') { p->s++; return !(p_unary(p) != 0); }
    if (*p->s == '-') { p->s++; return -p_unary(p); }
    if (*p->s == '+') { p->s++; return  p_unary(p); }
    return p_power(p);
}

static double p_mul(parser_t *p)
{
    double v = p_unary(p);
    for (;;) {
        if (p->err) return 0;
        if (p_eat(p, "*")) { v *= p_unary(p); }
        else if (p_eat(p, "/")) {
            double r = p_unary(p);
            if (r == 0) { p_fail(p, "division entre cero"); return 0; }
            v /= r;
        } else if (p_eat(p, "%")) {
            double r = p_unary(p);
            if (r == 0) { p_fail(p, "modulo entre cero"); return 0; }
            v = fmod(v, r);
        } else return v;
    }
}

static double p_add(parser_t *p)
{
    double v = p_mul(p);
    for (;;) {
        if (p->err) return 0;
        if (p_eat(p, "+")) v += p_mul(p);
        else if (p_eat(p, "-")) v -= p_mul(p);
        else return v;
    }
}

static double p_rel(parser_t *p)
{
    double v = p_add(p);
    for (;;) {
        if (p->err) return 0;
        if (p_eat(p, "<="))      v = v <= p_add(p);
        else if (p_eat(p, ">=")) v = v >= p_add(p);
        else if (p_eat(p, "<"))  v = v <  p_add(p);
        else if (p_eat(p, ">"))  v = v >  p_add(p);
        else return v;
    }
}

static double p_eq(parser_t *p)
{
    double v = p_rel(p);
    for (;;) {
        if (p->err) return 0;
        if (p_eat(p, "=="))      v = v == p_rel(p);
        else if (p_eat(p, "!=")) v = v != p_rel(p);
        else return v;
    }
}

static double p_and(parser_t *p)
{
    double v = p_eq(p);
    while (!p->err && p_eat(p, "&&")) { double r = p_eq(p); v = (v != 0) && (r != 0); }
    return v;
}

static double p_or(parser_t *p)
{
    double v = p_and(p);
    while (!p->err && p_eat(p, "||")) { double r = p_and(p); v = (v != 0) || (r != 0); }
    return v;
}

/* Evalua expr; devuelve 1 y deja el valor en *out, o 0 y el error en err */
static int evaluate(const char *expr, double *out, char *err, size_t errn)
{
    parser_t p = { expr, expr, 0, "" };
    double v = p_or(&p);
    p_ws(&p);
    if (!p.err && *p.s) p_fail(&p, "texto sobrante al final");
    if (!p.err && !isfinite(v)) p_fail(&p, "el resultado no es un numero finito");
    if (p.err) { snprintf(err, errn, "%s", p.emsg); return 0; }
    *out = v;
    return 1;
}

static void format_number(double v, char *buf, size_t n)
{
    if (fabs(v) < 1e15 && v == floor(v)) snprintf(buf, n, "%.0f", v);
    else snprintf(buf, n, "%.10g", v);
}

static void handle_calc(int out_fd, msg_t *t)
{
    msg_t r;
    memset(&r, 0, sizeof r);
    r.type = M_RESULT; r.kind = K_CALC;
    r.solver = ME; r.solver.pid = getpid(); r.solver.ppid = getppid();

    say("Recibi por mi pipe un calculo: \"%s\". Ooooh, eso es facil!", t->text);
    double v;
    char err[160], num[64];
    if (evaluate(t->text, &v, err, sizeof err)) {
        format_number(v, num, sizeof num);
        r.ok = 1;
        snprintf(r.text, sizeof r.text, "%.150s = %s", t->text, num);
        say("Resultado: %s. Calculo completado! Gracias por dejarme servirle. Adios! *puff*", r.text);
    } else {
        r.ok = 0;
        snprintf(r.text, sizeof r.text, "error: %.200s", err);
        say("No pude evaluar \"%s\": %s. La existencia es dolor! *puff*", t->text, err);
    }
    write_msg(out_fd, &r);
}

/* ------------------------------------------------------------------------ */
/*  C. Programas externos (pendiente)                                        */
/* ------------------------------------------------------------------------ */
static void handle_run(int out_fd, msg_t *t)
{
    msg_t r;
    memset(&r, 0, sizeof r);
    r.type = M_RESULT; r.kind = K_RUN; r.ok = 0;
    snprintf(r.text, sizeof r.text, "run aun no implementado: %s", t->text);
    write_msg(out_fd, &r);
}

/* ------------------------------------------------------------------------ */
/*  Punto de entrada de TODO Mr. Meeseeks (raiz o ayudante)                  */
/* ------------------------------------------------------------------------ */
static void meeseeks_run(int in_fd, int out_fd, int N, int i)
{
    IS_BOX  = 0;
    ME.pid  = getpid();
    ME.ppid = getppid();
    ME.N    = N;
    ME.i    = i;
    ME.d    = -1.0;
    BORN_AT = now_s();
    SLEPT   = 0.0;
    HOLDS_SLOT = 1;                 /* el padre (o la Box) reservo su slot */

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    unsigned long long s = (unsigned long long)getpid() * 0x9E3779B97F4A7C15ULL;
    s ^= CFG.seeded ? CFG.seed : ((unsigned long long)ts.tv_sec << 30) ^ (unsigned long long)ts.tv_nsec;
    rng_seed(s);

    say("Hi, I'm Mr. Meeseeks! Look at Meeeee. (%d, %d, %d, %d)", ME.pid, ME.ppid, N, i);
    register_self();

    msg_t t;
    if (read_msg(in_fd, &t) != 1 || t.type != M_TASK) {
        say("No recibi ninguna solicitud por mi pipe... *puff*");
        vanish(1);
    }

    switch (t.kind) {
    case K_TEXT: handle_text(in_fd, out_fd, &t); break;
    case K_CALC: handle_calc(out_fd, &t);        break;
    case K_RUN:  handle_run(out_fd, &t);         break;
    default:     say("Tipo de solicitud desconocido.");
    }
    vanish(0);
}

/* ======================================================================== */
/*  LA BOX                                                                   */
/* ======================================================================== */

typedef struct {
    int    id, kind, status;          /* status: 1 completada, 0 no, 2 caos */
    char   text[MAX_TEXT];
    char   result[MAX_TEXT];
    double d;                         /* dificultad efectiva (texto)        */
    int    d_random;                  /* 1 si se dejo a la suerte           */
    double dur_user;                  /* duracion pedida (0 = aleatoria)    */
    double wall, sim, real, cpu;      /* tiempos medidos                    */
    double chaos_at;                  /* instante del caos (-1 si no hubo)  */
    long   created, max_alive, failed_forks, capped;
    int    max_level;
    double sum_sleep, sum_active;
    who_t  solver;
} task_rec_t;

static task_rec_t TASKS[MAX_TASKS];
static int        NT = 0;
static volatile sig_atomic_t INTERRUPTED = 0;
static int        SEM_READY = 0;

static void on_sigint(int sig) { (void)sig; INTERRUPTED = 1; }

static const char *kind_name(int k)
{
    return k == K_TEXT ? "texto" : k == K_CALC ? "calc" : k == K_RUN ? "run" : "?";
}

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

static void csv_row(const task_rec_t *R)
{
    if (!CFG.csv) return;
    char clean[MAX_TEXT];
    snprintf(clean, sizeof clean, "%s", R->text);
    for (char *c = clean; *c; c++) if (*c == ',' || *c == '"' || *c == '\n') *c = ' ';
    fprintf(CFG.csv, "%d,%s,%s,%.4f,%d,%.4f,%d,%.6f,%.6f,%.6f,%.6f,%ld,%d,%ld,%.6f,%.6f,%ld,%ld,%.6f,%d,%d,%d,%.4f,%.3f\n",
            R->id, kind_name(R->kind), clean, R->d, R->d_random, R->dur_user, R->status,
            R->wall, R->sim, R->real, R->cpu, R->created, R->max_level, R->max_alive,
            R->sum_sleep, R->sum_active, R->failed_forks, R->capped, R->chaos_at,
            R->solver.pid, R->solver.N, R->solver.i, CFG.max_time, CFG.gain);
    fflush(CFG.csv);
}

/* Atiende UNA solicitud: crea al Meeseeks raiz y espera su resultado.
 * Devuelve 1 si se decreto caos planetario y la Box debe cerrarse.       */
static int run_task(int kind, const char *text, double d, double dur)
{
    task_rec_t *R = &TASKS[NT < MAX_TASKS ? NT++ : MAX_TASKS - 1];
    memset(R, 0, sizeof *R);
    R->id = NT; R->kind = kind; R->chaos_at = -1.0;
    snprintf(R->text, sizeof R->text, "%s", text);
    R->d = d; R->d_random = (kind == K_TEXT && d < 0); R->dur_user = dur;

    shared_reset();
    sem_wait(&SH->slots);              /* la raiz ocupa un slot         */
    SH->created = 1;

    int down[2], up[2];
    if (pipe(down) < 0 || pipe(up) < 0) { perror("pipe"); return 0; }

    struct rusage r0, r1;
    getrusage(RUSAGE_CHILDREN, &r0);
    double t0 = now_s();

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 0; }
    if (pid == 0) {
        setpgid(0, 0);                 /* grupo propio: facilita el caos */
        signal(SIGINT, SIG_IGN);
        close(down[1]); close(up[0]);
        if (CFG.csv) { /* el hijo no debe tocar el FILE* de la Box */ }
        meeseeks_run(down[0], up[1], 1, 1);
        _exit(0);
    }
    setpgid(pid, pid);
    close(down[0]); close(up[1]);

    say("Boton presionado: Mr. Meeseeks raiz (pid %d) atendera la solicitud #%d [%s] \"%s\"",
        pid, R->id, kind_name(kind), text);

    msg_t m;
    memset(&m, 0, sizeof m);
    m.type = M_TASK; m.kind = kind; m.difficulty = d; m.duration = dur;
    snprintf(m.text, sizeof m.text, "%s", text);
    write_msg(down[1], &m);            /* mensaje privado via pipe        */

    msg_t res;
    int have = 0, chaos = 0, killed = 0;
    double t_res = 0;
    for (;;) {
        int to = -1;
        if (CFG.max_time > 0 && !chaos) {
            double rem = t0 + CFG.max_time - now_s();
            to = rem <= 0 ? 0 : (int)(rem * 1000.0) + 1;
        }
        struct pollfd p = { up[0], POLLIN, 0 };
        int r = poll(&p, 1, to);
        if (r < 0 && errno == EINTR) {
            if (INTERRUPTED) { killed = 1; break; }
            continue;
        }
        if (r == 0) {                                  /* vencio el tiempo */
            chaos = 1;
            R->chaos_at = now_s() - t0;
            say("*** CAOS PLANETARIO! *** La solicitud #%d supero el tiempo maximo de %.1f s con %ld Mr. Meeseeks creados.",
                R->id, CFG.max_time, SH->created);
            if (!CFG.experiment) { killed = 1; break; }
            say("Modo experimento (-x): no se termina; se mide cuanto tiempo adicional necesitan los Meeseeks.");
            continue;
        }
        int k = read_msg(up[0], &res);
        t_res = now_s();
        have = (k == 1);
        break;
    }

    if (killed) {
        say("Terminando el grupo de procesos %d (SIGTERM y luego SIGKILL)...", pid);
        kill(-pid, SIGTERM);
        usleep(300000);
        kill(-pid, SIGKILL);
        t_res = now_s();
    }

    int st;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) ;
    /* La Box es subreaper: recoge huerfanos que hayan quedado */
    if (killed) { while (waitpid(-1, NULL, 0) > 0 || errno == EINTR) ; }
    else        { while (waitpid(-1, NULL, WNOHANG) > 0) ; }
    getrusage(RUSAGE_CHILDREN, &r1);

    R->wall = t_res - t0;
    R->cpu  = (r1.ru_utime.tv_sec - r0.ru_utime.tv_sec) + (r1.ru_utime.tv_usec - r0.ru_utime.tv_usec) / 1e6
            + (r1.ru_stime.tv_sec - r0.ru_stime.tv_sec) + (r1.ru_stime.tv_usec - r0.ru_stime.tv_usec) / 1e6;
    R->created      = SH->created;
    R->max_alive    = SH->max_alive;
    R->max_level    = SH->max_level;
    R->failed_forks = SH->failed_forks;
    R->capped       = SH->capped;
    R->sum_sleep    = SH->sum_sleep;
    R->sum_active   = SH->sum_active;
    if (kind == K_TEXT) R->d = SH->root_d;

    if (have && (res.type == M_SOLVED || (res.type == M_RESULT && res.ok))) R->status = 1;
    else if (killed) R->status = 2;
    else R->status = 0;

    if (have && res.type == M_SOLVED) { R->solver = res.solver; R->sim = res.path_sleep; }
    else R->sim = SH->max_path_sleep;
    if (have && res.type == M_RESULT) snprintf(R->result, sizeof R->result, "%s", res.text);
    R->real = R->wall - R->sim;
    if (R->real < 0) R->real = 0;

    close(down[1]); close(up[0]);

    const char *est = R->status == 1 ? "COMPLETADA" : R->status == 2 ? "CAOS PLANETARIO" : "NO COMPLETADA";
    say("Solicitud #%d %s. Tiempo total %.3f s | espera simulada %.3f s | tiempo real %.3f s | CPU %.3f s",
        R->id, est, R->wall, R->sim, R->real, R->cpu);
    say("   Mr. Meeseeks creados: %ld | nivel maximo: %d | pico simultaneo: %ld%s",
        R->created, R->max_level, R->max_alive, R->capped ? " | se activo el tope de contingencia" : "");
    if (R->chaos_at >= 0 && R->status == 1)
        say("   Caos declarado a los %.1f s; los Meeseeks necesitaron %.3f s adicionales para cumplir.",
            R->chaos_at, R->wall - CFG.max_time);
    if (R->result[0]) say("   Resultado: %s", R->result);
    csv_row(R);

    return killed && !CFG.keep_going;
}

/* Informe final de la Box al despedirse */
static void final_report(void)
{
    int ok = 0, no = 0, ch = 0;
    printf("\n==================================== INFORME DE LA BOX ====================================\n");
    printf("%-3s %-5s %-28s %6s %-13s %9s %9s %8s %8s %4s  %s\n",
           "#", "Tipo", "Solicitud", "Dific", "Estado", "T.total", "T.real", "CPU", "Meeseeks", "Niv", "Resolvio (pid,ppid,N,i)");
    for (int k = 0; k < NT; k++) {
        task_rec_t *R = &TASKS[k];
        char t[29], dif[16] = "-", who[64] = "-";
        snprintf(t, sizeof t, "%s", R->text);
        if (R->kind == K_TEXT) snprintf(dif, sizeof dif, "%.2f%s", R->d, R->d_random ? "*" : "");
        if (R->kind == K_TEXT && R->status == 1)
            snprintf(who, sizeof who, "(%d, %d, %d, %d)", R->solver.pid, R->solver.ppid, R->solver.N, R->solver.i);
        else if (R->status == 1) snprintf(who, sizeof who, "(raiz)");
        const char *est = R->status == 1 ? "completada" : R->status == 2 ? "caos" : "no completada";
        printf("%-3d %-5s %-28s %6s %-13s %8.3fs %8.3fs %7.3fs %8ld %4d  %s\n",
               R->id, kind_name(R->kind), t, dif, est, R->wall, R->real, R->cpu, R->created, R->max_level, who);
        if (R->status == 1) ok++; else if (R->status == 2) ch++; else no++;
    }
    printf("-------------------------------------------------------------------------------------------\n");
    printf("Solicitudes: %d | completadas: %d | no completadas: %d | caos: %d   (* dificultad a la suerte)\n",
           NT, ok, no, ch);
    printf("T.real = tiempo total menos la espera simulada del camino critico.\n");
    printf("===========================================================================================\n");
    fflush(stdout);
}

/* ------------------------------------------------------------------------ */
/*  Lectura de la entrada del usuario                                        */
/* ------------------------------------------------------------------------ */
static int read_line(const char *prompt, char *buf, size_t n)
{
    fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(buf, (int)n, stdin)) return 0;
    buf[strcspn(buf, "\r\n")] = 0;
    if (!isatty(STDIN_FILENO)) { fputs(buf, stdout); fputc('\n', stdout); fflush(stdout); }
    /* recortar espacios */
    char *a = buf;
    while (isspace((unsigned char)*a)) a++;
    memmove(buf, a, strlen(a) + 1);
    size_t L = strlen(buf);
    while (L && isspace((unsigned char)buf[L - 1])) buf[--L] = 0;
    return 1;
}

static int is_goodbye(const char *s)
{
    const char *bye[] = { "adios", "adiós", "bye", "salir", "exit", "chao", "hasta luego", "quit", NULL };
    for (int k = 0; bye[k]; k++) if (strcasecmp(s, bye[k]) == 0) return 1;
    return 0;
}

static void usage(const char *p)
{
    fprintf(stderr,
        "Uso: %s [-T seg] [-m max_total] [-a max_vivos] [-g ganancia] [-s semilla] [-q] [-x] [-k] [-o archivo.csv]\n"
        "  -T  tiempo maximo por solicitud antes del caos planetario (0 = sin limite, defecto 300)\n"
        "  -m  maximo de Mr. Meeseeks por solicitud (defecto 3000)\n"
        "  -a  maximo de Mr. Meeseeks vivos simultaneamente (defecto 128)\n"
        "  -g  ganancia de dilucion de la dificultad (defecto 0.5)\n"
        "  -s  semilla aleatoria\n"
        "  -q  silencioso: solo mensajes de la Box\n"
        "  -x  modo experimento: al vencer -T no se termina, se mide el tiempo extra\n"
        "  -k  seguir aceptando solicitudes despues de un caos\n"
        "  -o  agregar una fila CSV por solicitud\n", p);
}

int main(int argc, char **argv)
{
    int opt;
    const char *csv_path = NULL;
    while ((opt = getopt(argc, argv, "T:m:a:g:s:qxko:h")) != -1) {
        switch (opt) {
        case 'T': CFG.max_time  = atof(optarg); break;
        case 'm': CFG.max_total = atol(optarg); break;
        case 'a': CFG.max_alive = atoi(optarg); break;
        case 'g': CFG.gain      = atof(optarg); break;
        case 's': CFG.seed      = strtoul(optarg, NULL, 10); CFG.seeded = 1; break;
        case 'q': CFG.quiet     = 1; break;
        case 'x': CFG.experiment = 1; break;
        case 'k': CFG.keep_going = 1; break;
        case 'o': csv_path      = optarg; break;
        default:  usage(argv[0]); return opt == 'h' ? 0 : 2;
        }
    }
    if (CFG.max_alive < 1) CFG.max_alive = 1;

    if (csv_path) {
        CFG.csv = fopen(csv_path, "a");
        if (!CFG.csv) { perror(csv_path); return 1; }
        if (ftell(CFG.csv) == 0)
            fprintf(CFG.csv, "id,tipo,solicitud,dificultad,dif_aleatoria,dur_usuario,estado,t_total,t_simulado,t_real,cpu,"
                             "meeseeks,nivel_max,pico_vivos,suma_esperas,suma_activo,forks_fallidos,topes,caos_en,"
                             "solver_pid,solver_N,solver_i,max_time,ganancia\n");
    }

    signal(SIGPIPE, SIG_IGN);                 /* escribir a un hijo muerto da EPIPE */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);             /* sin SA_RESTART: poll devuelve EINTR */
    prctl(PR_SET_CHILD_SUBREAPER, 1);         /* adopta huerfanos de la solicitud */

    SH = mmap(NULL, sizeof *SH, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (SH == MAP_FAILED) { perror("mmap"); return 1; }
    shared_reset();

    ME.pid = getpid(); ME.ppid = getppid(); ME.N = 0; ME.i = 0;
    if (CFG.seeded) rng_seed(CFG.seed); else rng_seed((unsigned long long)time(NULL) ^ getpid());

    printf("+--------------------------------------------------------------+\n");
    printf("|             BOX DE MR. MEESEEKS   (IC-6600, ITCR)            |\n");
    printf("|  Escriba una solicitud, 'calc <expr>', 'run <comando>'       |\n");
    printf("|  o despidase con 'adios'.                                    |\n");
    printf("+--------------------------------------------------------------+\n");
    printf("Configuracion: tiempo maximo %.0f s%s | tope %ld Meeseeks | %d vivos a la vez | ganancia %.2f\n",
           CFG.max_time, CFG.max_time <= 0 ? " (sin limite)" : "", CFG.max_total, CFG.max_alive, CFG.gain);

    char line[MAX_TEXT], aux[64];
    for (;;) {
        if (INTERRUPTED) break;
        if (!read_line("\nBox> ", line, sizeof line)) break;
        if (!line[0]) continue;
        if (is_goodbye(line)) { say("El usuario se despide. Cerrando la Box..."); break; }

        int quit = 0;
        if (strncasecmp(line, "calc ", 5) == 0 || line[0] == '=') {
            const char *e = line[0] == '=' ? line + 1 : line + 5;
            quit = run_task(K_CALC, e, 0, 0);
        } else if (strncasecmp(line, "run ", 4) == 0 || line[0] == '!') {
            const char *c = line[0] == '!' ? line + 1 : line + 4;
            quit = run_task(K_RUN, c, 0, 0);
        } else {
            double d = -1.0, dur = 0.0;
            if (!read_line("  Dificultad 0-100 (Enter = a la suerte del Meeseeks): ", aux, sizeof aux)) break;
            if (aux[0] && aux[0] != 'r' && aux[0] != 'R') {
                char *end;
                d = strtod(aux, &end);
                if (end == aux) { say("Valor no valido; se deja a la suerte."); d = -1.0; }
                else { if (d < 0) d = 0; if (d > 100) d = 100; }
            }
            if (!read_line("  Duracion simulada en s (Enter = aleatoria 0.5-5): ", aux, sizeof aux)) break;
            if (aux[0]) { dur = atof(aux); if (dur < 0) dur = 0; }
            if (d == 0.0)
                say("Advertencia: dificultad 0 = tarea imposible. Se aplicara el plan de contingencia.");
            quit = run_task(K_TEXT, line, d, dur);
        }
        if (quit || INTERRUPTED) break;
    }

    final_report();
    say("Gracias por usar la Box de Mr. Meeseeks. Hasta luego!");
    if (CFG.csv) fclose(CFG.csv);
    sem_destroy(&SH->mutex);
    sem_destroy(&SH->slots);
    sem_destroy(&SH->out);
    munmap(SH, sizeof *SH);
    return 0;
}
