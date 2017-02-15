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


#ifndef CHEBYSHEV_TEST_H_
#define CHEBYSHEV_TEST_H_

#include <cxxtest/TestSuite.h>

#include <random>
#include <time.h>
#include "Level1.h"
#include "detail/Chebyshev.h"

using namespace Aboria;

class ChebyshevTest : public CxxTest::TestSuite {
public:
    template <unsigned int D>
    void helper_Rn_calculation(void) {
        const double tol = 1e-10;
        // randomly generate a bunch of positions over a range 
        std::uniform_real_distribution<double> U(-10,100);
        generator_type generator(time(NULL));
        typedef Vector<double,D> double_d;
        typedef Vector<int,D> int_d;
        const size_t N = 50;
        std::vector<double_d> positions(N);
        for (int i=0; i<N; i++) {
            for (int d=0; d<D; ++d) {
                positions[i][d] = U(generator);
            }
        }
        detail::Chebyshev_Rn<D> Rn;
        for (int n=1; n<10; ++n) {
            Rn.calculate_Sn(positions,n);
            const int_d start = int_d(0);
            const int_d end = int_d(n-1);
            auto range = iterator_range<lattice_iterator<D>>(
                lattice_iterator<D>(start,end,start)
                ,++lattice_iterator<D>(start,end,end)
                );
            const double_d scale = double_d(1.0)/(Rn.box.bmax-Rn.box.bmin);
            for (int i=0; i<positions.size(); ++i) {
                const double_d &x =  (2*positions[i]-Rn.box.bmin-Rn.box.bmax)*scale;
                for (const int_d& m: range) {
                    TS_ASSERT_DELTA(Rn(m,i),detail::chebyshev_Rn_slow(x,m,n),tol);
                }
            }
        }
    }
    void test_chebyshev_polynomial_calculation(void) {
        const double tol = 1e-10;
        // evaluate polynomial of order k at i-th root
        // of polynomial of order n
        // should by cos(k*(2*i-1)/(2*n)*PI
        std::cout << "testing polynomial calculation..." << std::endl;
        const int n = 4;
        for (int i=0; i<n; ++i) {
            const double x = cos((2.0*i+1.0)/(2.0*n)*detail::PI);
            for (int k=0; k<n; ++k) {
                    TS_ASSERT_DELTA(detail::chebyshev_polynomial(x,k),
                                    cos(k*(2.0*i+1.0)/(2.0*n)*detail::PI),tol);
            }
        }

    }

    void test_Rn_calculation(void) {
        std::cout << "testing 1D..." << std::endl;
        helper_Rn_calculation<1>();
        std::cout << "testing 2D..." << std::endl;
        helper_Rn_calculation<2>();
        std::cout << "testing 3D..." << std::endl;
        helper_Rn_calculation<3>();
        std::cout << "testing 4D..." << std::endl;
        helper_Rn_calculation<4>();
    }
};


#endif