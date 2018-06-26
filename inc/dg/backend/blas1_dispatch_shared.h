#ifndef _DG_BLAS_VECTOR_
#define _DG_BLAS_VECTOR_

#ifdef DG_DEBUG
#include <cassert>
#endif //DG_DEBUG

#include <tuple>
#include <thrust/host_vector.h>
#include <thrust/device_vector.h>

#include "vector_categories.h"
#include "scalar_categories.h"
#include "tensor_traits.h"
#include "predicate.h"

#include "blas1_serial.h"
#if THRUST_DEVICE_SYSTEM==THRUST_DEVICE_SYSTEM_CUDA
#include "blas1_cuda.cuh"
#else
#include "blas1_omp.h"
#endif


///@cond
namespace dg
{
namespace blas1
{
template< class Subroutine, class ContainerType, class ...ContainerTypes>
inline void subroutine( Subroutine f, ContainerType&& x, ContainerTypes&&... xs);
namespace detail
{
template< class ContainerType1, class ContainerType2>
inline std::vector<int64_t> doDot_superacc( const ContainerType1& x, const ContainerType2& y);
//we need to distinguish between Scalars and Vectors

///////////////////////////////////////////////////////////////////////////////////////////
template< class To, class From>
To doTransfer( const From& in, ThrustVectorTag, ThrustVectorTag)
{
    To t( in.begin(), in.end());
    return t;
}

template< class Vector1, class Vector2>
std::vector<int64_t> doDot_superacc( const Vector1& x, const Vector2& y, SharedVectorTag)
{
    static_assert( std::is_convertible<get_value_type<Vector1>, double>::value, "We only support double precision dot products at the moment!");
    static_assert( std::is_convertible<get_value_type<Vector2>, double>::value, "We only support double precision dot products at the moment!");
    //find out which one is the SharedVector and determine category and policy
    using vector_type = find_if_t<dg::is_not_scalar, Vector1, Vector1, Vector2>;
    constexpr unsigned vector_idx = find_if_v<dg::is_not_scalar, Vector1, Vector1, Vector2>::value;
    using execution_policy = get_execution_policy<vector_type>;
    static_assert( all_true<
            dg::has_any_or_same_policy<Vector1, execution_policy>::value,
            dg::has_any_or_same_policy<Vector2, execution_policy>::value
            >::value,
        "All ContainerType types must have compatible execution policies (AnyPolicy or Same)!");
    //maybe assert size here?
    auto size = std::get<vector_idx>(std::forward_as_tuple(x,y)).size();
    return doDot_dispatch( execution_policy(), size, get_pointer_or_scalar(x), get_pointer_or_scalar(y));
}


//template<class T>
//auto do_get_iterator( T v) -> decltype(v.begin()){
//    return v.begin();
//}
template<class T>
inline auto do_get_iterator( T&& v, AnyVectorTag) -> decltype(v.begin()){
    return v.begin();
}
template<class T>
inline thrust::constant_iterator<T> do_get_iterator( T&& v, AnyScalarTag){
    return thrust::constant_iterator<T>(v);
}
template<class T>
inline auto get_iterator( T&& v ) -> decltype( do_get_iterator( std::forward<T>(v), get_tensor_category<T>())) {
    return do_get_iterator( std::forward<T>(v), get_tensor_category<T>());
}

template< class Subroutine, class ContainerType, class ...ContainerTypes>
inline void doSubroutine( SharedVectorTag, Subroutine f, ContainerType&& x, ContainerTypes&&... xs)
{
    using vector_type = find_if_t<dg::is_not_scalar_has_not_any_policy, get_value_type<ContainerType>, ContainerType, ContainerTypes...>;
    static_assert( !is_scalar<vector_type>::value,
            "At least one ContainerType must have a non-trivial execution policy!"); //Actually, we know that this is true at this point
    using execution_policy = get_execution_policy<vector_type>;
    static_assert( all_true<
            dg::has_any_or_same_policy<ContainerType, execution_policy>::value,
            dg::has_any_or_same_policy<ContainerTypes, execution_policy>::value...
            >::value,
        "All ContainerType types must have compatible execution policies (AnyPolicy or Same)!");
    constexpr unsigned vector_idx = find_if_v<dg::is_not_scalar_has_not_any_policy, get_value_type<ContainerType>, ContainerType, ContainerTypes...>::value;

//#ifdef DG_DEBUG
    //is this possible?
    //assert( !x.empty());
    //assert( x.size() == xs.size() );
//#endif //DG_DEBUG
    doSubroutine_dispatch(
            get_execution_policy<vector_type>(),
            std::get<vector_idx>(std::forward_as_tuple(x,xs...)).size(),
            f,
            get_iterator(std::forward<ContainerType>(x)) ,
            get_iterator(std::forward<ContainerTypes>(xs)) ...
            );
}

} //namespace detail
} //namespace blas1
} //namespace dg
///@endcond

#endif //_DG_BLAS_VECTOR_
