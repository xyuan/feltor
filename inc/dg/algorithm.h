#pragma once

/*! @file
 * Includes all container independent headers of the dg library.
 *
 * @note include <mpi.h> before this header to activate mpi support
 */
#include "backend/timer.h"
#include "backend/transpose.h"
#include "geometry/split_and_join.h"
#include "geometry/xspacelib.h"
#include "geometry/evaluationX.h"
#include "geometry/derivativesX.h"
#include "geometry/weightsX.h"
#include "geometry/interpolationX.h"
#include "geometry/projectionX.h"
#include "geometry/geometry.h"
#include "blas.h"
#include "helmholtz.h"
#include "cg.h"
#include "functors.h"
#include "multistep.h"
#include "elliptic.h"
#include "runge_kutta.h"
#include "multigrid.h"
#include "refined_elliptic.h"
#include "arakawa.h"
#include "poisson.h"
#include "geometry/average.h"
#ifdef MPI_VERSION
#include "geometry/average_mpi.h"
#include "backend/mpi_init.h"
#endif
