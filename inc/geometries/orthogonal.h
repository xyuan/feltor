#pragma once

#include "dg/backend/grid.h"
#include "dg/backend/functions.h"
#include "dg/backend/interpolation.cuh"
#include "dg/backend/operator.h"
#include "dg/backend/derivatives.h"
#include "dg/functors.h"
#include "dg/runge_kutta.h"
#include "dg/nullstelle.h"
#include "dg/geometry.h"
#include "fields.h"



namespace orthogonal
{

namespace detail
{

//This leightweights struct and its methods finds the initial R and Z values and the coresponding f(\psi) as 
//good as it can, i.e. until machine precision is reached
template< class Psi, class PsiX, class PsiY>
struct Fpsi
{
    
    Fpsi( Psi psi, PsiX psiX, PsiY psiY, double x0, double y0): 
        psip_(psi), fieldRZYT_(psiX, psiY, x0, y0), fieldRZtau_(psiX, psiY)
    {
        X_init = x0; Y_init = y0;
        while( fabs( psiX(X_init, Y_init)) <= 1e-10 && fabs( psiY( X_init, Y_init)) <= 1e-10)
            X_init = x0 + 1.; Y_init = y0;
    }
    //finds the starting points for the integration in y direction
    void find_initial( double psi, double& R_0, double& Z_0) 
    {
        unsigned N = 50;
        thrust::host_vector<double> begin2d( 2, 0), end2d( begin2d), end2d_old(begin2d); 
        begin2d[0] = end2d[0] = end2d_old[0] = X_init;
        begin2d[1] = end2d[1] = end2d_old[1] = Y_init;
        //std::cout << "In init function\n";
        double eps = 1e10, eps_old = 2e10;
        while( (eps < eps_old || eps > 1e-7) && eps > 1e-14)
        {
            //remember old values
            eps_old = eps; end2d_old = end2d;
            //compute new values
            N*=2; dg::stepperRK17( fieldRZtau_, begin2d, end2d, psip_(X_init, Y_init), psi, N);
            eps = sqrt( (end2d[0]-end2d_old[0])*(end2d[0]-end2d_old[0]) + (end2d[1]-end2d_old[1])*(end2d[1]-end2d_old[1]));
        }
        X_init = R_0 = end2d_old[0], Y_init = Z_0 = end2d_old[1];
    }

    //compute f for a given psi between psi0 and psi1
    double construct_f( double psi, double& R_0, double& Z_0) 
    {
        find_initial( psi, R_0, Z_0);
        //std::cout << "Begin error "<<eps_old<<" with "<<N<<" steps\n";
        //std::cout << "In Stepper function:\n";
        //double y_old=0;
        thrust::host_vector<double> begin( 3, 0), end(begin), end_old(begin);
        begin[0] = R_0, begin[1] = Z_0;
        //std::cout << begin[0]<<" "<<begin[1]<<" "<<begin[2]<<"\n";
        double eps = 1e10, eps_old = 2e10;
        unsigned N = 50;
        //double y_eps = 1;
        while( (eps < eps_old || eps > 1e-7)&& eps > 1e-14)
        {
            //remember old values
            eps_old = eps, end_old = end;
            //compute new values
            N*=2;
            dg::stepperRK17( fieldRZYT_, begin, end, 0., 2*M_PI, N);
            eps = sqrt( (end[0]-begin[0])*(end[0]-begin[0]) + (end[1]-begin[1])*(end[1]-begin[1]));
            //y_eps = sqrt( (end_old[2] - end[2])*(end_old[2]-end[2]))/sqrt(end[2]*end[2]);
            //std::cout << "\t error "<<eps<<" with "<<N<<" steps\t";
            //std::cout <<end_old[2] << " "<<end[2] << "error in y is "<<y_eps<<"\n";
        }
        double f_psi = 2.*M_PI/end_old[2];
        return f_psi;
    }
    double operator()( double psi)
    {
        double R_0, Z_0; 
        return construct_f( psi, R_0, Z_0);
    }

    private:
    double X_init, Y_init;
    Psi psip_;
    solovev::orthogonal::FieldRZYT<PsiX, PsiY> fieldRZYT_;
    solovev::FieldRZtau<PsiX, PsiY> fieldRZtau_;

};

//compute the vector of r and z - values that form one psi surface
template <class PsiX, class PsiY>
void compute_rzy( PsiX psiX, PsiY psiY, double psi, const thrust::host_vector<double>& y_vec,
        thrust::host_vector<double>& r, 
        thrust::host_vector<double>& z, 
        double R_0, double Z_0, double f_psi ) 
{
    solovev::orthogonal::FieldRZY<PsiX, PsiY> fieldRZY(psiX, psiY);
    thrust::host_vector<double> r_old(y_vec.size(), 0), r_diff( r_old);
    thrust::host_vector<double> z_old(y_vec.size(), 0), z_diff( z_old);
    r.resize( y_vec.size()), z.resize(y_vec.size());
    thrust::host_vector<double> begin( 2, 0), end(begin), temp(begin);
    begin[0] = R_0, begin[1] = Z_0;
    //std::cout <<f_psi<<" "<<" "<< begin[0] << " "<<begin[1]<<"\t";
    fieldRZY.set_f(f_psi);
    unsigned steps = 1;
    double eps = 1e10, eps_old=2e10;
    while( (eps < eps_old||eps > 1e-7) && eps > 1e-14)
    {
        //begin is left const
        eps_old = eps, r_old = r, z_old = z;
        dg::stepperRK17( fieldRZY, begin, end, 0, y_vec[0], steps);
        r[0] = end[0], z[0] = end[1];
        for( unsigned i=1; i<y_vec.size(); i++)
        {
            temp = end;
            dg::stepperRK17( fieldRZY, temp, end, y_vec[i-1], y_vec[i], steps);
            r[i] = end[0], z[i] = end[1];
        }
        temp = end;
        dg::stepperRK17( fieldRZY, temp, end, y_vec[y_vec.size()-1], 2.*M_PI, steps);
        //compute error in R,Z only
        dg::blas1::axpby( 1., r, -1., r_old, r_diff);
        dg::blas1::axpby( 1., z, -1., z_old, z_diff);
        double er = dg::blas1::dot( r_diff, r_diff);
        double ez = dg::blas1::dot( z_diff, z_diff);
        double ar = dg::blas1::dot( r, r);
        double az = dg::blas1::dot( z, z);
        eps =  sqrt( er + ez)/sqrt(ar+az);
        //std::cout << "rel. error is "<<eps<<" with "<<steps<<" steps\n";
        //std::cout << "abs. error is "<<sqrt( (end[0]-begin[0])*(end[0]-begin[0]) + (end[1]-begin[1])*(end[1]-begin[1]))<<"\n";
        steps*=2;
    }
    r = r_old, z = z_old;

}

//This struct computes -2pi/f with a fixed number of steps for all psi
//and provides the Nemov algorithm for orthogonal grid

template< class PsiX, class PsiY, class PsiXX, class PsiXY, class PsiYY, class LaplacePsiX, class LaplacePsiY>
struct Nemov
{
    Nemov( PsiX psiX, PsiY psiY, PsiXX psiXX, PsiXY psiXY, PsiYY psiYY, LaplacePsiX laplacePsiX, LaplacePsiY laplacePsiY, double f0):
        f0_(f0),
        psipR_(psiX), psipZ_(psiY),
        psipRR_(psiXX), psipZZ_(psiYY), psipRZ_(psiXY), 
        laplacePsipR_( laplacePsiX), laplacePsipZ_(laplacePsiY)
            { }
    void initialize( 
        const thrust::host_vector<double>& r_init, //1d intial values
        const thrust::host_vector<double>& z_init, //1d intial values
        thrust::host_vector<double>& h_init,
        thrust::host_vector<double>& hr_init,
        thrust::host_vector<double>& hz_init)
    {
        unsigned size = r_init.size(); 
        h_init.resize( size), hr_init.resize( size), hz_init.resize( size);
        for( unsigned i=0; i<size; i++)
        {
            double psipR = psipR_(r_init[i], z_init[i]), 
                   psipZ = psipZ_(r_init[i], z_init[i]);
            double laplace = psipRR_(r_init[i], z_init[i]) + 
                             psipZZ_(r_init[i], z_init[i]);
            double psip2 = (psipR*psipR+psipZ*psipZ);
            h_init[i]  = f0_;
            hr_init[i] = -f0_*laplace/psip2*psipR;
            hz_init[i] = -f0_*laplace/psip2*psipZ;
        }
    }

    void operator()(const std::vector<thrust::host_vector<double> >& y, std::vector<thrust::host_vector<double> >& yp) 
    { 
        //y[0] = R, y[1] = Z, y[2] = h, y[3] = hr, y[4] = hz
        unsigned size = y[0].size();
        double psipR, psipZ, psipRR, psipRZ, psipZZ, psip2;
        for( unsigned i=0; i<size; i++)
        {
            psipR = psipR_(y[0][i], y[1][i]), psipZ = psipZ_(y[0][i], y[1][i]);
            psipRR = psipRR_(y[0][i], y[1][i]), psipRZ = psipRZ_(y[0][i], y[1][i]), psipZZ = psipZZ_(y[0][i], y[1][i]);
            psip2 = f0_*(psipR*psipR+psipZ*psipZ);
            yp[0][i] = psipR/psip2;
            yp[1][i] = psipZ/psip2;
            yp[2][i] = y[2][i]*( -(psipRR+psipZZ) )/psip2;
            yp[3][i] = ( -(2.*psipRR+psipZZ)*y[3][i] - psipRZ*y[4][i] - laplacePsipR_(y[0][i], y[1][i])*y[2][i])/psip2;
            yp[4][i] = ( -psipRZ*y[3][i] - (2.*psipZZ+psipRR)*y[4][i] - laplacePsipZ_(y[0][i], y[1][i])*y[2][i])/psip2;
        }
    }
    private:
    double f0_;
    PsiX psipR_;
    PsiY psipZ_;
    PsiXX psipRR_;
    PsiYY psipZZ_;
    PsiXY psipRZ_;
    LaplacePsiX laplacePsipR_;
    LaplacePsiY laplacePsipZ_;
};

template<class Nemov>
void construct_rz( Nemov nemov, 
        double f0, 
        const thrust::host_vector<double>& x_vec,  //1d x values
        const thrust::host_vector<double>& r_init, //1d intial values
        const thrust::host_vector<double>& z_init, //1d intial values
        thrust::host_vector<double>& r, 
        thrust::host_vector<double>& z, 
        thrust::host_vector<double>& h,
        thrust::host_vector<double>& hr,
        thrust::host_vector<double>& hz
    )
{
    unsigned N = 1;
    double eps = 1e10, eps_old=2e10;
    std::vector<thrust::host_vector<double> > begin(5);
    thrust::host_vector<double> h_init, hr_init, hz_init;
    nemov.initialize( r_init, z_init, h_init, hr_init, hz_init);
    begin[0] = r_init, begin[1] = z_init, 
    begin[2] = h_init; begin[3] = hr_init, begin[4] = hz_init;
    //now we have the starting values 
    std::vector<thrust::host_vector<double> > end(begin), temp(begin);
    unsigned sizeX = x_vec.size(), sizeY = r_init.size();
    unsigned size2d = x_vec.size()*r_init.size();
    r.resize(size2d), z.resize(size2d), h.resize(size2d), hr.resize(size2d), hz.resize(size2d);
    //std::cout << "In psi function:\n";
    double x0=0, x1 = x_vec[0];
    thrust::host_vector<double> r_old(r), r_diff( r), z_old(z), z_diff(z);
    while( (eps < eps_old || eps > 1e-6) && eps > 1e-13)
    {
        r_old = r, z_old = z; eps_old = eps; 
        temp = begin;
        //////////////////////////////////////////////////
        for( unsigned i=0; i<sizeX; i++)
        {
            x0 = i==0?0:x_vec[i-1], x1 = x_vec[i];
            //////////////////////////////////////////////////
            dg::stepperRK17( nemov, temp, end, x0, x1, N);
            for( unsigned j=0; j<sizeY; j++)
            {
                unsigned idx = j*sizeX+i;
                 r[idx] = end[0][j],  z[idx] = end[1][j];
                hr[idx] = end[3][j], hz[idx] = end[4][j];
                 h[idx] = end[2][j]; 
            }
            //////////////////////////////////////////////////
            temp = end;
        }
        dg::blas1::axpby( 1., r, -1., r_old, r_diff);
        dg::blas1::axpby( 1., z, -1., z_old, z_diff);
        dg::blas1::pointwiseDot( r_diff, r_diff, r_diff);
        dg::blas1::pointwiseDot( 1., z_diff, z_diff, 1., r_diff);
        eps = sqrt( dg::blas1::dot( r_diff, r_diff)/sizeX/sizeY); //should be relative to the interpoint distances
        //std::cout << "Effective Absolute diff error is "<<eps<<" with "<<N<<" steps\n"; 
        N*=2;
    }

}

} //namespace detail

template< class container>
struct RingGrid2d; 

/**
 * @brief A three-dimensional grid based on orthogonal coordinates
 */
template< class container>
struct RingGrid3d : public dg::Grid3d<double>
{
    typedef dg::OrthogonalCylindricalTag metric_category;
    typedef RingGrid2d<container> perpendicular_grid;
    /**
     * @brief Construct 
     *
     * @param gp The geometric parameters define the magnetic field
     * @param psi_0 lower boundary for psi
     * @param psi_1 upper boundary for psi
     * @param n The dG number of polynomials
     * @param Nx The number of points in x-direction
     * @param Ny The number of points in y-direction
     * @param Nz The number of points in z-direction
     * @param bcx The boundary condition in x (y,z are periodic)
     */
    RingGrid3d( solovev::GeomParameters gp, double psi_0, double psi_1, unsigned n, unsigned Nx, unsigned Ny, unsigned Nz, dg::bc bcx): 
        dg::Grid3d<double>( 0, 1, 0., 2.*M_PI, 0., 2.*M_PI, n, Nx, Ny, Nz, bcx, dg::PER, dg::PER)
    { 
        solovev::Psip psip(gp); 
        solovev::PsipR psipR(gp); solovev::PsipZ psipZ(gp);
        solovev::PsipRR psipRR(gp); solovev::PsipZZ psipZZ(gp); solovev::PsipRZ psipRZ(gp);
        solovev::LaplacePsipR lapPsipR(gp); solovev::LaplacePsipZ lapPsipZ(gp); 
        construct( psip, psipR, psipZ, psipRR, psipRZ, psipZZ, lapPsipR, lapPsipZ, psi_0, psi_1, gp.R_0, 0, n, Nx, Ny);
    }
    template< class Psi, class PsiX, class PsiY, class PsiXX, class PsiXY, class PsiYY, class LaplacePsiX, class LaplacePsiY>
    RingGrid3d( Psi psi, PsiX psiX, PsiY psiY, PsiXX psiXX, PsiXY psiXY, PsiYY psiYY, LaplacePsiX laplacePsiX, LaplacePsiY laplacePsiY, 
            double psi_0, double psi_1, double x0, double y0, unsigned n, unsigned Nx, unsigned Ny, unsigned Nz, dg::bc bcx):
        dg::Grid3d<double>( 0, 1, 0., 2.*M_PI, 0., 2.*M_PI, n, Nx, Ny, Nz, bcx, dg::PER, dg::PER)
    { 
        construct( psi, psiX, psiY, psiXX, psiXY, psiYY, laplacePsiX, laplacePsiY, psi_0, psi_1, x0, y0, n, Nx, Ny);
    }

    template< class Psi, class PsiX, class PsiY, class PsiXX, class PsiXY, class PsiYY, class LaplacePsiX, class LaplacePsiY>
    void construct( Psi psi, PsiX psiX, PsiY psiY, 
            PsiXX psiXX, PsiXY psiXY, PsiYY psiYY, 
            LaplacePsiX laplacePsiX, LaplacePsiY laplacePsiY, 
            double psi_0, double psi_1, 
            double x0, double y0, unsigned n, unsigned Nx, unsigned Ny)
    {
        assert( psi_1 != psi_0);

        //compute innermost flux surface
        orthogonal::detail::Fpsi<Psi, PsiX, PsiY> fpsi(psi, psiX, psiY, x0, y0);
        dg::Grid1d<double> gY1d( 0, 2*M_PI, n, Ny, dg::PER);
        unsigned sizeY = gY1d.size();
        thrust::host_vector<double> y_vec = dg::evaluate( dg::coo1, gY1d);
        thrust::host_vector<double> r_init(sizeY), z_init(sizeY);
        double R0, Z0, f0;
        thrust::host_vector<double> begin( 2, 0), end(begin), temp(begin);
        f0 = fpsi.construct_f( psi_0, R0, Z0);
        if( psi_1 < psi_0) f0*=-1;
        detail::compute_rzy( psiX, psiY, psi_0, y_vec, r_init, z_init, R0, Z0, f0);

        //now construct grid in x
        double x_1 = fabs( f0*(psi_1-psi_0));
        init_X_boundaries( 0., x_1);

        dg::Grid1d<double> gX1d( this->x0(), this->x1(), n, Nx);
        thrust::host_vector<double> x_vec = dg::evaluate( dg::coo1, gX1d);
        detail::Nemov<PsiX, PsiY, PsiXX, PsiXY, PsiYY, LaplacePsiX, LaplacePsiY> 
            nemov(psiX, psiY, psiXX, psiXY, psiYY, laplacePsiX, laplacePsiY, f0);
        thrust::host_vector<double> h, hr, hz;
        detail::construct_rz(nemov, f0, x_vec, r_init, z_init, 
                r_, z_, h, hr, hz);
        r_.resize(size()), z_.resize(size());
        xr_.resize(size()), xz_.resize(size()), 
        yr_.resize(size()), yz_.resize(size());
        lapx_.resize(size()), lapy_.resize(size());
        for( unsigned idx=0; idx<r_.size(); idx++)
        {
            double psipR = psiX(r_[idx], z_[idx]);
            double psipZ = psiY(r_[idx], z_[idx]);
            xr_[idx] = f0*psipR;
            xz_[idx] = f0*psipZ;
            yr_[idx] = h[idx]*psipZ;
            yz_[idx] = -h[idx]*psipR;
            lapx_[idx] = f0*(psiXX( r_[idx], z_[idx]) + psiYY(r_[idx], z_[idx]));
            lapy_[idx] = -hr[idx]*psipZ + hz[idx]*psipR;
        }
        lift3d( ); //lift to 3D grid
        construct_metric();
    }

    perpendicular_grid perp_grid() const { return orthogonal::RingGrid2d<container>(*this);}
    const thrust::host_vector<double>& r()const{return r_;}
    const thrust::host_vector<double>& z()const{return z_;}
    const thrust::host_vector<double>& xr()const{return xr_;}
    const thrust::host_vector<double>& yr()const{return yr_;}
    const thrust::host_vector<double>& xz()const{return xz_;}
    const thrust::host_vector<double>& yz()const{return yz_;}
    const thrust::host_vector<double>& lapx()const{return lapx_;}
    const thrust::host_vector<double>& lapy()const{return lapy_;}
    const container& g_xx()const{return g_xx_;}
    const container& g_yy()const{return g_yy_;}
    const container& g_xy()const{return g_xy_;}
    const container& g_pp()const{return g_pp_;}
    const container& vol()const{return vol_;}
    const container& perpVol()const{return vol2d_;}
    private:
    void lift3d( )
    {
        //lift to 3D grid
        unsigned Nx = this->n()*this->Nx(), Ny = this->n()*this->Ny();
        for( unsigned k=1; k<this->Nz(); k++)
            for( unsigned i=0; i<Nx*Ny; i++)
            {
                r_[k*Nx*Ny+i] = r_[(k-1)*Nx*Ny+i];
                z_[k*Nx*Ny+i] = z_[(k-1)*Nx*Ny+i];
                xr_[k*Nx*Ny+i] = xr_[(k-1)*Nx*Ny+i];
                xz_[k*Nx*Ny+i] = xz_[(k-1)*Nx*Ny+i];
                yr_[k*Nx*Ny+i] = yr_[(k-1)*Nx*Ny+i];
                yz_[k*Nx*Ny+i] = yz_[(k-1)*Nx*Ny+i];
                lapx_[k*Nx*Ny+i] = lapx_[(k-1)*Nx*Ny+i];
                lapy_[k*Nx*Ny+i] = lapy_[(k-1)*Nx*Ny+i];
            }
    }
    //compute metric elements from xr, xz, yr, yz, r and z
    void construct_metric( )
    {
        thrust::host_vector<double> tempxx( r_), tempxy(r_), tempyy(r_), tempvol(r_);
        for( unsigned i = 0; i<this->size(); i++)
        {
            tempxx[i] = (xr_[i]*xr_[i]+xz_[i]*xz_[i]);
            tempxy[i] = (yr_[i]*xr_[i]+yz_[i]*xz_[i]);
            tempyy[i] = (yr_[i]*yr_[i]+yz_[i]*yz_[i]);
            tempvol[i] = r_[i]/sqrt( tempxx[i]*tempyy[i] );
        }
        g_xx_=tempxx, g_xy_=tempxy, g_yy_=tempyy, vol_=tempvol;
        dg::blas1::pointwiseDivide( tempvol, r_, tempvol);
        vol2d_ = tempvol;
        thrust::host_vector<double> ones = dg::evaluate( dg::one, *this);
        dg::blas1::pointwiseDivide( ones, r_, tempxx);
        dg::blas1::pointwiseDivide( tempxx, r_, tempxx); //1/R^2
        g_pp_=tempxx;
    }
    thrust::host_vector<double> r_, z_, xr_, xz_, yr_, yz_, lapx_, lapy_; 
    container g_xx_, g_xy_, g_yy_, g_pp_, vol_, vol2d_;
};

/**
 * @brief A three-dimensional grid based on orthogonal coordinates
 */
template< class container>
struct RingGrid2d : public dg::Grid2d<double>
{
    typedef dg::OrthogonalCylindricalTag metric_category;
    template< class Psi, class PsiX, class PsiY, class PsiXX, class PsiXY, class PsiYY, class LaplacePsiX, class LaplacePsiY>
    RingGrid2d( Psi psi, PsiX psiX, PsiY psiY, PsiXX psiXX, PsiXY psiXY, PsiYY psiYY, LaplacePsiX laplacePsiX, LaplacePsiY laplacePsiY, 
            double psi_0, double psi_1, double x0, double y0, unsigned n, unsigned Nx, unsigned Ny, dg::bc bcx):
        dg::Grid2d<double>( 0, 1, 0., 2.*M_PI, n, Nx, Ny, bcx, dg::PER)
    {
        orthogonal::RingGrid3d<container> g( psi, psiX, psiY, psiXX, psiXY, psiYY, laplacePsiX, laplacePsiY, psi_0, psi_1, x0, y0, n,Nx,Ny,1,bcx);
        init_X_boundaries( g.x0(), g.x1());
        r_=g.r(), z_=g.z(), xr_=g.xr(), xz_=g.xz(), yr_=g.yr(), yz_=g.yz(), lapx_=g.lapx(), lapy_=g.lapy();
        g_xx_=g.g_xx(), g_xy_=g.g_xy(), g_yy_=g.g_yy();
        vol2d_=g.perpVol();

    }
    RingGrid2d( const solovev::GeomParameters gp, double psi_0, double psi_1, unsigned n, unsigned Nx, unsigned Ny, dg::bc bcx): 
        dg::Grid2d<double>( 0, 1., 0., 2*M_PI, n,Nx,Ny, bcx, dg::PER)
    {
        orthogonal::RingGrid3d<container> g( gp, psi_0, psi_1, n,Nx,Ny,1,bcx);
        init_X_boundaries( g.x0(), g.x1());
        r_=g.r(), z_=g.z(), xr_=g.xr(), xz_=g.xz(), yr_=g.yr(), yz_=g.yz(), lapx_=g.lapx(), lapy_=g.lapy();
        g_xx_=g.g_xx(), g_xy_=g.g_xy(), g_yy_=g.g_yy();
        vol2d_=g.perpVol();
    }
    RingGrid2d( const RingGrid3d<container>& g):
        dg::Grid2d<double>( g.x0(), g.x1(), g.y0(), g.y1(), g.n(), g.Nx(), g.Ny(), g.bcx(), g.bcy())
    {
        unsigned s = this->size();
        r_.resize( s), z_.resize(s), xr_.resize(s), xz_.resize(s), yr_.resize(s), yz_.resize(s), lapx_.resize(s), lapy_.resize(s);
        g_xx_.resize( s), g_xy_.resize(s), g_yy_.resize(s), vol2d_.resize(s);
        for( unsigned i=0; i<s; i++)
        { r_[i]=g.r()[i], z_[i]=g.z()[i], xr_[i]=g.xr()[i], xz_[i]=g.xz()[i], yr_[i]=g.yr()[i], yz_[i]=g.yz()[i]; lapx_[i] = g.lapx()[i]; lapy_[i] = g.lapy()[i];}
        thrust::copy( g.g_xx().begin(), g.g_xx().begin()+s, g_xx_.begin());
        thrust::copy( g.g_xy().begin(), g.g_xy().begin()+s, g_xy_.begin());
        thrust::copy( g.g_yy().begin(), g.g_yy().begin()+s, g_yy_.begin());
        thrust::copy( g.perpVol().begin(), g.perpVol().begin()+s, vol2d_.begin());
    }

    const thrust::host_vector<double>& r()const{return r_;}
    const thrust::host_vector<double>& z()const{return z_;}
    const thrust::host_vector<double>& xr()const{return xr_;}
    const thrust::host_vector<double>& yr()const{return yr_;}
    const thrust::host_vector<double>& xz()const{return xz_;}
    const thrust::host_vector<double>& yz()const{return yz_;}
    const thrust::host_vector<double>& lapx()const{return lapx_;}
    const thrust::host_vector<double>& lapy()const{return lapy_;}
    const container& g_xx()const{return g_xx_;}
    const container& g_yy()const{return g_yy_;}
    const container& g_xy()const{return g_xy_;}
    const container& vol()const{return vol2d_;}
    const container& perpVol()const{return vol2d_;}
    private:
    thrust::host_vector<double> r_, z_, xr_, xz_, yr_, yz_, lapx_, lapy_; //2d vector
    container g_xx_, g_xy_, g_yy_, vol2d_;
};

/**
 * @brief Integrates the equations for a field line and 1/B
 */ 
struct Field
{
    Field( solovev::GeomParameters gp, const dg::Grid2d<double>& gXY, const thrust::host_vector<double>& f2):
        gp_(gp),
        psipR_(gp), psipZ_(gp),
        ipol_(gp), invB_(gp), gXY_(gXY), g_(dg::create::forward_transform(f2, gXY)) 
    { }

    /**
     * @brief \f[ \frac{d \hat{R} }{ d \varphi}  = \frac{\hat{R}}{\hat{I}} \frac{\partial\hat{\psi}_p}{\partial \hat{Z}}, \hspace {3 mm}
     \frac{d \hat{Z} }{ d \varphi}  =- \frac{\hat{R}}{\hat{I}} \frac{\partial \hat{\psi}_p}{\partial \hat{R}} , \hspace {3 mm}
     \frac{d \hat{l} }{ d \varphi}  =\frac{\hat{R}^2 \hat{B}}{\hat{I}  \hat{R}_0}  \f]
     */ 
    void operator()( const dg::HVec& y, dg::HVec& yp)
    {
        //x,y,s,R,Z
        double psipR = psipR_(y[3],y[4]), psipZ = psipZ_(y[3],y[4]), ipol = ipol_( y[3],y[4]);
        double xs = y[0],ys=y[1];
        gXY_.shift_topologic( y[0], M_PI, xs,ys);
        double g = dg::interpolate( xs,  ys, g_, gXY_);
        yp[0] = 0;
        yp[1] = y[3]*g*(psipR*psipR+psipZ*psipZ)/ipol;
        //yp[1] = g/ipol;
        yp[2] =  y[3]*y[3]/invB_(y[3],y[4])/ipol/gp_.R_0; //ds/dphi =  R^2 B/I/R_0_hat
        yp[3] =  y[3]*psipZ/ipol;              //dR/dphi =  R/I Psip_Z
        yp[4] = -y[3]*psipR/ipol;             //dZ/dphi = -R/I Psip_R

    }
    /**
     * @brief \f[   \frac{1}{\hat{B}} = 
      \frac{\hat{R}}{\hat{R}_0}\frac{1}{ \sqrt{ \hat{I}^2  + \left(\frac{\partial \hat{\psi}_p }{ \partial \hat{R}}\right)^2
      + \left(\frac{\partial \hat{\psi}_p }{ \partial \hat{Z}}\right)^2}}  \f]
     */ 
    double operator()( double R, double Z) const { return invB_(R,Z); }
    /**
     * @brief == operator()(R,Z)
     */ 
    double operator()( double R, double Z, double phi) const { return invB_(R,Z,phi); }
    double error( const dg::HVec& x0, const dg::HVec& x1)
    {
        //compute error in x,y,s
        return sqrt( (x0[0]-x1[0])*(x0[0]-x1[0]) +(x0[1]-x1[1])*(x0[1]-x1[1])+(x0[2]-x1[2])*(x0[2]-x1[2]));
    }
    bool monitor( const dg::HVec& end){ 
        if ( isnan(end[1]) || isnan(end[2]) || isnan(end[3])||isnan( end[4]) ) 
        {
            return false;
        }
        if( (end[3] < 1e-5) || end[3]*end[3] > 1e10 ||end[1]*end[1] > 1e10 ||end[2]*end[2] > 1e10 ||(end[4]*end[4] > 1e10) )
        {
            return false;
        }
        return true;
    }
    
    private:
    solovev::GeomParameters gp_;
    solovev::PsipR  psipR_;
    solovev::PsipZ  psipZ_;
    solovev::Ipol   ipol_;
    solovev::InvB   invB_;
    const dg::Grid2d<double> gXY_;
    thrust::host_vector<double> g_;
   
};

}//namespace orthogonal
namespace dg{
/**
 * @brief This function pulls back a function defined in cartesian coordinates R,Z to the orthogonal coordinates x,y,\phi
 *
 * i.e. F(x,y) = f(R(x,y), Z(x,y))
 * @tparam BinaryOp The function object 
 * @param f The function defined on R,Z
 * @param g The grid
 *
 * @return A set of points representing F(x,y)
 */
template< class BinaryOp, class container>
thrust::host_vector<double> pullback( BinaryOp f, const orthogonal::RingGrid2d<container>& g)
{
    thrust::host_vector<double> vec( g.size());
    for( unsigned i=0; i<g.size(); i++)
        vec[i] = f( g.r()[i], g.z()[i]);
    return vec;
}
///@cond
template<class container>
thrust::host_vector<double> pullback( double(f)(double,double), const orthogonal::RingGrid2d<container>& g)
{
    return pullback<double(double,double),container>( f, g);
}
///@endcond
/**
 * @brief This function pulls back a function defined in cylindrical coordinates R,Z,\phi to the orthogonal coordinates x,y,\phi
 *
 * i.e. F(x,y,\phi) = f(R(x,y), Z(x,y), \phi)
 * @tparam TernaryOp The function object 
 * @param f The function defined on R,Z,\phi
 * @param g The grid
 *
 * @return A set of points representing F(x,y,\phi)
 */
template< class TernaryOp, class container>
thrust::host_vector<double> pullback( TernaryOp f, const orthogonal::RingGrid3d<container>& g)
{
    thrust::host_vector<double> vec( g.size());
    unsigned size2d = g.n()*g.n()*g.Nx()*g.Ny();
    Grid1d<double> gz( g.z0(), g.z1(), 1, g.Nz());
    thrust::host_vector<double> absz = create::abscissas( gz);
    for( unsigned k=0; k<g.Nz(); k++)
        for( unsigned i=0; i<size2d; i++)
            vec[k*size2d+i] = f( g.r()[k*size2d+i], g.z()[k*size2d+i], absz[k]);
            //vec[k*size2d+i] = f( g.r()[i], g.z()[i], absz[k]);
    return vec;
}
///@cond
template<class container>
thrust::host_vector<double> pullback( double(f)(double,double,double), const orthogonal::RingGrid3d<container>& g)
{
    return pullback<double(double,double,double),container>( f, g);
}
///@endcond

}//namespace dg
