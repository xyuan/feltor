#ifndef _DG_BLAS_SERIAL_
#define _DG_BLAS_SERIAL_
#include "config.h"
#include "execution_policy.h"
#include "exblas/exdot_serial.h"

namespace dg
{
namespace blas1
{
namespace detail
{
template<class RA1, class RA2>
inline std::vector<int64_t> doDot_dispatch( SerialTag, unsigned size, RA1 x_ptr, RA2 y_ptr) {
    std::vector<int64_t> h_superacc(exblas::BIN_COUNT);
    exblas::exdot_cpu( size, x_ptr,y_ptr, &h_superacc[0]) ;
    return h_superacc;
}
template<class RA1, class RA2, class RA3>
inline std::vector<int64_t> doDot_dispatch( SerialTag, unsigned size, RA1 x_ptr, RA2 y_ptr, RA3 z_ptr) {
    std::vector<int64_t> h_superacc(exblas::BIN_COUNT);
    exblas::exdot_cpu( size, x_ptr,y_ptr,z_ptr, &h_superacc[0]) ;
    return h_superacc;
}

template< class Subroutine, class T, class ...Ts>
inline void doSubroutine_dispatch( SerialTag, int size, Subroutine f, T x, Ts... xs)
{
    for( int i=0; i<size; i++)
    {
        //f(x[i], xs[i]...);
        f(thrust::raw_reference_cast(*(x+i)), thrust::raw_reference_cast(*(xs+i))...);
    }
}


}//namespace detail
}//namespace blas1
}//namespace dg
#endif //_DG_BLAS_SERIAL_
