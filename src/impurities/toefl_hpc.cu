#include <iostream>
#include <iomanip>
#include <vector>

#include "toeflI.cuh"
#include "parameters.h"

#include "file/nc_utilities.h"

/*
   - reads parameters from input.txt or any other given file,
   - integrates the ToeflR - functor and
   - writes outputs to a given outputfile using netcdf4.
*/

int main( int argc, char* argv[])
{
    ////////////////////////Parameter initialisation//////////////////////////
    Json::Value js;
    Json::CharReaderBuilder parser;
    parser["collectComments"] = false;
    std::string errs;
    if( argc != 3)
    {
        std::cerr << "ERROR: Wrong number of arguments!\nUsage: "<< argv[0]<<" [inputfile] [outputfile]\n";
        return -1;
    }
    else
    {
        std::ifstream is(argv[1]);
        parseFromStream( parser, is, &js, &errs); //read input without comments
    }
    std::cout << js<<std::endl;
    std::string input = js.toStyledString(); //save input without comments, which is important if netcdf file is later read by another parser
    const imp::Parameters p( js);
    p.display( std::cout);
    ////////////////////////////////set up computations///////////////////////////
    dg::CartesianGrid2d grid( 0, p.lx, 0, p.ly, p.n, p.Nx, p.Ny, p.bc_x, p.bc_y);
    dg::CartesianGrid2d grid_out( 0, p.lx, 0, p.ly, p.n_out, p.Nx_out, p.Ny_out, p.bc_x, p.bc_y);
    //create RHS
    dg::Diffusion< dg::CartesianGrid2d, dg::DMatrix, dg::DVec > diffusion( grid, p);
    dg::ToeflI< dg::CartesianGrid2d, dg::DMatrix, dg::DVec > toeflI( grid, p);
    /////////////////////The initial field///////////////////////////////////////////
    dg::Gaussian gaussian( p.posX*p.lx, p.posY*p.ly, p.sigma, p.sigma, p.amp); //gaussian width is in absolute values
    std::vector<dg::DVec> y0(3, dg::evaluate(dg::zero, grid));
    dg::Helmholtz<dg::CartesianGrid2d, dg::DMatrix, dg::DVec> & gamma = toeflI.gamma();
    if( p.mode == 1)
    {   if( p.vorticity == 0)
        {   gamma.alpha() = -0.5*p.tau[1];
            y0[0] = dg::evaluate( gaussian, grid);
            dg::blas2::symv( gamma, y0[0], y0[1]); // n_e = \Gamma_i n_i -> n_i = ( 1+alphaDelta) n_e' + 1
            dg::DVec v2d=dg::create::inv_weights(grid);
            dg::blas2::symv( v2d, y0[1], y0[1]);
            dg::blas1::scal( y0[1], 1./p.a[1]); //n_i ~1./a_i n_e
            y0[2] = dg::evaluate( dg::zero, grid);
        }
        else
        {   y0[1] = y0[0] = dg::evaluate( gaussian, grid);
            dg::blas1::scal( y0[1], 1/p.a[1]);
            y0[2] = dg::evaluate( dg::zero, grid);
        }
    }
    if( p.mode == 2)
    {   //init wall in y0[2]
        dg::GaussianX wall( p.wall_pos*p.lx, p.wall_sigma, p.wall_amp);
        dg::DVec wallv = dg::evaluate( wall, grid);
        gamma.alpha() = -0.5*p.tau[2]*p.mu[2];
        dg::blas2::symv( gamma, wallv, y0[2]);
        dg::DVec v2d=dg::create::inv_weights(grid);
        dg::blas2::symv( v2d, y0[2], y0[2]);
        if( p.a[2] != 0.)
            dg::blas1::scal( y0[2], 1./p.a[2]); //n_z ~1./a_z

        //init blob in y0[1]
        gamma.alpha() = -0.5*p.tau[1];
        y0[0] = dg::evaluate( gaussian, grid);
        dg::blas2::symv( gamma, y0[0], y0[1]);
        dg::blas1::pointwiseDot( v2d, y0[1], y0[1]);
        if( p.a[2] == 1)
        {   std::cerr << "No blob with trace ions possible!\n";
            return -1;
        }
        dg::blas1::scal( y0[1], 1./p.a[1]); //n_i ~1./a_i n_e

        //sum up
        if( p.a[2] != 0)
            dg::blas1::axpby( 1., wallv, 1., y0[0]); //add wall to blob in n_e
    }
    if( p.mode == 3)
    {   gamma.alpha() = -0.5*p.tau[2]*p.mu[2];
        y0[0] = dg::evaluate( gaussian, grid);
        dg::blas2::symv( gamma, y0[0], y0[2]);
        dg::DVec v2d=dg::create::inv_weights(grid);
        dg::blas2::symv( v2d, y0[2], y0[2]);
        if( p.a[2] == 0)
        {   std::cerr << "No impurity blob with trace impurities possible!\n";
            return -1;
        }
        dg::blas1::axpby( 1./p.a[2], y0[2], 0., y0[2]); //n_z ~1./a_z n_e
        y0[1] = dg::evaluate( dg::zero, grid);
    }
    std::vector<dg::DVec> y1( y0);
    //////////////////initialisation of timestepper and first step///////////////////
    std::cout << "init timestepper...\n";
    double time = 0.0;
    dg::Karniadakis< std::vector<dg::DVec> > karniadakis( y0, y0[0].size(), p.eps_time);
    karniadakis.init( toeflI, diffusion, time, y0, p.dt);
    /////////////////////////////set up netcdf/////////////////////////////////////
    file::NC_Error_Handle err;
    int ncid;
    err = nc_create( argv[2],NC_NETCDF4|NC_CLOBBER, &ncid);
    err = nc_put_att_text( ncid, NC_GLOBAL, "inputfile", input.size(), input.data());
    //const int version[3] = {FELTOR_MAJOR_VERSION, FELTOR_MINOR_VERSION, FELTOR_SUBMINOR_VERSION}; REMOVED (MW)
    //err = nc_put_att_int( ncid, NC_GLOBAL, "feltor_major_version",    NC_INT, 1, &version[0]);
    //err = nc_put_att_int( ncid, NC_GLOBAL, "feltor_minor_version",    NC_INT, 1, &version[1]);
    //err = nc_put_att_int( ncid, NC_GLOBAL, "feltor_subminor_version", NC_INT, 1, &version[2]);
    int dim_ids[3], tvarID;
    err = file::define_dimensions( ncid, dim_ids, &tvarID, grid_out);
    //field IDs
    std::string names[5] = {"electrons", "ions", "impurities", "potential", "vorticity"};
    int dataIDs[5];
    for( unsigned i=0; i<5; i++)
        err = nc_def_var( ncid, names[i].data(), NC_DOUBLE, 3, dim_ids, &dataIDs[i]);
    //energy IDs
    int EtimeID, EtimevarID;
    err = file::define_time( ncid, "energy_time", &EtimeID, &EtimevarID);
    int energyID, massID, dissID, dEdtID, accuracyID;
    err = nc_def_var( ncid, "energy",      NC_DOUBLE, 1, &EtimeID, &energyID);
    err = nc_def_var( ncid, "mass",        NC_DOUBLE, 1, &EtimeID, &massID);
    err = nc_def_var( ncid, "dissipation", NC_DOUBLE, 1, &EtimeID, &dissID);
    err = nc_def_var( ncid, "dEdt",        NC_DOUBLE, 1, &EtimeID, &dEdtID);
    err = nc_def_var( ncid, "accuracy",    NC_DOUBLE, 1, &EtimeID, &accuracyID);
    err = nc_enddef(ncid);
    ///////////////////////////////////first output/////////////////////////
    std::cout << "First output ... \n";
    size_t start[3] = {0, 0, 0};
    size_t count[3] = {1, grid_out.n()*grid_out.Ny(), grid_out.n()*grid_out.Nx()};
    dg::DVec transfer(  dg::evaluate(dg::zero, grid));
    dg::DVec transferD( dg::evaluate(dg::zero, grid_out));
    dg::HVec transferH( dg::evaluate(dg::zero, grid_out));
    dg::IDMatrix interpolate = dg::create::interpolation( grid_out, grid);
    for( unsigned i=0; i<3; i++)
    {   dg::blas2::gemv( interpolate, y0[i], transferD);
        dg::blas1::transfer( transferD, transferH);
        err = nc_put_vara_double( ncid, dataIDs[i], start, count, transferH.data() );
    }
    //Potential
    transfer = toeflI.polarization( y0);
    dg::blas2::symv( interpolate, transfer, transferD);
    dg::blas1::transfer( transferD, transferH);
    err = nc_put_vara_double( ncid, dataIDs[3], start, count, transferH.data() );
    //Vorticity
    dg::blas2::gemv( diffusion.laplacianM(), transfer, y0[1]);
    dg::blas2::symv( interpolate, y0[1], transferD);
    dg::blas1::transfer( transferD, transferH);
    err = nc_put_vara_double( ncid, dataIDs[4], start, count, transferH.data() );
    err = nc_put_vara_double( ncid, tvarID, start, count, &time);
    err = nc_put_vara_double( ncid, EtimevarID, start, count, &time);
    size_t Estart[] = {0};
    size_t Ecount[] = {1};
    double energy0 = toeflI.energy(), mass0 = toeflI.mass(), E0 = energy0, mass = mass0, E1 = 0.0, dEdt = 0., diss = 0., accuracy=0.;
    err = nc_put_vara_double( ncid, energyID, Estart, Ecount, &energy0);
    err = nc_put_vara_double( ncid, massID,   Estart, Ecount, &mass0);
    err = nc_put_vara_double( ncid, dissID,     Estart, Ecount,&diss);
    err = nc_put_vara_double( ncid, dEdtID,     Estart, Ecount,&dEdt);
    err = nc_put_vara_double( ncid, accuracyID, Estart, Ecount,&accuracy);
    err = nc_close(ncid);
    std::cout << "First write successful!\n";
    ///////////////////////////////////////Timeloop/////////////////////////////////
    dg::Timer t;
    t.tic();
    try
    {
        unsigned step = 0;
        for( unsigned i=1; i<=p.maxout; i++)
        {

#ifdef DG_BENCHMARK
            dg::Timer ti;
            ti.tic();
#endif//DG_BENCHMARK
            for( unsigned j=0; j<p.itstp; j++)
            {   karniadakis.step( toeflI, diffusion, time, y0);
                y0.swap( y1);
                step++;
                Estart[0] = step;
                E0 = E1;
                E1 = toeflI.energy();
                mass = toeflI.mass();
                dEdt = (E1 - E0)/p.dt;
                diss = toeflI.energy_diffusion( );
                accuracy = 2.*fabs((dEdt-diss)/(dEdt + diss));

                //store accuracy details
                err = nc_open(argv[2], NC_WRITE, &ncid);
                err = nc_put_vara_double( ncid, EtimevarID, Estart, Ecount, &time);
                err = nc_put_vara_double( ncid, energyID,   Estart, Ecount, &E1);
                err = nc_put_vara_double( ncid, massID,     Estart, Ecount, &mass);
                err = nc_put_vara_double( ncid, dissID,     Estart, Ecount, &diss);
                err = nc_put_vara_double( ncid, dEdtID,     Estart, Ecount, &dEdt);
                err = nc_put_vara_double( ncid, accuracyID, Estart, Ecount, &accuracy);
                err = nc_close(ncid);
                std::cout << "(m_tot-m_0)/m_0: "<< (mass-mass0)/mass0<<"\t";
                std::cout << "(E_tot-E_0)/E_0: "<< (E1-energy0)/energy0<<"\t";
                std::cout <<" d E/dt = " << dEdt <<" Lambda = " << diss << " -> Accuracy: "<< accuracy << "\n";
            }
            //output all three fields
            //////////////////////////write fields////////////////////////
            start[0] = i;
            err = nc_open(argv[2], NC_WRITE, &ncid);
            for( unsigned j=0; j<3; j++)
            {   dg::blas2::symv( interpolate, y0[j], transferD);
                dg::blas1::transfer( transferD, transferH);
                err = nc_put_vara_double( ncid, dataIDs[j], start, count, transferH.data());
            }
            transfer = toeflI.potential()[0];
            dg::blas2::symv( interpolate, transfer, transferD);
            dg::blas1::transfer( transferD, transferH);
            err = nc_put_vara_double( ncid, dataIDs[3], start, count, transferH.data() );
            transfer = toeflI.potential()[0];
            dg::blas2::gemv( diffusion.laplacianM(), transfer, y1[1]);        //correct?
            dg::blas2::symv( interpolate, y1[1], transferD);
            dg::blas1::transfer( transferD, transferH);
            err = nc_put_vara_double( ncid, dataIDs[4], start, count, transferH.data() );
            err = nc_put_vara_double( ncid, tvarID, start, count, &time);
            err = nc_close(ncid);
#ifdef DG_BENCHMARK
            ti.toc();
            std::cout << "\n\t Step "<<step <<" of "<<p.itstp*p.maxout <<" at time "<<time;
            std::cout << "\n\t Average time for one step: "<<ti.diff()/(double)p.itstp<<"s\n\n"<<std::flush;
#endif//DG_BENCHMARK
        }
    }
    catch( dg::Fail& fail)
    {   std::cerr << "CG failed to converge to "<<fail.epsilon()<<"\n";
        std::cerr << "Does Simulation respect CFL condition?\n";
    }
    t.toc();
    unsigned hour = (unsigned)floor(t.diff()/3600);
    unsigned minute = (unsigned)floor( (t.diff() - hour*3600)/60);
    double second = t.diff() - hour*3600 - minute*60;
    std::cout << std::fixed << std::setprecision(2) <<std::setfill('0');
    std::cout <<"Computation Time \t"<<hour<<":"<<std::setw(2)<<minute<<":"<<second<<"\n";
    std::cout <<"which is         \t"<<t.diff()/p.itstp/p.maxout<<"s/step\n";

    return 0;

}

