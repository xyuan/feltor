#pragma once

#include "mpi_vector.h"
#include "memory.h"

/*!@file

@brief MPI matrix classes

@note the corresponding blas file for the Local matrix must be included before this file
*/
namespace dg {
///@cond
namespace blas2{
namespace detail{
template< class MatrixType, class ContainerType1, class ContainerType2>
inline void doSymv( get_value_type<ContainerType1> alpha,
                  MatrixType&& M,
                  const ContainerType1& x,
                  get_value_type<ContainerType1> beta,
                  ContainerType2& y,
                  AnyMatrixTag);
template< class MatrixType, class ContainerType1, class ContainerType2>
inline void doSymv( MatrixType&& M,
                  const ContainerType1& x,
                  ContainerType2& y,
                  AnyMatrixTag);
template< class MatrixType, class ContainerType1, class ContainerType2>
inline void doSymv( get_value_type<ContainerType1> alpha,
                  MatrixType&& M,
                  const ContainerType1& x,
                  get_value_type<ContainerType1> beta,
                  ContainerType2& y,
                  SparseBlockMatrixTag);
template< class MatrixType, class ContainerType1, class ContainerType2>
inline void doSymv( MatrixType&& M,
                  const ContainerType1& x,
                  ContainerType2& y,
                  SparseBlockMatrixTag);
template< class MatrixType, class ContainerType1, class ContainerType2>
inline void doSymv( get_value_type<ContainerType1> alpha,
                  MatrixType&& M,
                  const ContainerType1& x,
                  get_value_type<ContainerType1> beta,
                  ContainerType2& y,
                  SelfMadeMatrixTag);
template< class MatrixType, class ContainerType1, class ContainerType2>
inline void doSymv( MatrixType&& M,
                  const ContainerType1& x,
                  ContainerType2& y,
                  SelfMadeMatrixTag);
}//namespace detail
}//namespace blas2
///@endcond


///@addtogroup mpi_structures
///@{

/**
* @brief Distributed memory matrix class, asynchronous communication
*
* The idea of this mpi matrix is to separate communication and computation in order to reuse existing optimized matrix formats for the computation.
* It can be expected that this works particularly well for cases in which the communication to computation ratio is low.
* This class assumes that the matrix and vector elements are distributed rowwise among mpi processes.
* The matrix elements are then further separated into columns that are inside the domain and the ones that are outside, i.e.
* \f[
 M=M_i+M_o
 \f]
 where \f$ M_i\f$ is the inner matrix which requires no communication, while
 \f$ M_o\f$ is the outer matrix containing all elements which require
 communication from the Collective object.
* @tparam LocalMatrixInner The class of the matrix for local computations of the inner points.
 symv(m,x,y) needs to be callable on the container class of the MPI_Vector
* @tparam LocalMatrixOuter The class of the matrix for local computations of the outer points.
 symv(1,m,x,1,y) needs to be callable on the container class of the MPI_Vector
* @tparam Collective must be a \c NearestNeighborComm The Communication class needs to gather values across processes. The \c global_gather_init(), \c global_gather_wait() and \c size()
member functions are called.
Gather points from other processes that are necessary for the outer computations.
 If \c size()==0 the global_gather() function won't be called and
only the inner matrix is applied.
@note This class overlaps communication with computation of the inner matrix
*/
template<class LocalMatrixInner, class LocalMatrixOuter, class Collective >
struct RowColDistMat
{
    ///@brief no memory allocation
    RowColDistMat(){}

    /**
    * @brief Constructor
    *
    * @param inside The local matrix for the inner elements
    * @param outside A local matrix for the elements from other processes
    * @param c The communication object
    */
    RowColDistMat( const LocalMatrixInner& inside, const LocalMatrixOuter& outside, const Collective& c):
        m_i(inside), m_o(outside), m_c(c), m_buffer( c.allocate_buffer()) { }


    /**
    * @brief Copy constructor

    * The idea is that a device matrix can be constructed by copying a host matrix.
    *
    * @tparam OtherMatrixInner LocalMatrixInner must be copy-constructible from OtherMatrixInner
    * @tparam OtherMatrixOuter LocalMatrixOuter must be copy-constructible from OtherMatrixOuter

    * @tparam OtherCollective Collective must be copy-constructible from OtherCollective

    * @param src another Matrix
    */
    template< class OtherMatrixInner, class OtherMatrixOuter, class OtherCollective>
    RowColDistMat( const RowColDistMat<OtherMatrixInner, OtherMatrixOuter, OtherCollective>& src):
        m_i(src.inner_matrix()), m_o( src.outer_matrix()), m_c(src.collective()), m_buffer( m_c.allocate_buffer()) { }
    /**
    * @brief Read access to the inner matrix
    *
    * @return
    */
    const LocalMatrixInner& inner_matrix() const{return m_i;}
    /**
    * @brief Read access to the outer matrix
    *
    * @return
    */
    const LocalMatrixOuter& outer_matrix() const{return m_o;}
    /**
    * @brief Read access to the communication object
    *
    * @return
    */
    const Collective& collective() const{return m_c;}

    /**
    * @brief Matrix Vector product
    *
    * First the inner elements are computed with a call to doSymv then
    * the global_gather function of the communication object is called.
    * Finally the outer elements are added with a call to doSymv for the outer matrix
    * @tparam ContainerType container class of the vector elements
    * @param alpha scalar
    * @param x input
    * @param beta scalar
    * @param y output
    */
    template<class ContainerType1, class ContainerType2>
    void symv( double alpha, const ContainerType1& x, double beta, ContainerType2& y) const
    {
        if( m_c.size() == 0) //no communication needed
        {
            dg::blas2::detail::doSymv( alpha, m_i, x.data(), beta, y.data(),
                       get_tensor_category<LocalMatrixInner>()
                       );
            return;

        }
        int result;
        MPI_Comm_compare( x.communicator(), y.communicator(), &result);
        assert( result == MPI_CONGRUENT || result == MPI_IDENT);
        MPI_Comm_compare( x.communicator(), m_c.communicator(), &result);
        assert( result == MPI_CONGRUENT || result == MPI_IDENT);

        //1.1 initiate communication
        MPI_Request rqst[4];
        m_c.global_gather_init( x.data(), rqst);
        //1.2 compute inner points
        dg::blas2::detail::doSymv( alpha, m_i, x.data(), beta, y.data(),
                       get_tensor_category<LocalMatrixInner>()
                       );
        //2. wait for communication to finish
        m_c.global_gather_wait( x.data(), m_buffer.data(), rqst);
        //3. compute and add outer points
        dg::blas2::detail::doSymv(alpha, m_o, m_buffer.data(), 1., y.data(),
                       get_tensor_category<LocalMatrixOuter>()
                       );
    }

    /**
    * @brief Matrix Vector product
    *
    * First the inner elements are computed with a call to doSymv then
    * the collect function of the communication object is called.
    * Finally the outer elements are added with a call to doSymv for the outer matrix
    * @tparam ContainerType container class of the vector elements
    * @param x input
    * @param y output
    */
    template<class ContainerType1, class ContainerType2>
    void symv( const ContainerType1& x, ContainerType2& y) const
    {
        if( m_c.size() == 0) //no communication needed
        {
            dg::blas2::detail::doSymv( m_i, x.data(), y.data(),
                       get_tensor_category<LocalMatrixInner>()
                       );
            return;

        }
        int result;
        MPI_Comm_compare( x.communicator(), y.communicator(), &result);
        assert( result == MPI_CONGRUENT || result == MPI_IDENT);
        MPI_Comm_compare( x.communicator(), m_c.communicator(), &result);
        assert( result == MPI_CONGRUENT || result == MPI_IDENT);

        //1.1 initiate communication
        MPI_Request rqst[4];
        m_c.global_gather_init( x.data(), rqst);
        //1.2 compute inner points
        dg::blas2::detail::doSymv( m_i, x.data(), y.data(),
                       get_tensor_category<LocalMatrixInner>()
                       );
        //2. wait for communication to finish
        m_c.global_gather_wait( x.data(), m_buffer.data(), rqst);
        //3. compute and add outer points
        dg::blas2::detail::doSymv(1, m_o, m_buffer.data(), 1., y.data(),
                       get_tensor_category<LocalMatrixOuter>()
                       );
    }

    private:
    LocalMatrixInner m_i;
    LocalMatrixOuter m_o;
    Collective m_c;
    Buffer< typename Collective::container_type>  m_buffer;
};


///Type of distribution of MPI distributed matrices
enum dist_type
{
    row_dist=0, //!< Row distributed
    col_dist=1 //!< Column distributed
};

/**
* @brief Distributed memory matrix class
*
* The idea of this mpi matrix is to separate communication and computation in order to reuse existing optimized matrix formats for the computation.
* It can be expected that this works particularly well for cases in which the communication to computation ratio is low.
* In this class the matrix elements can be distributed rowwise or columnwise among mpi processes.
* @tparam LocalMatrix The class of the matrix for local computations.
 symv needs to be callable on the container class of the MPI_Vector
* @tparam Collective models aCommunicator The Communication class needs to scatter and gather values across processes.
Gather all points (including the ones that the process already has) necessary for the local matrix-vector
product into one vector, such that the local matrix can be applied.
If size()==0 the global_gather and global_scatter_reduce functions won't be called and
only the local matrix is applied.
*/
template<class LocalMatrix, class Collective >
struct MPIDistMat
{
    ///@brief no memory allocation
    MPIDistMat( ) { }
    /**
    * @brief Constructor
    *
    * @param m The local matrix
    * @param c The communication object
    * @param dist either row or column distributed
    */
    MPIDistMat( const LocalMatrix& m, const Collective& c, enum dist_type dist = row_dist):
        m_m(m), m_c(c), m_buffer( c.allocate_buffer()), m_dist( dist) { }

    /**
    * @brief Copy Constructor
    *
    * @tparam OtherMatrix LocalMatrix must be copy-constructible from OtherMatrix
    * @tparam OtherCollective Collective must be copy-constructible from OtherCollective
    * @param src The other matrix
    */
    template< class OtherMatrix, class OtherCollective>
    MPIDistMat( const MPIDistMat<OtherMatrix, OtherCollective>& src):
        m_m(src.matrix()), m_c(src.collective()), m_buffer( m_c.get().allocate_buffer()), m_dist(src.get_dist()) { }
    /**
    * @brief Access to the local matrix
    *
    * @return Reference to the local matrix
    */
    const LocalMatrix& matrix() const{return m_m;}
    /**
    * @brief Access to the communication object
    *
    * @return Reference to the collective object
    */
    const Collective& collective() const{return m_c.get();}

    enum dist_type get_dist() const {return m_dist;}
    void set_dist(enum dist_type dist){m_dist=dist;}

    template<class ContainerType1, class ContainerType2>
    void symv( double alpha, const ContainerType1& x, double beta, ContainerType2& y) const
    {
        if( m_c.size() == 0) //no communication needed
        {
            dg::blas2::detail::doSymv( alpha, m_m, x.data(), beta, y.data(),
                       get_tensor_category<LocalMatrix>()
                       );
            return;

        }
        int result;
        MPI_Comm_compare( x.communicator(), y.communicator(), &result);
        assert( result == MPI_CONGRUENT || result == MPI_IDENT);
        MPI_Comm_compare( x.communicator(), m_c.get().communicator(), &result);
        assert( result == MPI_CONGRUENT || result == MPI_IDENT);
        if( m_dist == row_dist){
            m_c.global_gather( x.data(), m_buffer.data());
            dg::blas2::detail::doSymv( alpha, m_m, m_buffer.data(), beta, y.data(),
                       get_tensor_category<LocalMatrix>()
                       );
        }
        if( m_dist == col_dist){
            dg::blas2::detail::doSymv( alpha, m_m, x.data(), beta, m_buffer.data(),
                       get_tensor_category<LocalMatrix>()
                       );
            m_c.get().global_scatter_reduce( m_buffer.data(), y.data());
        }
    }
    template<class ContainerType1, class ContainerType2>
    void symv( const ContainerType1& x, ContainerType2& y) const
    {
        if( m_c.get().size() == 0) //no communication needed
        {
            dg::blas2::detail::doSymv( m_m, x.data(), y.data(),
                       get_tensor_category<LocalMatrix>()
                       );
            return;

        }
        int result;
        MPI_Comm_compare( x.communicator(), y.communicator(), &result);
        assert( result == MPI_CONGRUENT || result == MPI_IDENT);
        MPI_Comm_compare( x.communicator(), m_c.get().communicator(), &result);
        assert( result == MPI_CONGRUENT || result == MPI_IDENT);
        if( m_dist == row_dist){
            m_c.get().global_gather( x.data(), m_buffer.data());
            dg::blas2::detail::doSymv( m_m, m_buffer.data(), y.data(),
                       get_tensor_category<LocalMatrix>()
                       );
        }
        if( m_dist == col_dist){
            dg::blas2::detail::doSymv( m_m, x.data(), m_buffer.data(),
                       get_tensor_category<LocalMatrix>()
                       );
            m_c.get().global_scatter_reduce( m_buffer.data(), y.data());
        }
    }

    private:
    LocalMatrix m_m;
    ClonePtr<Collective> m_c;
    Buffer< typename Collective::container_type> m_buffer;
    enum dist_type m_dist;
};
///@}

///@addtogroup dispatch
///@{
template<class LI, class LO, class C>
struct TensorTraits<RowColDistMat<LI,LO, C> >
{
    using value_type = get_value_type<LI>;//!< value type
    using tensor_category = MPIMatrixTag;
};

template<class L, class C>
struct TensorTraits<MPIDistMat<L, C> >
{
    using value_type = get_value_type<L>;//!< value type
    using tensor_category = MPIMatrixTag;
};
///@}

} //namespace dg
