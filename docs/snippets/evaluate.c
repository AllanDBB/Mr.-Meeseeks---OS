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
