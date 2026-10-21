#pragma once

// The Kaiser window's Bessel function, shared by the Oversampling filters' design and the Limiter's true-peak
// estimator.

namespace comp1
{

// The modified Bessel function of the first kind, order 0, by its power series, as dyn-lab works it out.
inline double besselI0 (double x)
{
    double total = 1.0, term = 1.0;
    for (int k = 1; term > 1.0e-17 * total; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        total += term;
    }
    return total;
}

} // namespace comp1
