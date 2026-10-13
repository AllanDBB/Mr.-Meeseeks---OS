/* "Tiro Meeseeks": probabilidad de que un Meeseeks, solo, logre una tarea
 * de dificultad d <= 85 sin pedir ayuda. Cubica: castiga mucho lo dificil. */
static double self_success_p(double d)
{
    double x = d / 100.0;
    return x * x * x;
}
