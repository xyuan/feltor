#pragma once

#include "dg/backend/mpi_vector.h"
#include "mpi_grid.h"
#include "evaluation.h"

/*! @file
  @brief Function discretization routines for mpi vectors
  */
namespace dg
{


///@addtogroup evaluation
///@{

/**
 * @brief Evaluate a function on gaussian abscissas
 *
 * Evaluates f(x) on the given grid
 * @copydoc hide_binary
 * @param f The function to evaluate: f = f(x,y)
 * @param g The 2d grid on which to evaluate f
 *
 * @return  A MPI Vector with values
 * @sa <a href="./dg_introduction.pdf" target="_blank">Introduction to dg methods</a>
 * @copydoc hide_code_mpi_evaluate2d
 */
template< class BinaryOp,class real_type>
MPI_Vector<thrust::host_vector<real_type> > evaluate( const BinaryOp& f, const aRealMPITopology2d<real_type>& g)
{
    //since the local grid is not binary compatible we have to use this implementation
    unsigned n = g.n();
    RealGrid2d<real_type> l = g.local();
    int dims[2], periods[2], coords[2];
    MPI_Cart_get( g.communicator(), 2, dims, periods, coords);
    thrust::host_vector<real_type> absx( l.n()*l.Nx());
    thrust::host_vector<real_type> absy( l.n()*l.Ny());
    for( unsigned i=0; i<l.Nx(); i++)
        for( unsigned j=0; j<n; j++)
        {
            unsigned coord = i+l.Nx()*coords[0];
            real_type xmiddle = DG_FMA( g.hx(), (real_type)(coord), g.x0());
            real_type h2 = g.hx()/2.;
            real_type absj = 1.+g.dlt().abscissas()[j];
            absx[i*n+j] = DG_FMA( h2, absj, xmiddle);
        }
    for( unsigned i=0; i<l.Ny(); i++)
        for( unsigned j=0; j<n; j++)
        {
            unsigned coord = i+l.Ny()*coords[1];
            real_type ymiddle = DG_FMA( g.hy(), (real_type)(coord), g.y0());
            real_type h2 = g.hy()/2.;
            real_type absj = 1.+g.dlt().abscissas()[j];
            absy[i*n+j] = DG_FMA( h2, absj, ymiddle );
        }

    thrust::host_vector<real_type> w( l.size());
    for( unsigned i=0; i<l.Ny(); i++)
        for( unsigned k=0; k<n; k++)
            for( unsigned j=0; j<l.Nx(); j++)
                for( unsigned r=0; r<n; r++)
                    w[ ((i*n+k)*l.Nx() + j)*n + r] = f( absx[j*n+r], absy[i*n+k]);
    MPI_Vector<thrust::host_vector<real_type> > v( w, g.communicator());
    return v;
};
///@cond
template<class real_type>
MPI_Vector<thrust::host_vector<real_type> > evaluate( real_type(f)(real_type, real_type), const aRealMPITopology2d<real_type>& g)
{
    return evaluate<real_type(real_type, real_type)>( *f, g);
};
///@endcond

/**
 * @brief Evaluate a function on gaussian abscissas
 *
 * Evaluates f(x,y,z) on the given grid
 * @copydoc hide_ternary
 * @param f The function to evaluate: f = f(x,y,z)
 * @param g The 3d grid on which to evaluate f
 *
 * @return  A MPI Vector with values
 * @sa <a href="./dg_introduction.pdf" target="_blank">Introduction to dg methods</a>
 * @copydoc hide_code_mpi_evaluate3d
 */
template< class TernaryOp,class real_type>
MPI_Vector<thrust::host_vector<real_type> > evaluate( const TernaryOp& f, const aRealMPITopology3d<real_type>& g)
{
    //since the local grid is not binary compatible we have to use this implementation
    unsigned n = g.n();
    //abscissas
    RealGrid3d<real_type> l = g.local();
    int dims[3], periods[3], coords[3];
    MPI_Cart_get( g.communicator(), 3, dims, periods, coords);
    thrust::host_vector<real_type> absx( l.n()*l.Nx());
    thrust::host_vector<real_type> absy( l.n()*l.Ny());
    thrust::host_vector<real_type> absz(       l.Nz());
    for( unsigned i=0; i<l.Nx(); i++)
        for( unsigned j=0; j<n; j++)
        {
            unsigned coord = i+l.Nx()*coords[0];
            real_type xmiddle = DG_FMA( g.hx(), (real_type)(coord), g.x0());
            real_type h2 = g.hx()/2.;
            real_type absj = 1.+g.dlt().abscissas()[j];
            absx[i*n+j] = DG_FMA( h2, absj, xmiddle);
        }
    for( unsigned i=0; i<l.Ny(); i++)
        for( unsigned j=0; j<n; j++)
        {
            unsigned coord = i+l.Ny()*coords[1];
            real_type ymiddle = DG_FMA( g.hy(), (real_type)(coord), g.y0());
            real_type h2 = g.hy()/2.;
            real_type absj = 1.+g.dlt().abscissas()[j];
            absy[i*n+j] = DG_FMA( h2, absj, ymiddle );
        }
    for( unsigned i=0; i<l.Nz(); i++)
    {
        unsigned coord = i+l.Nz()*coords[2];
        real_type zmiddle = DG_FMA( g.hz(), (real_type)(coord), g.z0());
        real_type h2 = g.hz()/2.;
        absz[i] = DG_FMA( h2, (1.), zmiddle );
    }

    thrust::host_vector<real_type> w( l.size());
    for( unsigned s=0; s<l.Nz(); s++)
        for( unsigned i=0; i<l.Ny(); i++)
            for( unsigned k=0; k<n; k++)
                for( unsigned j=0; j<l.Nx(); j++)
                    for( unsigned r=0; r<n; r++)
                        w[ (((s*l.Ny()+i)*n+k)*l.Nx() + j)*n + r] = f( absx[j*n+r], absy[i*n+k], absz[s]);
    MPI_Vector<thrust::host_vector<real_type> > v( w, g.communicator());
    return v;
};
///@cond
template<class real_type>
MPI_Vector<thrust::host_vector<real_type> > evaluate( real_type(f)(real_type, real_type, real_type), const aRealMPITopology3d<real_type>& g)
{
    return evaluate<real_type(real_type, real_type, real_type)>( *f, g);
};
///@endcond
//
///@}

/**
 * @brief Take the relevant local part of a global vector
 *
 * @param global a vector the size of the global grid
 * @param g the assumed topology
 * @return an MPI_Vector that is the distributed version of the global vector
 * @ingroup scatter
 */
template<class real_type>
MPI_Vector<thrust::host_vector<real_type> > global2local( const thrust::host_vector<real_type>& global, const aRealMPITopology3d<real_type>& g)
{
    assert( global.size() == g.global().size());
    RealGrid3d<real_type> l = g.local();
    thrust::host_vector<real_type> temp(l.size());
    int dims[3], periods[3], coords[3];
    MPI_Cart_get( g.communicator(), 3, dims, periods, coords);
    for( unsigned s=0; s<l.Nz(); s++)
        //for( unsigned py=0; py<dims[1]; py++)
            for( unsigned i=0; i<l.n()*l.Ny(); i++)
                //for( unsigned px=0; px<dims[0]; px++)
                    for( unsigned j=0; j<l.n()*l.Nx(); j++)
                    {
                        unsigned idx1 = (s*l.n()*l.Ny()+i)*l.n()*l.Nx() + j;
                        unsigned idx2 = (((s*dims[1]+coords[1])*l.n()*l.Ny()+i)*dims[0] + coords[0])*l.n()*l.Nx() + j;
                        temp[idx1] = global[idx2];
                    }
    return MPI_Vector<thrust::host_vector<real_type> >(temp, g.communicator());
}
/**
 * @copydoc global2local
 * @ingroup scatter
 */
template<class real_type>
MPI_Vector<thrust::host_vector<real_type> > global2local( const thrust::host_vector<real_type>& global, const aRealMPITopology2d<real_type>& g)
{
    assert( global.size() == g.global().size());
    RealGrid2d<real_type> l = g.local();
    thrust::host_vector<real_type> temp(l.size());
    int dims[2], periods[2], coords[2];
    MPI_Cart_get( g.communicator(), 2, dims, periods, coords);
    //for( unsigned py=0; py<dims[1]; py++)
        for( unsigned i=0; i<l.n()*l.Ny(); i++)
            //for( unsigned px=0; px<dims[0]; px++)
                for( unsigned j=0; j<l.n()*l.Nx(); j++)
                {
                    unsigned idx1 = i*l.n()*l.Nx() + j;
                    unsigned idx2 = ((coords[1]*l.n()*l.Ny()+i)*dims[0] + coords[0])*l.n()*l.Nx() + j;
                    temp[idx1] = global[idx2];
                }
    return MPI_Vector<thrust::host_vector<real_type> >(temp, g.communicator());
}

}//namespace dg

