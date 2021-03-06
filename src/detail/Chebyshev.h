/*

Copyright (c) 2005-2016, University of Oxford.
All rights reserved.

University of Oxford means the Chancellor, Masters and Scholars of the
University of Oxford, having an administrative office at Wellington
Square, Oxford OX1 2JD, UK.

This file is part of Aboria.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
 * Neither the name of the University of Oxford nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/


#ifndef CHEBYSHEV_DETAIL_H_
#define CHEBYSHEV_DETAIL_H_

#include <boost/math/constants/constants.hpp>
#include "Vector.h"
#include "detail/SpatialUtil.h"

namespace Aboria {
namespace detail {

// constexpr sin and cos routines from (Boost Licence):
// https://github.com/pkeir/ctfft/blob/master/const_math.hpp
// Based on the triple-angle formula: sin 3x = 3 sin x - 4 sin ^3 x

const double PI = boost::math::constants::pi<double>();
const double PI_2 = boost::math::constants::pi<double>()/2;

/*
constexpr double tol = 0.001;

constexpr double cube(const double x) { return x*x*x; }

constexpr
double sin_helper(const double x) {
  return x < tol ? x : 3*(sin_helper(x/3.0)) - 4*cube(sin_helper(x/3.0));
}

constexpr
double sin(const double x) {
  return sin_helper(x < 0 ? -x+PI : x);
}

//sinh 3x = 3 sinh x + 4 sinh ^3 x
constexpr
double sinh_helper(const double x) {
  return x < tol ? x : 3*(sinh_helper(x/3.0)) + 4*cube(sinh_helper(x/3.0));
}

//sinh 3x = 3 sinh x + 4 sinh ^3 x
constexpr
double sinh(const double x) {
  return x < 0 ? -sinh_helper(-x) : sinh_helper(x);
}

constexpr double cos (const double x) { return sin(PI_2 - x); }
*/


template <typename T>
T chebyshev_polynomial(const T &x, unsigned int n) {
    if (n==0) {
        return 1.0;
    } else if (n==1) {
        return x;
    } else {
        T Tn_2 = 1.0;
        T Tn_1 = x;
        for (int i=2; i<=n; ++i) {
            T Tn = 2.0*x*Tn_1 - Tn_2;
            Tn_2 = Tn_1;
            Tn_1 = Tn;
        }
        return Tn_1;
    }
}

template <typename T=void>
double chebyshev_node(const unsigned int i, const unsigned int n) {
    return cos((2.0*i+1.0)*PI/(2.0*n));
}

template <unsigned int N>
double chebyshev_Rn_slow(const Vector<double,N> &x, const Vector<int,N> &i, unsigned int n) {
    double Rn = 1.0;
    for (unsigned int d = 0; d < N; ++d) {
        double Sn = 0;
        for (unsigned int k = 1; k < n; ++k) {
            const double root = chebyshev_node(i[d],n);
            Sn += chebyshev_polynomial(root,k)*chebyshev_polynomial(x[d],k);
        }
        Sn = (1.0 + 2.0*Sn)/n;
        Rn *= Sn;
    }
    return Rn;
}

// evaluate 1/n + 2/n * sum_{k=1}^{n-1} T_k(y_i) T_k(x), where y_i is the i-th
// root of the the chebyshev polynomial of order n
// NOTE: valid range of i is 0..n-1
template <typename T>
T chebyshev_Sn(const T &x, unsigned int i, unsigned int n) {
    // Clenshaw algorithm: \alpha = 2x, \beta = -1, T0=1, T1=x
    //                     a_0 = 1/n, a_k = 2/n * cos(k*(2i-1)/(2n) * pi)
    T bk_1 = 0;
    T bk_2 = 0;
    const double invn = 1.0/n;
    for (unsigned int k=n-1; k>=1; --k) {
        T bk = 2*invn*cos(k*(2.0*i+1.0)*PI_2*invn) + 2.0*x*bk_1 - bk_2; 
        bk_2 = bk_1;
        bk_1 = bk;
    }
    // one more step with 2*a_0, then s(x) = 0.5*(b0 - b2)
    return (invn + x*bk_1 - bk_2);
}



// struct to calculate Rn with N chebyshev nodes using spatial dimension D 
template <unsigned int D>
struct Chebyshev_Rn {
    typedef Vector<double,D> double_d;
    typedef Vector<int,D> int_d;
    typedef std::vector<double_d> vector_double_d;
    typedef typename std::vector<double_d>::const_iterator double_d_iterator;
    vector_double_d Sn;
    unsigned int n;
    unsigned int N;
    detail::bbox<D> box;
    Chebyshev_Rn() {}
    void calculate_Sn(const double_d_iterator& positions, 
                      const unsigned int with_N,
                      const unsigned int with_n) {
        n = with_n;
        N = with_N;
        Sn.resize(N*n);
        box.bmin = positions[0];
        box.bmax = positions[0];
        for (int i=1; i<N; ++i) {
            for (int d=0; d<D; ++d) {
                if (positions[i][d] < box.bmin[d]) box.bmin[d] = positions[i][d];
                if (positions[i][d] > box.bmax[d]) box.bmax[d] = positions[i][d];
            }
        }
        const double_d scale = double_d(1.0)/(box.bmax-box.bmin);
        for (int i=0; i<N; ++i) {
            for (int m=0; m<n; ++m) {
                Sn[i*n + m] = chebyshev_Sn((2*positions[i]-box.bmin-box.bmax)*scale,m,n);
            }
        }
    }

    // NOTE: valid range of m is 0..n-1
    double_d get_position(const int_d &m) {
        ASSERT((m>=0).all() ,"m should be greater than or equal to 0");
        ASSERT((m<n).all() ,"m should be less than n");
        double_d pos;
        for (int d=0; d<D; ++d) {
            pos[d] = chebyshev_node(m[d],n);
        }
        return 0.5*(pos+1)*(box.bmax-box.bmin) + box.bmin;
    }

    // NOTE: valid range of m is 0..n-1
    double operator()(const int_d &m, const unsigned int i) {
        const unsigned int ii = i*n;
        ASSERT(ii < Sn.size(),"requesting i greater than particles size");
        ASSERT((m>=0).all() ,"m should be greater than or equal to 0");
        ASSERT((m<n).all() ,"m should be less than n");
        double ret = 1.0;
        for (int d=0; d<D; ++d) {
            ret *= Sn[ii+m[d]][d];
        }
        return ret;
    }
};
 


 
    
}
}

#endif
