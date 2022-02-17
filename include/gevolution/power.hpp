#pragma once

#include <complex> // norm
#include <vector>  // vector
#include <utility> // pair
#include <exception> // runtime_error
#include <boost/mpi/communicator.hpp>
#include <boost/mpi/collectives.hpp>
#include <boost/mpi/inplace.hpp>
#include <boost/serialization/utility.hpp>
#include "LATfield2.hpp"

namespace gevolution
{
    namespace detail
    {
        // define this because boost::mpi does not support lambdas yet
        template<typename T1, typename T2>
        struct my_pair_sum
        {
            using value_type = typename std::pair<T1,T2>;
            
            value_type operator()(const value_type& x, const value_type& y)
            {
                return value_type{x.first+y.first,x.second+y.second};
            }
        };
    } // namespace detail
    // power spectrum of a scalar field
    template<class complex_field_type>
    auto power_spectrum(const complex_field_type& F)
    {
        using complex_type = typename complex_field_type::value_type;
        using real_type = typename complex_type::value_type;
        using std::norm;
        const int N_global = F.lattice().size(1);
        const real_type N3_inv = std::pow( 1.0 / N_global , 3 );
        const int k_nyquist = (N_global - 1)/2;
        std::vector< std::pair<int,real_type> > pw(k_nyquist+1,{0,0});
        
        auto signed_mode = [k_nyquist,N_global](int n)
        {
            return n <= k_nyquist ? n : N_global - n;
        };
        
        if(F.components()>1)
            throw std::runtime_error(
                "gevolution::power_spectrum only works for scalar fields");
        
        // accumulate modes on local grid
        F.for_each( 
            [&pw,&signed_mode](const complex_type& value, const LATfield2::Site & x)
            {
                double global_mode=0;
                for(int i=0;i<3;++i)
                {
                    int k_i = signed_mode( x.coord(i) );
                    global_mode += k_i*k_i;
                }
                size_t index = std::floor( std::sqrt(global_mode) + 0.5 );
                
                if(index >= pw.size())
                    return;
                
                auto& [count,sum] = pw[index];
                ++count;
                using std::norm;
                auto z2 = norm(value);
                sum += z2;
                
                if(global_mode>0)
                {
                    // there is a hidden symmetric partner due to R2C storage
                    // implementation
                    sum += z2;
                    ++ count;
                }else{
                
                }
            });
        
        const boost::mpi::communicator& com = LATfield2::parallel.my_comm;
        ::boost::mpi::all_reduce(com,
            ::boost::mpi::inplace_t< std::pair<int,real_type>* >(pw.data()),pw.size(),
            detail::my_pair_sum<int,real_type>());
        std::vector<real_type> average(pw.size());
        for(auto i=0U;i<pw.size();++i)
        {
            average[i]=0;
            if(pw[i].first>0)
                average[i] = pw[i].second / pw[i].first;
            average[i] *= N3_inv;
        }
        return average;
    }
} // namespace gevolution
