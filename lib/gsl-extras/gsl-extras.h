#ifndef GSL_EXTRAS_H
#define GSL_EXTRAS_H

/* GSLEXTRAS_CDF_ERROR: call the error handler, and return a NAN. */
#define GSLEXTRAS_CDF_ERROR(reason, gsl_errno) \
       do { \
       gsl_error (reason, __FILE__, __LINE__, gsl_errno) ; \
       return GSL_NAN ; \
       } while (0)

double gslextras_cdf_beta_Pinv (const double p, const double a,
                                const double b);
double gslextras_cdf_beta_Qinv (double q, double a, double b);
double gslextras_cdf_binomial_P(const long k, const long n, const double p);
double gslextras_cdf_binomial_Q(const long k, const long n, const double q);
double gslextras_cdf_geometric_P (const long n, const double p);
double gslextras_cdf_geometric_Q ( const long n, const double p);
double gslextras_cdf_hypergeometric_P (const unsigned int k, 
                                       const unsigned int n0,
                                       const unsigned int n1,
                                       const unsigned int t);
double gslextras_cdf_hypergeometric_Q (const unsigned int k, 
                                       const unsigned int n0,
                                       const unsigned int n1,
                                       const unsigned int t);
double gslextras_cdf_negative_binomial_P(const long n,
                                         const long k, const double p);
double gslextras_cdf_negative_binomial_Q(const long n, const long k,
                                         const double p);
double gslextras_cdf_poisson_P (const long k, const double lambda);
double gslextras_cdf_poisson_Q (const long k, const double lambda);

#endif /* gsl-extras.h */
