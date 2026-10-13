/* ------------------------------------------------------------------------ */
/*  C. Programas externos                                                    */
/*  El Meeseeks crea un proceso nieto con fork() que hace exec del comando   */
/*  a traves de /bin/sh -c (permite argumentos, tuberias y redirecciones),   */
/*  mide su tiempo de ejecucion y espera su valor de retorno con waitpid().  */
/*  Exito <=> el programa termino normalmente con codigo 0.                  */
/* ------------------------------------------------------------------------ */
static void handle_run(int out_fd, msg_t *t)
{
    msg_t r;
    memset(&r, 0, sizeof r);
    r.type = M_RESULT; r.kind = K_RUN;
    r.solver = ME; r.solver.pid = getpid(); r.solver.ppid = getppid();

    say("Recibi por mi pipe la orden de ejecutar: %s. Ooooh, a la orden!", t->text);
    fflush(NULL);
    double t0 = now_s();
    pid_t c = fork();
    if (c < 0) {
        r.ok = 0;
        snprintf(r.text, sizeof r.text, "error: fork fallo (%s)", strerror(errno));
        write_msg(out_fd, &r);
        return;
    }
    if (c == 0) {
        int dn = open("/dev/null", O_RDONLY);   /* el programa no lee de la Box */
        if (dn >= 0) { dup2(dn, STDIN_FILENO); close(dn); }
        close(out_fd);
        execl("/bin/sh", "sh", "-c", t->text, (char *)NULL);
        _exit(127);                              /* exec fallo */
    }
    say("Programa externo lanzado como pid %d; espero su valor de retorno...", c);
    int st;
    while (waitpid(c, &st, 0) < 0 && errno == EINTR) ;
    double el = now_s() - t0;

    if (WIFEXITED(st)) {
        int code = WEXITSTATUS(st);
        r.ok = (code == 0);
        snprintf(r.text, sizeof r.text, "codigo de salida %d%s en %.4f s", code,
                 code == 127 ? " (comando no encontrado)" : code == 126 ? " (no ejecutable)" : "", el);
    } else if (WIFSIGNALED(st)) {
        r.ok = 0;
        snprintf(r.text, sizeof r.text, "terminado por la senal %d (%s) en %.4f s",
                 WTERMSIG(st), strsignal(WTERMSIG(st)), el);
    }
    if (r.ok)
        say("El programa termino con exito (%s). Tarea completada! Gracias por dejarme servirle. Adios! *puff*", r.text);
    else
        say("El programa NO termino con exito (%s). La existencia es dolor! *puff*", r.text);
    write_msg(out_fd, &r);
}
