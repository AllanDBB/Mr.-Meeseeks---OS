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
