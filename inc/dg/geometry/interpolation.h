#pragma once
//#include <iomanip>

#include <cusp/coo_matrix.h>
#include <cusp/csr_matrix.h>
#include "grid.h"
#include "evaluation.h"
#include "functions.h"
#include "creation.h"
#include "operator_tensor.h"

/*! @file

  @brief contains 1D, 2D and 3D interpolation matrix creation functions
  */

namespace dg{
///@addtogroup typedefs
///@{
template<class real_type>
using tIHMatrix = cusp::csr_matrix<int, real_type, cusp::host_memory>;
template<class real_type>
using tIDMatrix = cusp::csr_matrix<int, real_type, cusp::device_memory>;
using IHMatrix = tIHMatrix<double>;
using IDMatrix = tIDMatrix<double>;
//typedef cusp::csr_matrix<int, double, cusp::host_memory> IHMatrix; //!< CSR host Matrix
//typedef cusp::csr_matrix<int, double, cusp::device_memory> IDMatrix; //!< CSR device Matrix

///@}

namespace create{
    ///@cond
namespace detail{

/**
 * @brief Evaluate n Legendre poloynomial on given abscissa
 *
 * @param xn normalized x-value on which to evaluate the polynomials: -1<=xn<=1
 * @param n  maximum order of the polynomial
 *
 * @return array of coefficients beginning with p_0(x_n) until p_{n-1}(x_n)
 */
template<class real_type>
std::vector<real_type> coefficients( real_type xn, unsigned n)
{
    assert( xn <= 1. && xn >= -1.);
    std::vector<real_type> px(n);
    if( xn == -1)
    {
        for( unsigned i=0; i<n; i++)
            px[i] = (real_type)pow( -1, i);
    }
    else if( xn == 1)
    {
        for( unsigned i=0; i<n; i++)
            px[i] = 1.;
    }
    else
    {
        px[0] = 1.;
        if( n > 1)
        {
            px[1] = xn;
            for( unsigned i=1; i<n-1; i++)
                px[i+1] = ((real_type)(2*i+1)*xn*px[i]-(real_type)i*px[i-1])/(real_type)(i+1);
        }
    }
    return px;
}

}//namespace detail
///@endcond
///@addtogroup interpolation
///@{
/**
 * @brief Create interpolation matrix
 *
 * The created matrix has \c g.size() columns and \c x.size() rows. It uses
 * polynomial interpolation given by the dG polynomials, i.e. the interpolation has order \c g.n() .
 * When applied to a vector the result contains the interpolated values at the given interpolation points.
 * @sa <a href="./dg_introduction.pdf" target="_blank">Introduction to dg methods</a>
 * @param x X-coordinates of interpolation points
 * @param g The Grid on which to operate
 *
 * @return interpolation matrix
 */
template<class real_type>
cusp::coo_matrix<int, real_type, cusp::host_memory> interpolation( const thrust::host_vector<real_type>& x, const RealGrid1d<real_type>& g)
{
    cusp::coo_matrix<int, real_type, cusp::host_memory> A( x.size(), g.size(), x.size()*g.n());

    int number = 0;
    dg::Operator<real_type> forward( g.dlt().forward());
    for( unsigned i=0; i<x.size(); i++)
    {
        if (!(x[i] >= g.x0() && x[i] <= g.x1())) {
            std::cerr << "xi = " << x[i] <<std::endl;
        }
        assert(x[i] >= g.x0() && x[i] <= g.x1());

        //determine which cell (x) lies in
        real_type xnn = (x[i]-g.x0())/g.h();
        unsigned n = (unsigned)floor(xnn);
        //determine normalized coordinates
        real_type xn = 2.*xnn - (real_type)(2*n+1);
        //intervall correction
        if (n==g.N()) {
            n-=1;
            xn = 1.;
        }
        //evaluate 2d Legendre polynomials at (xn, yn)...
        std::vector<real_type> px = detail::coefficients( xn, g.n());
        //...these are the matrix coefficients with which to multiply
        std::vector<real_type> pxF(px.size(),0);
        for( unsigned l=0; l<g.n(); l++)
            for( unsigned k=0; k<g.n(); k++)
                pxF[l]+= px[k]*forward(k,l);
        unsigned col_begin = n*g.n();
        detail::add_line( A, number, i,  col_begin, pxF);
    }
    return A;
}

/**
 * @brief Create interpolation matrix
 *
 * The created matrix has \c g.size() columns and \c x.size() rows. It uses
 * polynomial interpolation given by the dG polynomials, i.e. the interpolation has order \c g.n() .
 * When applied to a vector the result contains the interpolated values at the given interpolation points.
 * @snippet geometry/interpolation_t.cu doxygen
 * @sa <a href="./dg_introduction.pdf" target="_blank">Introduction to dg methods</a>
 * @param x X-coordinates of interpolation points
 * @param y Y-coordinates of interpolation points ( has to have equal size as x)
 * @param g The Grid on which to operate
 * @param bcx determines what to do when a point lies exactly on the boundary in x:  DIR generates zeroes in the interpolation matrix,
 NEU and PER interpolate the inner side polynomial. (DIR_NEU and NEU_DIR apply NEU / DIR to the respective left or right boundary )
 * @param bcy determines what to do when a point lies exactly on the boundary in y. Behaviour correponds to bcx.
 *
 * @return interpolation matrix
 * @attention all points (x,y) must lie within or on the boundaries of g.
 */
template<class real_type>
cusp::coo_matrix<int, real_type, cusp::host_memory> interpolation( const thrust::host_vector<real_type>& x, const thrust::host_vector<real_type>& y, const aRealTopology2d<real_type>& g , dg::bc bcx = dg::NEU, dg::bc bcy = dg::NEU)
{
    assert( x.size() == y.size());
    std::vector<real_type> gauss_nodes = g.dlt().abscissas();
    dg::Operator<real_type> forward( g.dlt().forward());
    cusp::array1d<real_type, cusp::host_memory> values;
    cusp::array1d<int, cusp::host_memory> row_indices;
    cusp::array1d<int, cusp::host_memory> column_indices;

    for( int i=0; i<(int)x.size(); i++)
    {
        //assert that point is inside the grid boundaries
        if (!(x[i] >= g.x0() && x[i] <= g.x1())) {
            std::cerr << g.x0()<<"< xi = " << x[i] <<" < "<<g.x1()<<std::endl;
        }
        assert(x[i] >= g.x0() && x[i] <= g.x1());
        if (!(y[i] >= g.y0() && y[i] <= g.y1())) {
            std::cerr << g.y0()<<"< yi = " << y[i] <<" < "<<g.y1()<<std::endl;
        }
        assert( y[i] >= g.y0() && y[i] <= g.y1());

        //determine which cell (x,y) lies in
        real_type xnn = (x[i]-g.x0())/g.hx();
        real_type ynn = (y[i]-g.y0())/g.hy();
        unsigned nn = (unsigned)floor(xnn);
        unsigned mm = (unsigned)floor(ynn);
        //determine normalized coordinates
        real_type xn =  2.*xnn - (real_type)(2*nn+1);
        real_type yn =  2.*ynn - (real_type)(2*mm+1);
        //interval correction
        if (nn==g.Nx()) {
            nn-=1;
            xn = 1.;
        }
        if (mm==g.Ny()) {
            mm-=1;
            yn =1.;
        }
        //Test if the point is a Gauss point since then no interpolation is needed
        int idxX =-1, idxY = -1;
        for( unsigned k=0; k<g.n(); k++)
        {
            if( fabs( xn - gauss_nodes[k]) < 1e-14)
                idxX = nn*g.n() + k; //determine which grid column it is
            if( fabs( yn - gauss_nodes[k]) < 1e-14)
                idxY = mm*g.n() + k;  //determine grid line
        }
        if( idxX < 0 && idxY < 0 ) //there is no corresponding point
        {
            //evaluate 2d Legendre polynomials at (xn, yn)...
            std::vector<real_type> px = detail::coefficients( xn, g.n()),
                                py = detail::coefficients( yn, g.n());
            std::vector<real_type> pxF(g.n(),0), pyF(g.n(), 0);
            for( unsigned l=0; l<g.n(); l++)
                for( unsigned k=0; k<g.n(); k++)
                {
                    pxF[l]+= px[k]*forward(k,l);
                    pyF[l]+= py[k]*forward(k,l);
                }
            std::vector<real_type> pxy( g.n()*g.n());
            //these are the matrix coefficients with which to multiply
            for(unsigned k=0; k<pyF.size(); k++)
                for( unsigned l=0; l<pxF.size(); l++)
                    pxy[k*px.size()+l]= pyF[k]*pxF[l];
            if (  (x[i] == g.x0() && (bcx==dg::DIR || bcx==dg::DIR_NEU) )
                ||(x[i] == g.x1() && (bcx==dg::DIR || bcx==dg::NEU_DIR) )
                ||(y[i] == g.y0() && (bcy==dg::DIR || bcy==dg::DIR_NEU) )
                ||(y[i] == g.y1() && (bcy==dg::DIR || bcy==dg::NEU_DIR) ))
            {
                //zeroe boundary values
                for(unsigned k=0; k<py.size(); k++)
                for( unsigned l=0; l<px.size(); l++)
                    pxy[k*px.size()+l]= 0;
            }
            for( unsigned k=0; k<g.n(); k++)
                for( unsigned l=0; l<g.n(); l++)
                {
                    row_indices.push_back( i);
                    column_indices.push_back( (mm*g.n()+k)*g.n()*g.Nx()+nn*g.n() + l);
                    values.push_back( pxy[k*g.n()+l]);
                }
        }
        else if ( idxX < 0 && idxY >=0) //there is a corresponding line
        {
            std::vector<real_type> px = detail::coefficients( xn, g.n());
            std::vector<real_type> pxF(g.n(),0);
            for( unsigned l=0; l<g.n(); l++)
                for( unsigned k=0; k<g.n(); k++)
                    pxF[l]+= px[k]*forward(k,l);
            for( unsigned l=0; l<g.n(); l++)
            {
                row_indices.push_back( i);
                column_indices.push_back( (idxY)*g.Nx()*g.n() + nn*g.n() + l);
                values.push_back( pxF[l]);
            }
        }
        else if ( idxX >= 0 && idxY < 0) //there is a corresponding column
        {
            std::vector<real_type> py = detail::coefficients( yn, g.n());
            std::vector<real_type> pyF(g.n(),0);
            for( unsigned l=0; l<g.n(); l++)
                for( unsigned k=0; k<g.n(); k++)
                    pyF[l]+= py[k]*forward(k,l);
            for( unsigned k=0; k<g.n(); k++)
            {
                row_indices.push_back(i);
                column_indices.push_back((mm*g.n()+k)*g.Nx()*g.n() + idxX);
                values.push_back(pyF[k]);
            }
        }
        else //the point already exists
        {
            row_indices.push_back(i);
            column_indices.push_back(idxY*g.Nx()*g.n() + idxX);
            values.push_back(1.);
        }

    }
    cusp::coo_matrix<int, real_type, cusp::host_memory> A( x.size(), g.size(), values.size());
    A.row_indices = row_indices; A.column_indices = column_indices; A.values = values;

    return A;
}



/**
 * @brief Create interpolation matrix
 *
 * The created matrix has \c g.size() columns and \c x.size() rows. It uses
 * polynomial interpolation given by the dG polynomials, i.e. the interpolation has order \c g.n() .
 * When applied to a vector the result contains the interpolated values at the given interpolation points.
 * @snippet geometry/interpolation_t.cu doxygen3d
 * @sa <a href="./dg_introduction.pdf" target="_blank">Introduction to dg methods</a>
 * @param x X-coordinates of interpolation points
 * @param y Y-coordinates of interpolation points
 * @param z Z-coordinates of interpolation points
 * @param g The Grid on which to operate
 * @param bcx determines what to do when a point lies exactly on the boundary in x:  DIR generates zeroes in the interpolation matrix,
 NEU and PER interpolate the inner side polynomial. (DIR_NEU and NEU_DIR apply NEU / DIR to the respective left or right boundary )
 * @param bcy determines what to do when a point lies exactly on the boundary in y. Behaviour correponds to bcx.
 *
 * @return interpolation matrix
 * @attention all points (x, y, z) must lie within or on the boundaries of g
 */
template<class real_type>
cusp::coo_matrix<int, real_type, cusp::host_memory> interpolation( const thrust::host_vector<real_type>& x, const thrust::host_vector<real_type>& y, const thrust::host_vector<real_type>& z, const aRealTopology3d<real_type>& g, dg::bc bcx = dg::NEU, dg::bc bcy = dg::NEU)
{
    assert( x.size() == y.size());
    assert( y.size() == z.size());
    std::vector<real_type> gauss_nodes = g.dlt().abscissas();
    dg::Operator<real_type> forward( g.dlt().forward());
    cusp::array1d<real_type, cusp::host_memory> values;
    cusp::array1d<int, cusp::host_memory> row_indices;
    cusp::array1d<int, cusp::host_memory> column_indices;

    for( int i=0; i<(int)x.size(); i++)
    {
        //assert that point is inside the grid boundaries
        if (!(x[i] >= g.x0() && x[i] <= g.x1())) {
            std::cerr << g.x0()<<"< xi = " << x[i] <<" < "<<g.x1()<<std::endl;
        } assert(x[i] >= g.x0() && x[i] <= g.x1());
        if (!(y[i] >= g.y0() && y[i] <= g.y1())) {
            std::cerr << g.y0()<<"< yi = " << y[i] <<" < "<<g.y1()<<std::endl;
        } assert( y[i] >= g.y0() && y[i] <= g.y1());
        if (!(z[i] >= g.z0() && z[i] <= g.z1())) {
            std::cerr << g.z0()<<"< zi = " << z[i] <<" < "<<g.z1()<<std::endl;
        } assert( z[i] >= g.z0() && z[i] <= g.z1());

        //determine which cell (x,y) lies in
        real_type xnn = (x[i]-g.x0())/g.hx();
        real_type ynn = (y[i]-g.y0())/g.hy();
        real_type znn = (z[i]-g.z0())/g.hz();
        unsigned nn = (unsigned)floor(xnn);
        unsigned mm = (unsigned)floor(ynn);
        unsigned ll = (unsigned)floor(znn);
        //determine normalized coordinates
        real_type xn = 2.*xnn - (real_type)(2*nn+1);
        real_type yn = 2.*ynn - (real_type)(2*mm+1);
        //interval correction
        if (nn==g.Nx()) {
            nn-=1;
            xn = 1.;
        }
        if (mm==g.Ny()) {
            mm-=1;
            yn =1.;
        }
        if (ll==g.Nz()) {
            ll-=1;
        }
        //Test if the point is a Gauss point since then no interpolation is needed
        int idxX =-1, idxY = -1;
        for( unsigned k=0; k<g.n(); k++)
        {
            if( fabs( xn - gauss_nodes[k]) < 1e-14)
                idxX = nn*g.n() + k; //determine which grid column it is
            if( fabs( yn - gauss_nodes[k]) < 1e-14)
                idxY = mm*g.n() + k;  //determine grid line
        } //in z-direction we don't interpolate
        if( idxX < 0 && idxY < 0 ) //there is no corresponding point
        {
            //evaluate 2d Legendre polynomials at (xn, yn)...
            std::vector<real_type> px = detail::coefficients( xn, g.n()),
                                py = detail::coefficients( yn, g.n());
            std::vector<real_type> pxF(g.n(),0), pyF(g.n(), 0);
            for( unsigned l=0; l<g.n(); l++)
                for( unsigned k=0; k<g.n(); k++)
                {
                    pxF[l]+= px[k]*forward(k,l);
                    pyF[l]+= py[k]*forward(k,l);
                }
            std::vector<real_type> pxyz( g.n()*g.n());
            //these are the matrix coefficients with which to multiply
            for(unsigned k=0; k<pyF.size(); k++)
                for( unsigned l=0; l<pxF.size(); l++)
                    pxyz[k*g.n()+l]= 1.*pyF[k]*pxF[l];
            if (  (x[i] == g.x0() && (bcx==dg::DIR || bcx==dg::DIR_NEU) )
                ||(x[i] == g.x1() && (bcx==dg::DIR || bcx==dg::NEU_DIR) )
                ||(y[i] == g.y0() && (bcy==dg::DIR || bcy==dg::DIR_NEU) )
                ||(y[i] == g.y1() && (bcy==dg::DIR || bcy==dg::NEU_DIR) ))
            {
                //zeroe boundary values
                for(unsigned k=0; k<g.n(); k++)
                for(unsigned l=0; l<g.n(); l++)
                    pxyz[k*g.n()+l]= 0;
            }
            for( unsigned k=0; k<g.n(); k++)
                for( unsigned l=0; l<g.n(); l++)
                {
                    row_indices.push_back( i);
                    column_indices.push_back( ((ll*g.Ny()+mm)*g.n()+k)*g.n()*g.Nx()+nn*g.n() + l);
                    values.push_back( pxyz[k*g.n()+l]);
                }
        }
        else if ( idxX < 0 && idxY >=0) //there is a corresponding line
        {
            std::vector<real_type> px = detail::coefficients( xn, g.n());
            std::vector<real_type> pxF(g.n(),0);
            for( unsigned l=0; l<g.n(); l++)
                for( unsigned k=0; k<g.n(); k++)
                    pxF[l]+= px[k]*forward(k,l);
            for( unsigned l=0; l<g.n(); l++)
            {
                row_indices.push_back( i);
                column_indices.push_back( (ll*g.Ny()*g.n() + idxY)*g.Nx()*g.n() + nn*g.n() + l);
                values.push_back( pxF[l]);
            }
        }
        else if ( idxX >= 0 && idxY < 0) //there is a corresponding column
        {
            std::vector<real_type> py = detail::coefficients( yn, g.n());
            std::vector<real_type> pyF(g.n(),0);
            for( unsigned l=0; l<g.n(); l++)
                for( unsigned k=0; k<g.n(); k++)
                    pyF[l]+= py[k]*forward(k,l);
            for( unsigned k=0; k<g.n(); k++)
            {
                row_indices.push_back(i);
                column_indices.push_back(((ll*g.Ny()+mm)*g.n()+k)*g.Nx()*g.n() + idxX);
                values.push_back(pyF[k]);
            }
        }
        else //the point already exists
        {
            row_indices.push_back(i);
            column_indices.push_back((ll*g.Ny()*g.n()+idxY)*g.Nx()*g.n() + idxX);
            values.push_back(1.);
        }

    }
    cusp::coo_matrix<int, real_type, cusp::host_memory> A( x.size(), g.size(), values.size());
    A.row_indices = row_indices; A.column_indices = column_indices; A.values = values;

    return A;
}
/**
 * @brief Create interpolation between two grids
 *
 * This matrix interpolates vectors on the old grid \c g_old to the %Gaussian nodes of the new grid \c g_new. The interpolation is of the order \c g_old.n()
 * @sa <a href="./dg_introduction.pdf" target="_blank">Introduction to dg methods</a>
 *
 * @param g_new The new grid
 * @param g_old The old grid
 *
 * @return Interpolation matrix with \c g_old.size() columns and \c g_new.size() rows
 * @note The boundaries of the old grid must lie within the boundaries of the new grid
 * @note also check the transformation matrix, which is the more general solution
 */
template<class real_type>
cusp::coo_matrix<int, real_type, cusp::host_memory> interpolation( const RealGrid1d<real_type>& g_new, const RealGrid1d<real_type>& g_old)
{
    //assert both grids are on the same box
    assert( g_new.x0() >= g_old.x0());
    assert( g_new.x1() <= g_old.x1());
    thrust::host_vector<real_type> pointsX = dg::evaluate( dg::cooX1d, g_new);
    return interpolation( pointsX, g_old);

}
///@copydoc interpolation(const RealGrid1d<real_type>&,const RealGrid1d<real_type>&)
template<class real_type>
cusp::coo_matrix<int, real_type, cusp::host_memory> interpolation( const aRealTopology2d<real_type>& g_new, const aRealTopology2d<real_type>& g_old)
{
    //assert both grids are on the same box
    assert( g_new.x0() >= g_old.x0());
    assert( g_new.x1() <= g_old.x1());
    assert( g_new.y0() >= g_old.y0());
    assert( g_new.y1() <= g_old.y1());
    thrust::host_vector<real_type> pointsX = dg::evaluate( dg::cooX2d, g_new);

    thrust::host_vector<real_type> pointsY = dg::evaluate( dg::cooY2d, g_new);
    return interpolation( pointsX, pointsY, g_old);

}

///@copydoc interpolation(const RealGrid1d<real_type>&,const RealGrid1d<real_type>&)
template<class real_type>
cusp::coo_matrix<int, real_type, cusp::host_memory> interpolation( const aRealTopology3d<real_type>& g_new, const aRealTopology3d<real_type>& g_old)
{
    //assert both grids are on the same box
    assert( g_new.x0() >= g_old.x0());
    assert( g_new.x1() <= g_old.x1());
    assert( g_new.y0() >= g_old.y0());
    assert( g_new.y1() <= g_old.y1());
    assert( g_new.z0() >= g_old.z0());
    assert( g_new.z1() <= g_old.z1());
    thrust::host_vector<real_type> pointsX = dg::evaluate( dg::cooX3d, g_new);
    thrust::host_vector<real_type> pointsY = dg::evaluate( dg::cooY3d, g_new);
    thrust::host_vector<real_type> pointsZ = dg::evaluate( dg::cooZ3d, g_new);
    return interpolation( pointsX, pointsY, pointsZ, g_old);

}
///@}


/**
 * @brief Transform a vector from XSPACE to LSPACE
 *
 * @param in input
 * @param g grid
 *
 * @ingroup misc
 * @return the vector in LSPACE
 */
template<class real_type>
thrust::host_vector<real_type> forward_transform( const thrust::host_vector<real_type>& in, const aRealTopology2d<real_type>& g)
{
    thrust::host_vector<real_type> out(in.size(), 0);
    dg::Operator<real_type> forward( g.dlt().forward());
    for( unsigned i=0; i<g.Ny(); i++)
    for( unsigned k=0; k<g.n(); k++)
    for( unsigned j=0; j<g.Nx(); j++)
    for( unsigned l=0; l<g.n(); l++)
    for( unsigned m=0; m<g.n(); m++)
    for( unsigned o=0; o<g.n(); o++)
        out[((i*g.n() + k)*g.Nx() + j)*g.n() + l] += forward(k,o)*forward( l, m)*in[((i*g.n() + o)*g.Nx() + j)*g.n() + m];
    return out;
}

}//namespace create

/**
 * @brief Interpolate a single point
 *
 * @param x X-coordinate of interpolation point
 * @param y Y-coordinate of interpolation point
 * @param v The vector to interpolate in LSPACE, s.a. dg::forward_transform( )
 * @param g The Grid on which to operate
 *
 * @ingroup interpolation
 * @return interpolated point
 * @note \c g.contains(x,y) must return true
 */
template<class real_type>
real_type interpolate( real_type x, real_type y,  const thrust::host_vector<real_type>& v, const aRealTopology2d<real_type>& g )
{
    assert( v.size() == g.size());

    if (!(x >= g.x0() && x <= g.x1())) {
        std::cerr << g.x0()<<"< xi = " << x <<" < "<<g.x1()<<std::endl;
    }

    assert(x >= g.x0() && x <= g.x1());

    if (!(y >= g.y0() && y <= g.y1())) {
        std::cerr << g.y0()<<"< yi = " << y <<" < "<<g.y1()<<std::endl;
    }
    assert( y >= g.y0() && y <= g.y1());

    //determine which cell (x,y) lies in

    real_type xnn = (x-g.x0())/g.hx();
    real_type ynn = (y-g.y0())/g.hy();
    unsigned n = (unsigned)floor(xnn);
    unsigned m = (unsigned)floor(ynn);
    //determine normalized coordinates

    real_type xn =  2.*xnn - (real_type)(2*n+1);
    real_type yn =  2.*ynn - (real_type)(2*m+1);
    //interval correction
    if (n==g.Nx()) {
        n-=1;
        xn = 1.;
    }
    if (m==g.Ny()) {
        m-=1;
        yn =1.;
    }
    //evaluate 2d Legendre polynomials at (xn, yn)...
    std::vector<real_type> px = create::detail::coefficients( xn, g.n()),
                        py = create::detail::coefficients( yn, g.n());
    //dg::Operator<real_type> forward( g.dlt().forward());
    //std::vector<real_type> pxF(g.n(),0), pyF(g.n(), 0);
    //for( unsigned l=0; l<g.n(); l++)
    //    for( unsigned k=0; k<g.n(); k++)
    //    {
    //        pxF[l]+= px[k]*forward(k,l);
    //        pyF[l]+= py[k]*forward(k,l);
    //    }
    //these are the matrix coefficients with which to multiply
    unsigned col_begin = (m)*g.Nx()*g.n()*g.n() + (n)*g.n();
    //multiply x
    real_type value = 0;
    for( unsigned i=0; i<g.n(); i++)
        for( unsigned j=0; j<g.n(); j++)
            value += v[col_begin + i*g.Nx()*g.n() + j]*px[j]*py[i];
            //value += v[col_begin + i*g.Nx()*g.n() + j]*pxF[j]*pyF[i];
    return value;
}

} //namespace dg
