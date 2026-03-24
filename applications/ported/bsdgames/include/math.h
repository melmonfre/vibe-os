#ifndef VIBE_BSDGAME_MATH_H
#define VIBE_BSDGAME_MATH_H

#include <lang/include/vibe_app_runtime.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double fabs(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double fmod(double x, double y);
double ldexp(double x, int exp);
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double acos(double x);
double exp(double x);
double pow(double x, double y);
double log(double x);
long lround(double x);

#endif
