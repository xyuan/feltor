#include <iostream>
#include "evaluation.h"
#include "average.h"
#include "dg/blas.h"


const double lx = M_PI/2.;
const double ly = M_PI;
double function( double x, double y) {return cos(x)*sin(y);}
double pol_average( double x, double y) {return cos(x)*2./M_PI;}
double tor_average( double x, double y) {return sin(y)*2./M_PI;}

int main()
{
    std::cout << "Program to test the average in x and y direction\n";
    unsigned n = 3, Nx = 32, Ny = 48;
    //![doxygen]
    const dg::Grid2d g( 0, lx, 0, ly, n, Nx, Ny);

    dg::Average< dg::DVec > pol(g, dg::coo2d::y);

    const dg::DVec vector = dg::evaluate( function ,g);
    dg::DVec average_y( vector);
    std::cout << "Averaging y ... \n";
    pol( vector, average_y);
    //![doxygen]
    const dg::DVec w2d = dg::create::weights( g);
    dg::DVec solution = dg::evaluate( pol_average, g);
    dg::blas1::axpby( 1., solution, -1., average_y);
    int64_t binary[] = {4406193765905047925,4395311848786989976};
    exblas::udouble res;
    res.d = sqrt( dg::blas2::dot( average_y, w2d, average_y));
    std::cout << "Distance to solution is: "<<res.d<<"\t"<<res.i-binary[0]<<std::endl;
    std::cout << "Averaging x ... \n";
    dg::Average< dg::DVec> tor( g, dg::coo2d::x);
    tor( vector, average_y);
    solution = dg::evaluate( tor_average, g);
    dg::blas1::axpby( 1., solution, -1., average_y);
    res.d = sqrt( dg::blas2::dot( average_y, w2d, average_y));
    std::cout << "Distance to solution is: "<<res.d<<"\t"<<res.i-binary[1]<<std::endl;
    //std::cout << "\n Continue with \n\n";

    return 0;
}
