#ifndef _DG_ARAKAWA_CUH
#define _DG_ARAKAWA_CUH

#include "blas.h"
#include "geometry/geometry.h"
#include "enums.h"
#include "geometry/evaluation.h"
#include "geometry/derivatives.h"
#ifdef MPI_VERSION
#include "geometry/mpi_derivatives.h"
#include "geometry/mpi_evaluation.h"
#endif

/*! @file

  @brief object for computation of Poisson bracket
  */
namespace dg
{
//citation missing in documentation
/**
 * @brief X-space generalized version of Arakawa's scheme
 *
 * Computes \f[ [f,g] := 1/\sqrt{g_{2d}}\left(\partial_x f\partial_y g - \partial_y f\partial_x g\right) \f]
 * where \f$ g_{2d} = g/g_{zz}\f$ is the two-dimensional volume element of the plane in 2x1 product space.
 * @snippet arakawa_t.cu function
 * @snippet arakawa_t.cu doxygen
 * @copydoc hide_geometry_matrix_container
 * @ingroup arakawa
 */
template< class Geometry, class Matrix, class container >
struct ArakawaX
{
    /**
     * @brief Create Arakawa on a grid
     * @param g The grid
     */
    ArakawaX( const Geometry& g);
    /**
     * @brief Create Arakawa on a grid using different boundary conditions
     * @param g The grid
     * @param bcx The boundary condition in x
     * @param bcy The boundary condition in y
     */
    ArakawaX( const Geometry& g, bc bcx, bc bcy);

    /**
     * @brief Compute Poisson bracket
     *
     * Computes \f[ [f,g] := 1/\sqrt{g_{2d}}\left(\partial_x f\partial_y g - \partial_y f\partial_x g\right) \f]
     * where \f$ g_{2d} = g/g_{zz}\f$ is the two-dimensional volume element of the plane in 2x1 product space.
     * @param lhs left hand side in x-space
     * @param rhs rights hand side in x-space
     * @param result Poisson's bracket in x-space
     * @note memops: 25 reads; 9 writes (+ 2 reads and 1 write, if geometry is nontrivial)
     */
    void operator()( const container& lhs, const container& rhs, container& result);

    /**
     * @brief Return internally used x - derivative
     *
     * The same as a call to dg::create::dx( g, bcx)
     * @return derivative
     */
    const Matrix& dx() {return bdxf;}
    /**
     * @brief Return internally used y - derivative
     *
     * The same as a call to dg::create::dy( g, bcy)
     * @return derivative
     */
    const Matrix& dy() {return bdyf;}

    /**
     * @brief Compute the total variation integrand
     *
     * Computes \f[ (\nabla\phi)^2 = \partial_i \phi g^{ij}\partial_j \phi \f]
     * in the plane of a 2x1 product space
     * @param phi function
     * @param varphi may equal phi, contains result on output
     */
    void variation( const container& phi, container& varphi)
    {
        blas2::symv( bdxf, phi, dxrhs);
        blas2::symv( bdyf, phi, dyrhs);
        tensor::multiply2d( metric_, dxrhs, dyrhs, varphi, helper_);
        blas1::pointwiseDot( varphi, dxrhs, varphi);
        blas1::pointwiseDot( 1., helper_, dyrhs,1., varphi );
    }

  private:
    container dxlhs, dxrhs, dylhs, dyrhs, helper_;
    Matrix bdxf, bdyf;
    SparseElement<container> perp_vol_inv_;
    SparseTensor<container> metric_;
};
///@cond
template<class Geometry, class Matrix, class container>
ArakawaX<Geometry, Matrix, container>::ArakawaX( const Geometry& g ):
    dxlhs( dg::transfer<container>(dg::evaluate( one, g)) ), dxrhs(dxlhs), dylhs(dxlhs), dyrhs( dxlhs), helper_( dxlhs),
    bdxf( dg::create::dx( g, g.bcx())),
    bdyf( dg::create::dy( g, g.bcy()))
{
    metric_=g.metric().perp();
    perp_vol_inv_ = dg::tensor::determinant(metric_);
    dg::tensor::sqrt(perp_vol_inv_);
}
template<class Geometry, class Matrix, class container>
ArakawaX<Geometry, Matrix, container>::ArakawaX( const Geometry& g, bc bcx, bc bcy):
    dxlhs( dg::transfer<container>(dg::evaluate( one, g)) ), dxrhs(dxlhs), dylhs(dxlhs), dyrhs( dxlhs), helper_( dxlhs),
    bdxf(dg::create::dx( g, bcx)),
    bdyf(dg::create::dy( g, bcy))
{
    metric_=g.metric().perp();
    perp_vol_inv_ = dg::tensor::determinant(metric_);
    dg::tensor::sqrt(perp_vol_inv_);
}

template<class T>
struct ArakawaFunctor
{
    DG_DEVICE
    void operator()(T lhs, T rhs, T dxlhs, T& dylhs, T& dxrhs, T& dyrhs) const
    {
        T result = T(0);
        result = DG_FMA(  (1./3.)*dxlhs, dyrhs, result);
        result = DG_FMA( -(1./3.)*dylhs, dxrhs, result);
        T temp = T(0);
        temp = DG_FMA(  (1./3.)*lhs, dyrhs, temp);
        dyrhs = result;
        temp = DG_FMA( -(1./3.)*dylhs, rhs, temp);
        dylhs = temp;
        temp = T(0);
        temp = DG_FMA(  (1./3.)*dxlhs, rhs, temp);
        temp = DG_FMA( -(1./3.)*lhs, dxrhs, temp);
        dxrhs = temp;
    }
};

template< class Geometry, class Matrix, class container>
void ArakawaX< Geometry, Matrix, container>::operator()( const container& lhs, const container& rhs, container& result)
{
    //compute derivatives in x-space
    blas2::symv( bdxf, lhs, dxlhs);
    blas2::symv( bdyf, lhs, dylhs);
    blas2::symv( bdxf, rhs, dxrhs);
    blas2::symv( bdyf, rhs, result);
    blas1::subroutine( ArakawaFunctor<get_value_type<container>>(), lhs, rhs, dxlhs, dylhs, dxrhs, result);

    //blas1::pointwiseDot( 1./3., dxlhs, dyrhs, -1./3., dylhs, dxrhs, 0., result);
    //blas1::pointwiseDot( 1./3.,   lhs, dyrhs, -1./3., dylhs,   rhs, 0., dylhs);
    //blas1::pointwiseDot( 1./3., dxlhs,   rhs, -1./3.,   lhs, dxrhs, 0., dxrhs);

    blas2::symv( 1., bdxf, dylhs, 1., result);
    blas2::symv( 1., bdyf, dxrhs, 1., result);
    tensor::pointwiseDot( perp_vol_inv_, result, result);
}
///@endcond

}//namespace dg

#endif //_DG_ARAKAWA_CUH
