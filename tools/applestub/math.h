#pragma once
/* 最小替身（syntax_check.py） */
double sqrt(double); float sqrtf(float); double fabs(double); float fabsf(float); double floor(double); float floorf(float); double ceil(double); float ceilf(float);
double pow(double, double); float powf(float, float); double exp(double); float expf(float); double log(double); float logf(float); double log2(double); float log2f(float);
double sin(double); float sinf(float); double cos(double); float cosf(float); double tan(double); double atan2(double, double); float atan2f(float, float); double atan(double);
double fmod(double, double); float fmodf(float, float); double round(double); float roundf(float); long lround(double); double trunc(double); float truncf(float);
double fmin(double, double); double fmax(double, double); float fminf(float, float); float fmaxf(float, float); double ldexp(double, int); double frexp(double, int *);
double modf(double, double *); double hypot(double, double); float hypotf(float, float); double log10(double); double asin(double); double acos(double); double exp2(double); float exp2f(float);
long lrint(double); long lrintf(float); double nearbyint(double); double cbrt(double); double sinh(double); double cosh(double); double tanh(double);
#define M_PI 3.14159265358979323846
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
