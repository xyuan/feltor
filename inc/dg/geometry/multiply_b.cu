
#include <iostream>
#include <cmath>

#include "tensor.h"
#include "weights.h"
#include "multiply.h"
#include "dg/backend/timer.h"

typedef thrust::device_vector<double> Vector;

int main()
{
    dg::Timer t;
    unsigned n, Nx, Ny, Nz;
    std::cout << "Type n, Nx, Ny and Nz\n";
    std::cin >> n >> Nx >> Ny >> Nz;
    dg::Grid3d grid( 0., 2.*M_PI, 0, 2.*M_PI, 0, 2.*M_PI, n, Nx, Ny, Nz);
    Vector w2d;
    dg::blas1::transfer( dg::create::weights(grid), w2d);
    dg::SparseTensor<Vector> g(3);
    g.idx(0,0) = 0, g.idx(0,1) = g.idx(1,0) = 1, g.idx(1,1) = 2;
    g.values()[0] = g.values()[1] = g.values()[2] = w2d;
    Vector v_x = dg::evaluate( dg::CONSTANT(2), grid), w_x(v_x), temp(v_x);
    Vector v_y = dg::evaluate( dg::CONSTANT(5), grid), w_y(v_y);
    double gbytes=(double)v_x.size()*sizeof(double)/1e9;
    int multi=20;
    t.tic();
    for( int i=0; i<multi; i++)
        dg::tensor::multiply2d( g, v_x, v_y, temp, v_y);
    t.toc();
    std::cout<<"multiply_inplace(g,v_x,v_y,temp) took   "<<t.diff()/multi<<"s\t"<<gbytes*multi/t.diff()<<"GB/s\n";
    t.tic();
    for( int i=0; i<multi; i++)
        dg::tensor::multiply2d( g, v_x, v_y, w_x, v_y);
    t.toc();
    std::cout<<"multiply2d(g,v_x,v_y,w_x,w_y) took      "<<t.diff()/multi<<"s\t"<<gbytes*multi/t.diff()<<"GB/s\n";
    return 0;
}
