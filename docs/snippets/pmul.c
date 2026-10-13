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
