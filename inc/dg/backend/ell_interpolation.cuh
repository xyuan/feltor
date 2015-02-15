#pragma once
#include <cusp/system/cuda/arch.h>
#include <cusp/system/cuda/utils.h>
#include "../blas.h"
#include "../functors.h"



namespace dg
{

namespace create
{
namespace detail
{
void __host__ __device__ legendre( double* pxn, const double xn, const unsigned n, const double* forward)

{
    pxn[0] = 1.;
    if( n > 1)
    {
        pxn[1] = xn;
        for( unsigned i=1; i<n-1; i++)
            pxn[i+1] = ((double)(2*i+1)*xn*pxn[i]-(double)i*pxn[i-1])/(double)(i+1);
        double temp[4]; //it's a bit faster with less elements
        for( unsigned k=0; k<n; k++)
        {
            temp[k] = 0;
            for( unsigned i=0; i<n; i++)
                temp[k] += pxn[i]*forward[i*n+k];
        }
        for( unsigned k=0; k<n; k++)
            pxn[k] = temp[k];
    }
}

template<size_t BLOCK_SIZE>
__launch_bounds__(BLOCK_SIZE, 1) //cuda performance hint macro, (max_threads_per_block, minBlocksPerMultiprocessor)
 __global__ void interpolation_kernel1d( 
         const int num_rows, 
         const int n, 
         const int* cellx, const double* xn, 
         const int pitch, int* Aj, double* Av, 
         const double* forward)
{
    const int thread_id = blockDim.x * blockIdx.x + threadIdx.x;
    const int grid_size = gridDim.x*blockDim.x;
    //every thread takes num_rows/grid_size rows
    for( int row = thread_id; row<num_rows; row += grid_size)
    {
        double px[4];
        detail::legendre( px, xn[row], n, forward);
        
        int offset = row;
        for(int k=0; k<n; k++)
        {
            Aj[offset] = cellx[row]*n + k
            Av[offset] = px[k];
            offset +=pitch;
        }
    }

}

template<size_t BLOCK_SIZE>
__launch_bounds__(BLOCK_SIZE, 1) //cuda performance hint macro, (max_threads_per_block, minBlocksPerMultiprocessor)
 __global__ void interpolation_kernel2d( 
         const int num_rows, const int n, const int Nx, 
         const int* cellx, const int* celly, 
         const double* xn, const double * yn, 
         const int pitch, int* Aj, double* Av, 
         const double * forward)
{
    const int thread_id = blockDim.x * blockIdx.x + threadIdx.x;
    const int grid_size = gridDim.x*blockDim.x;
    //every thread takes num_rows/grid_size rows
    for( int row = thread_id; row<num_rows; row += grid_size)
    {
        //evaluate 2d Legendre polynomials at (xn, yn)...
        double px[4], py[4];
        detail::legendre( px, xn[row], n, forward); 
        detail::legendre( py, yn[row], n, forward);
        
        int offset = row;
        //the column is the row in the vector with which to multiply
        //int col_begin = celly[row]*Nx*n*n + cellx[row]*n;
        for( int k=0; k<n; k++)
            for( int l=0; l<n; l++)
            {
                //Aj[offset] = col_begin + k*n*Nx + l;
                Aj[offset] = (celly[row]*n+k)*n*Nx + cellx[row]*n+l
                Av[offset] = py[k]*px[l];
                offset +=pitch;
            }
    }

}

template<size_t BLOCK_SIZE>
__launch_bounds__(BLOCK_SIZE, 1) //cuda performance hint macro, (max_threads_per_block, minBlocksPerMultiprocessor)
 __global__ void interpolation_kernel3d( 
         const int num_rows, const int n, const int Nx, const int Ny, const int Nz, 
         const int* cellx, const int* celly, const int *cellz,
         const double* xn, const double * yn, const double *zn,
         const int pitch, int* Aj, double* Av, 
         const double * forward)
{
    const int thread_id = blockDim.x * blockIdx.x + threadIdx.x;
    const int grid_size = gridDim.x*blockDim.x;
    //every thread takes num_rows/grid_size rows
    for( int row = thread_id; row<num_rows; row += grid_size)
    {
        //evaluate 2d Legendre polynomials at (xn, yn)...
        double px[4], py[4], pz[2] = {zn[row], 1-zn[row]};
        detail::legendre( px, xn[row], n, forward); 
        detail::legendre( py, yn[row], n, forward);
        
        int offset = row;
        for( int m=0; m<2; m++)
            for( int k=0; k<n; k++)
                for( int l=0; l<n; l++)
                {
                    Aj[offset] = ((cellz[row] +m)%Nz)*n*n*Nx*Ny  (celly[row]*n+k)*n*Nx + (cellx[row]*n + l);

                    Av[offset] = pz[m]*py[k]*px[l];
                    offset +=pitch;
                }
    }

}

 void interpolation_kernel2dcpu( 
         const int num_rows, const int n, const int Nx, 
         const int* cellx, const int* celly, 
         const double* xn, const double * yn, 
         const int pitch, int* Aj, double* Av, 
         const double * forward)
{
    for( int row = 0; row<num_rows; row ++)
    {
        //evaluate 2d Legendre polynomials at (xn, yn)...
        double px[4], py[4];
        detail::legendre( px, xn[row], n, forward); 
        detail::legendre( py, yn[row], n, forward);
        
        int offset = row;
        int col_begin = celly[row]*Nx*n*n + cellx[row]*n;
        //evaluate 2d Legendre polynomials at (xn)...
        for( int k=0; k<n; k++)
            for( int l=0; l<n; l++)
            {
                Aj[offset] = col_begin + k*n*Nx + l;
                Av[offset] = py[k]*px[l];
                offset +=pitch;
            }
    }

}

} //namespace detail 

/**
 * @brief Create an interpolation matrix on the device
 *
 * @param x Vector of x-values
 * @param g Grid on which to interpolate 
 *
 * @return interpolation matrix
 * @note n must be smaller than 5
 * @attention no range check is performed on the input vectors
 */
cusp::ell_matrix<double, int, cusp::device_memory> ell_interpolation( const thrust::device_vector<double>& x, const Grid1d<double>& g  )
{
    assert( g.n()<=4);
    //allocate ell matrix storage
    cusp::ell_matrix<int, double, cusp::device_memory> A( x.size(), g.size(), x.size()*g.n(), g.n());

    //set up kernel parameters
    const size_t BLOCK_SIZE = 256;
    const size_t MAX_BLOCKS = cusp::system::cuda::detail::max_active_blocks( 
            detail::interpolation_kernel1d<BLOCK_SIZE>, 
            BLOCK_SIZE, (size_t) 0  );
    const size_t NUM_BLOCKS = std::min<size_t>( 
            MAX_BLOCKS, 
            cusp::system::cuda::DIVIDE_INTO( A.num_rows, BLOCK_SIZE));
    const int pitch = A.column_indices.pitch; //ell has columen major format
    int* Aj = thrust::raw_pointer_cast(&A.column_indices(0,0));
    double * Av = thrust::raw_pointer_cast(&A.values(0,0));

    //compute normalized x values and cell numbers
    thrust::device_vector<double> xn(x.size()), cellxh(x.size());
    dg::blas1::transform( x, xn, dg::PLUS<double>( -g.x0()));
    dg::blas1::scal( xn, 1./g.h());
    thrust::device_vector<int> cellx( x.size());
    thrust::transform( xn.begin(), xn.end(), cellx.begin(), dg::FLOOR());
    thrust::transform( cellx.begin(), cellx.end(),cellxh.begin(), dg::PLUS<double>(0.5));
    const int* cellx_ptr = thrust::raw_pointer_cast( &cellx[0]);
    const double* xn_ptr = thrust::raw_pointer_cast( &xn[0]);
    //xn = 2*xn - 2*(cellx+0.5)
    const thrust::device_vector<double> forward(std::vector<double> ( g.dlt().forward()));
    const double* forward_ptr = thrust::raw_pointer_cast( &forward[0]);
    dg::blas1::axpby( 2., xn, -2., cellxh, xn);
    detail::interpolation_kernel1d<BLOCK_SIZE> <<<NUM_BLOCKS, BLOCK_SIZE>>> ( A.num_rows, g.n(), cellx_ptr, xn_ptr, pitch, Aj, Av, forward_ptr);
    return A;

}

/**
 * @brief Create an interpolation matrix on the device
 *
 * @param x Vector of x-values
 * @param y Vector of y-values
 * @param g 2D Grid on which to interpolate 
 *
 * @return interpolation matrix
 * @note n must be smaller than 5
 * @attention no range check is performed on the input vectors
 */
cusp::ell_matrix<int, double, cusp::device_memory> ell_interpolation( const thrust::device_vector<double>& x, const thrust::device_vector<double>& y, const Grid2d<double>& g  )
{
    Timer t;
    t.tic();
    assert( x.size() == y.size());
    //allocate ell matrix storage
    cusp::ell_matrix<int, double, cusp::device_memory> A( x.size(), g.size(), x.size()*g.n()*g.n(), g.n()*g.n());

    //set up kernel parameters
    const size_t BLOCK_SIZE = 256;
    const size_t MAX_BLOCKS = cusp::system::cuda::detail::max_active_blocks( detail::interpolation_kernel2d<BLOCK_SIZE>, BLOCK_SIZE, (size_t) 0  );
    const size_t NUM_BLOCKS = std::min<size_t>( 
            MAX_BLOCKS, 
            cusp::system::cuda::DIVIDE_INTO( A.num_rows, BLOCK_SIZE));
    const int pitch = A.column_indices.pitch; //# of cols in memory
    assert( pitch == A.values.pitch);
    int* Aj = thrust::raw_pointer_cast(&A.column_indices(0,0));
    double * Av = thrust::raw_pointer_cast(&A.values(0,0));

    //compute normalized x and y values and cell numbers
    thrust::device_vector<double> xn(x.size()), yn(y.size()), cellxh(x.size()), cellyh(x.size());
    dg::blas1::transform( x, xn, dg::PLUS<double>( -g.x0()));
    dg::blas1::transform( y, yn, dg::PLUS<double>( -g.y0()));
    dg::blas1::scal( xn, 1./g.hx());
    dg::blas1::scal( yn, 1./g.hy());
    thrust::device_vector<int> cellX( x.size());
    thrust::device_vector<int> cellY( y.size());
    thrust::transform( xn.begin(), xn.end(), cellX.begin(), dg::FLOOR());
    thrust::transform( yn.begin(), yn.end(), cellY.begin(), dg::FLOOR());
    thrust::transform( cellX.begin(), cellX.end(),cellxh.begin(), dg::PLUS<double>(0.5));
    thrust::transform( cellY.begin(), cellY.end(),cellyh.begin(), dg::PLUS<double>(0.5));
    const int* cellX_ptr = thrust::raw_pointer_cast( &cellX[0]);
    const int* cellY_ptr = thrust::raw_pointer_cast( &cellY[0]);
    const double* xn_ptr = thrust::raw_pointer_cast( &xn[0]);
    const double* yn_ptr = thrust::raw_pointer_cast( &yn[0]);
    //xn = 2*xn - 2*(cellx+0.5)
    dg::blas1::axpby( 2., xn, -2., cellxh, xn);
    dg::blas1::axpby( 2., yn, -2., cellyh, yn);
    thrust::device_vector<double> forward(std::vector<double> ( g.dlt().forward()));
    const double * forward_ptr = thrust::raw_pointer_cast( &forward[0]);
    //detail::interpolation_kernel2dcpu( A.num_rows, g.n(), g.Nx(), 
    t.toc();
    std::cout << "Prekernel took "<<t.diff()<<"s\n";
    t.tic();
    detail::interpolation_kernel2d<BLOCK_SIZE> <<<NUM_BLOCKS, BLOCK_SIZE>>> ( A.num_rows, g.n(), g.Nx(), 
            cellX_ptr, cellY_ptr, 
            xn_ptr, yn_ptr, pitch, Aj, Av, forward_ptr);
    t.toc();
    std::cout << "   kernel took "<<t.diff()<<"s\n";
    return A;

}
/**
 * @brief Create an interpolation matrix on the device
 *
 * @param x Vector of x-values
 * @param y Vector of y-values
 * @param z Vector of z-values
 * @param g 3D Grid on which to interpolate z direction is assumed periodic
 *
 * @return interpolation matrix
 * @note n must be smaller than or equal to 4
 * @attention no range check is performed on the input vectors
 */
cusp::ell_matrix<int, double, cusp::device_memory> ell_interpolation( const thrust::device_vector<double>& x, const thrust::device_vector<double>& y, const thrust::device_vector<double>& z, const Grid3d<double>& g  )
{
    assert( x.size() == y.size());
    assert( x.size() == z.size());
    //allocate ell matrix storage
    cusp::ell_matrix<int, double, cusp::device_memory> A( x.size(), g.size(), x.size()*2*g.n()*g.n(), 2*g.n()*g.n());

    //set up kernel parameters
    const size_t BLOCK_SIZE = 256;
    const size_t MAX_BLOCKS = cusp::system::cuda::detail::max_active_blocks( detail::interpolation_kernel2d<BLOCK_SIZE>, BLOCK_SIZE, (size_t) 0  );
    const size_t NUM_BLOCKS = std::min<size_t>( 
            MAX_BLOCKS, 
            cusp::system::cuda::DIVIDE_INTO( A.num_rows, BLOCK_SIZE));
    const int pitch = A.column_indices.pitch; //# of cols in memory
    assert( pitch == A.values.pitch);
    int* Aj = thrust::raw_pointer_cast(&A.column_indices(0,0));
    double * Av = thrust::raw_pointer_cast(&A.values(0,0));

    //compute normalized x and y values and cell numbers
    thrust::device_vector<double> xn(x.size()), yn(y.size()), zn(z.size()), cellxh(x.size()), cellyh(x.size());
    dg::blas1::transform( x, xn, dg::PLUS<double>( -g.x0()));
    dg::blas1::transform( y, yn, dg::PLUS<double>( -g.y0()));
    dg::blas1::scal( xn, 1./g.hx());
    dg::blas1::scal( yn, 1./g.hy());
    thrust::device_vector<int> cellX( x.size());
    thrust::device_vector<int> cellY( y.size());
    thrust::transform( xn.begin(), xn.end(), cellX.begin(), dg::FLOOR());
    thrust::transform( yn.begin(), yn.end(), cellY.begin(), dg::FLOOR());
    thrust::transform( cellX.begin(), cellX.end(),cellxh.begin(), dg::PLUS<double>(0.5));
    thrust::transform( cellY.begin(), cellY.end(),cellyh.begin(), dg::PLUS<double>(0.5));
    const int* cellX_ptr = thrust::raw_pointer_cast( &cellX[0]);
    const int* cellY_ptr = thrust::raw_pointer_cast( &cellY[0]);
    const double* xn_ptr = thrust::raw_pointer_cast( &xn[0]);
    const double* yn_ptr = thrust::raw_pointer_cast( &yn[0]);
    //xn = 2*xn - 2*(cellx+0.5)
    dg::blas1::axpby( 2., xn, -2., cellxh, xn);
    dg::blas1::axpby( 2., yn, -2., cellyh, yn);
    thrust::device_vector<double> forward(std::vector<double> ( g.dlt().forward()));
    const double * forward_ptr = thrust::raw_pointer_cast( &forward[0]);
    //detail::interpolation_kernel2dcpu( A.num_rows, g.n(), g.Nx(), 
    detail::interpolation_kernel2d<BLOCK_SIZE> <<<NUM_BLOCKS, BLOCK_SIZE>>> ( A.num_rows, g.n(), g.Nx(), 
            cellX_ptr, cellY_ptr, 
            xn_ptr, yn_ptr, pitch, Aj, Av, forward_ptr);
    return A;

}
} //namespace create
} //namespace dg
