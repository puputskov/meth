#ifndef METH_WASM_LIBC_MATH_H
#define METH_WASM_LIBC_MATH_H

/*
 * The math functions of the program for the WebAssembly build. The ones a
 * processor does in one instruction are builtins; the others are imported
 * from the page, which has them in its Math object (the module name is
 * "env"), and a few are in platform/wasm/libc.c.
 */

#define sqrt(x)  __builtin_sqrt (x)
#define fabs(x)  __builtin_fabs (x)
#define floor(x) __builtin_floor (x)
#define ceil(x)  __builtin_ceil (x)
#define trunc(x) __builtin_trunc (x)

#define isnan(x)    __builtin_isnan (x)
#define isinf(x)    __builtin_isinf (x)
#define isfinite(x) __builtin_isfinite (x)

#define ML_WASM_IMPORT(name) \
	__attribute__ ((import_module ("env"), import_name (name)))

ML_WASM_IMPORT ("sin")   double sin (double x);
ML_WASM_IMPORT ("cos")   double cos (double x);
ML_WASM_IMPORT ("tan")   double tan (double x);
ML_WASM_IMPORT ("asin")  double asin (double x);
ML_WASM_IMPORT ("acos")  double acos (double x);
ML_WASM_IMPORT ("atan")  double atan (double x);
ML_WASM_IMPORT ("atan2") double atan2 (double y, double x);
ML_WASM_IMPORT ("sinh")  double sinh (double x);
ML_WASM_IMPORT ("cosh")  double cosh (double x);
ML_WASM_IMPORT ("tanh")  double tanh (double x);
ML_WASM_IMPORT ("asinh") double asinh (double x);
ML_WASM_IMPORT ("acosh") double acosh (double x);
ML_WASM_IMPORT ("atanh") double atanh (double x);
ML_WASM_IMPORT ("exp")   double exp (double x);
ML_WASM_IMPORT ("log")   double log (double x);
ML_WASM_IMPORT ("log10") double log10 (double x);
ML_WASM_IMPORT ("pow")   double pow (double x, double y);

double round (double x);
double hypot (double x, double y);
double ldexp (double x, int n);
double lgamma (double x);
double erfc (double x);

#endif /* METH_WASM_LIBC_MATH_H */
