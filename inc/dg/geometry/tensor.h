#pragma once

#include "operator.h"
#include "dg/functors.h"
#include "dg/blas1.h"

namespace dg
{
    //separate algorithms from interface!!

/**
 * @brief This is a sparse Tensor with only one element i.e. a Form
 *
 * @tparam T a T class
 * @ingroup misc
 */
template<class T>
struct SparseElement
{
    ///create empty object
    SparseElement(){}
    /**
     * @brief copy construct value
     * @param value a value
     */
    SparseElement(const T& value):value_(1,value){ }
    /**
     * @brief Type conversion from other value types
     * @tparam OtherT dg::blas1::transfer must be callable for T and OtherT
     * @param src the source matrix to convert
     */
    template<class OtherT>
    SparseElement( const SparseElement<OtherT>& src)
    {
        if(src.isSet())
        {
            value_.resize(1);
            dg::blas1::transfer(src.value(), value_[0]);
        }
    }

    ///@brief Read access
    ///@return read access to contained value
    const T& value( )const {
        return value_[0];
    }
    /**
     * @brief write access, create a T if there isn't one already
     * @return write access, always returns a T
     */
    T& value() {
        if(!isSet()) value_.resize(1);
        return value_[0];
    }

    /**
     * @brief check if an element is set or not
     * @return false if the value array is empty
     */
    bool isSet()const{
        if( value_.empty()) return false;
        return true;
    }
    ///@brief Clear contained value
    void clear(){value_.clear();}
    private:
    std::vector<T> value_;
};

/**
* @brief Class for 2x2 and 3x3 matrices sharing or implicitly assuming elements
*
* This class enables shared access to stored Ts
* or not store them at all since the storage of (and computation with) a T is expensive.

* This class contains a (dense) 3x3 matrix of integers.
* If positive or zero, the integer represents a gather index into the stored array of Ts,
if negative the value of the T is assumed to be 1, except for the off-diagonal entries
    in the matrix where it is assumed to be 0.
* We then only need to store non-trivial and non-repetitive Ts.
* @tparam T must be default constructible and copyable.
* @ingroup misc
*/
template<class T>
struct SparseTensor
{
    ///no element is set
    SparseTensor( ):mat_idx_(3,-1) {}

    /**
     * @brief reserve space for value_size Ts in the values array
     * @param value_size reserve space for this number of Ts (default constructor)
     */
    SparseTensor( unsigned value_size): mat_idx_(3,-1), values_(value_size){}

    /**
    * @brief pass array of Ts
    * @param values The contained Ts are stored in the object
    */
    SparseTensor( const std::vector<T>& values ): mat_idx_(3,-1), values_(values){}

    /**
     * @brief Type conversion from other value types
     * @tparam OtherT \c dg::blas1::transfer must be callable for \c T and \c OtherT
     * @param src the source matrix to convert
     */
    template<class OtherT>
    SparseTensor( const SparseTensor<OtherT>& src): mat_idx_(3,-1), values_(src.values().size()){
        for(unsigned i=0; i<3; i++)
            for(unsigned j=0; j<3; j++)
                mat_idx_(i,j)=src.idx(i,j);

        for( unsigned i=0; i<src.values().size(); i++)
            dg::blas1::transfer( src.values()[i], values_[i]);
    }

    /**
    * @brief check if a value is set at the given position or not
    * @param i row index 0<=i<3
    * @param j col index 0<=j<3
    * @return true if \c T is non-empty, false if value is assumed implicitly
    */
    bool isSet(size_t i, size_t j)const{
        if( mat_idx_(i,j) <0) return false;
        return true;
    }

    /**
    * @brief read index into the values array at the given position
    * @param i row index 0<=i<3
    * @param j col index 0<=j<3
    * @return -1 if \c !isSet(i,j), index into values array else
    */
    int idx(unsigned i, unsigned j)const{return mat_idx_(i,j);}
    /**
    * @brief write index into the values array at the given position
    *
    * use this and the \c values() member to assemble the tensor
    * @param i row index 0<=i<3
    * @param j col index 0<=j<3
    * @return write access to value index to be set
    */
    int& idx(unsigned i, unsigned j){return mat_idx_(i,j);}
     /*! @brief unset an index, does not clear the associated value
      *
      * @param i row index 0<=i<3
      * @param j col index 0<=j<3
      */
     void unset( unsigned i, unsigned j) {
         mat_idx_(i,j)=-1;
     }
     /**
      * @brief clear any unused values and reset the corresponding indices
      *
      * This function erases all values that are unreferenced by any index and appropriately redefines the remaining indices
      */
     void clear_unused_values();

    /*!@brief Read access the underlying T
     * @return if !isSet(i,j) the result is undefined, otherwise values[idx(i,j)] is returned.
     * @param i row index 0<=i<3
     * @param j col index 0<=j<3
     * @note If the indices fall out of range of index the result is undefined
     */
    const T& value(size_t i, size_t j)const{
        int k = mat_idx_(i,j);
        return values_[k];
    }
    //if you're looking for this function: YOU DON'T NEED IT!!ALIASING
    //T& value(size_t i, size_t j);
    /**
     * @brief Return write access to the values array
     * @return write access to values array
     */
    std::vector<T>& values() {return values_;}
    /**
     * @brief Return read access to the values array
     * @return read access to values array
     */
    const std::vector<T>& values()const{return values_;}
    ///clear all values, Tensor is empty after that
    void clear(){
        mat_idx_=dg::Operator<int>(3,-1);
        values_.clear();
    }

    /**
    * @brief Test the matrix for emptiness
    *
    * The matrix is empty if !isSet(i,j) for all i and j
    * @return true if no value in the matrix is set
    */
    bool isEmpty()const{
        bool empty=true;
        for(unsigned i=0; i<3; i++)
            for( unsigned j=0; j<3; j++)
                if( isSet(i,j) )
                    empty=false;
        return empty;
    }

    /**
    * @brief Test if all elements are set
    *
    * The matrix is dense if isSet(i,j) for all i and j
    * @return true if all values in the matrix are set
    */
    bool isDense()const{
        bool dense=true;
        for(unsigned i=0; i<3; i++)
            for( unsigned j=0; j<3; j++)
                if( !isSet(i,j) )
                    dense=false;
        return dense;
    }

    /**
    * @brief Test if the elements in the third dimension are unset
    *
    * The matrix is perpendicular if !isSet(i,j) for any i,j=2
    * @return true if perpenicular
    * */
    bool isPerp() const {
        bool empty=true;
        for(unsigned i=0; i<3; i++)
        {
            if( isSet(i,2) || isSet(2,i))
                empty=false;
        }
        return empty;
    }
    /**
    * @brief Test if no off-diagonals are set
    *
    * The matrix is diagonal if no off-diagonal element is set
    * @return true if no off-diagonal element is set
    */
    bool isDiagonal()const{
        bool diagonal=true;
        for(unsigned i=0; i<3; i++)
            for(unsigned j=i+1; j<3; j++)
                if( isSet(i,j) ||  isSet(j,i))
                    diagonal=false;
        return diagonal;
    }

     ///construct an empty Tensor
     SparseTensor empty()const{return SparseTensor();}
     ///erase all values in the third dimension
     ///@note calls clear_unused_values() to get rid of the elements
     SparseTensor perp()const;
     ///erase all values in the first two dimensions (leave only (2,2))
     SparseTensor parallel()const;

    /**
     * @brief Return the transpose of the currrent tensor
     * @return swapped rows and columns
     */
    SparseTensor transpose()const{
        SparseTensor tmp(*this);
        tmp.mat_idx_ = mat_idx_.transpose();
        return tmp;
    }

    private:
    dg::Operator<int> mat_idx_;
    std::vector<T> values_;
    void unique_insert(std::vector<int>& indices, int& idx);
};

namespace tensor
{
 /**
 * @brief Construct a tensor with all unset values filled with explicit 0 or 1 using \c dg::blas1::transform
 *
 * @copydoc hide_ContainerType
 * @return a dense tensor
 * @note undefined if \c t.isEmpty() returns true
 */
template<class ContainerType>
SparseTensor<ContainerType> dense(const SparseTensor<ContainerType>& tensor)
{
    SparseTensor<ContainerType> t(tensor);
    if( t.isEmpty()) throw Error(Message(_ping_)<< "Can't make an empty tensor dense! ") ;
    ContainerType tmp = t.values()[0];
    //1. diagonal
    size_t size= t.values().size();
    bool diagonalIsSet=true;
    for(unsigned i=0; i<3; i++)
        if(!t.isSet(i,i)) diagonalIsSet = false;
    if (!diagonalIsSet){
        dg::blas1::transform( tmp, tmp, dg::CONSTANT(1));
        t.values().resize(size+1);
        t.values()[(size)]=tmp;
        for(unsigned i=0; i<3; i++)
            if(!t.isSet(i,i)) t.idx(i,i) = size;
    }
    //2. off-diagonal
    size = t.values().size();
    bool offIsSet=true;
    for(unsigned i=0; i<3; i++)
        for(unsigned j=0; j<3; j++)
            if( !t.isSet(i,j) ) offIsSet=false;
    if (!offIsSet){
        dg::blas1::transform( tmp, tmp, dg::CONSTANT(0));
        t.values().resize(size+1);
        t.values()[size]=tmp;
        for(unsigned i=0; i<3; i++)
            for(unsigned j=0; j<3; j++)
                if(!t.isSet(i,j) ) t.idx(i,j) = size;
    }
    return t;
}
}//namespace tensor

/**
 * @brief data structure to hold the LDL^T decomposition of a symmetric positive definite matrix
 *
 * LDL^T stands for a lower triangular matrix L,  a diagonal matrix D and the transpose L^T
 * @copydoc hide_ContainerType
 * @attention the tensor in the Elliptic classes actually only need to be positive **semi-definite**
 * and unfortunately the decomposition is unstable for semi-definite matrices.
* @ingroup misc
 */
template<class ContainerType>
struct CholeskyTensor
{
    /**
     * @brief decompose given tensor
     *
     * @param in must be symmetric and positive definite
     */
    CholeskyTensor( const SparseTensor<ContainerType>& in) {
        decompose(in);
    }
    /**
     * @brief Type conversion from other value types
     * @tparam OtherContainer dg::blas1::transfer must be callable for ContainerType and OtherContainer
     * @param in the source matrix to convert
     */
    template<class OtherContainer>
    CholeskyTensor( const CholeskyTensor<OtherContainer>& in):q_(in.lower()),diag_(in.diagonal()),upper_(in.upper()) { }

    /**
     * @brief decompose given tensor
     *
     * overwrites the existing decomposition
     * @param in must be symmetric and positive definite
     */
    void decompose( const SparseTensor<ContainerType>& in)
    {
        SparseTensor<ContainerType> denseIn=dg::tensor::dense(in);
        /*
         * One nice property of positive definite is that the diagonal elements are
         * greater than zero.
         */

        if(in.isSet(0,0))
        {
            diag_.idx(0,0)=0;
            if(diag_.values().size() == 0)
                diag_.values().resize(1);
            diag_.values()[0]=in.value(0,0);
        }
        if(in.isSet(1,0))
        {
            ContainerType tmp=in.value(1,0);
            if(diag_.isSet(0,0)) dg::blas1::pointwiseDivide(tmp,diag_.value(0,0),tmp);
            q_.idx(1,0)=0;
            if(q_.values().size() == 0)
                q_.values().resize(1);
            q_.values()[0]=tmp;
        }
        if(in.isSet(2,0))
        {
            ContainerType tmp=in.value(2,0);
            if(diag_.isSet(0,0))dg::blas1::pointwiseDivide(tmp,diag_.value(0,0),tmp);
            q_.idx(2,0)=1;
            if(q_.values().size() < 2)
                q_.values().resize(2);
            q_.values()[1]=tmp;
        }

        if( q_.isSet(1,0) || in.isSet(1,1))
        {
            SparseTensor<ContainerType> denseL=dg::tensor::dense(q_);
            ContainerType tmp=denseL.value(1,0);
            dg::blas1::pointwiseDot(tmp,tmp,tmp);
            if(diag_.isSet(0,0)) dg::blas1::pointwiseDot(tmp,diag_.value(0,0),tmp);
            dg::blas1::axpby( 1., denseIn.value(1,1), -1., tmp, tmp);
            diag_.idx(1,1)=1;
            if(diag_.values().size() < 2)
                diag_.values().resize(2);
            diag_.values()[1] = tmp;
        }

        if( in.isSet(2,1) || (q_.isSet(2,0)&&q_.isSet(1,0)))
        {
            SparseTensor<ContainerType> denseL=dg::tensor::dense(q_);
            ContainerType tmp=denseIn.value(2,1);
            dg::blas1::pointwiseDot(denseL.value(2,0), denseL.value(1,0), tmp);
            if(diag_.isSet(0,0))dg::blas1::pointwiseDot(tmp, diag_.value(0,0), tmp);
            dg::blas1::axpby(1., denseIn.value(2,1),-1.,tmp, tmp);
            if(diag_.isSet(1,1))dg::blas1::pointwiseDivide(tmp, diag_.value(1,1),tmp);
            q_.idx(2,1)=2;
            if(q_.values().size() < 3)
                q_.values().resize(3);
            q_.values()[2]=tmp;
        }
        if( in.isSet(2,2) || q_.isSet(2,0) || q_.isSet(2,1))
        {
            SparseTensor<ContainerType> denseL=dg::tensor::dense(q_);
            ContainerType tmp=denseL.value(2,0), tmp1=denseL.value(2,1);
            dg::blas1::pointwiseDot(tmp,tmp,tmp);
            if(diag_.isSet(0,0))dg::blas1::pointwiseDot(diag_.value(0,0),tmp,tmp);
            dg::blas1::pointwiseDot(tmp1,tmp1,tmp1);
            if(diag_.isSet(1,1))dg::blas1::pointwiseDot(diag_.value(1,1),tmp1,tmp1);
            dg::blas1::axpby(1., denseIn.value(2,2), -1., tmp, tmp);
            dg::blas1::axpby(1., tmp, -1., tmp1, tmp);
            diag_.idx(2,2)=2;
            if(diag_.values().size() < 3)
                diag_.values().resize(3);
            diag_.values()[2]=tmp;
        }
        diag_.clear_unused_values();
        q_.clear_unused_values();
        upper_=q_.transpose();
    }

    /**
     * @brief Returns L
     * @return a lower triangular matrix with 1 on diagonal
     */
    const SparseTensor<ContainerType>& lower()const{
        return q_;

    }
    /**
     * @brief Returns L^T
     * @return a upper triangular matrix with 1 on diagonal
     */
    const SparseTensor<ContainerType>& upper()const{
        return upper_;

    }

    /**
     * @brief Returns D
     * @return only diagonal elements are set if any
     */
    const SparseTensor<ContainerType>& diagonal()const{return diag_;}

    private:
    SparseTensor<ContainerType> q_, diag_, upper_;
    bool lower_;
};

///@cond

template<class ContainerType>
void SparseTensor<ContainerType>::unique_insert(std::vector<int>& indices, int& idx)
{
    bool unique=true;
    unsigned size=indices.size();
    for(unsigned i=0; i<size; i++)
        if(indices[i] == idx) {
            unique=false;
            idx=i;
        }
    if(unique)
    {
        indices.push_back(idx);
        idx=size;
    }
}

template<class ContainerType>
SparseTensor<ContainerType> SparseTensor<ContainerType>::perp() const
{
    SparseTensor<ContainerType> t(*this);
    if( isEmpty()) return t;
    for(unsigned i=0; i<3; i++)
    {
        t.mat_idx_(2,i)=-1;
        t.mat_idx_(i,2)=-1;
    }
    t.clear_unused_values();
    return t;
}
template<class ContainerType>
SparseTensor<ContainerType> SparseTensor<ContainerType>::parallel() const
{
    SparseTensor<ContainerType> t;
    if( isEmpty()) return t;
    if( isSet( 2,2) )
    {
        t.mat_idx_(2,2) = 0;
        t.values_.assign(1,values_[idx(2,2)] );
    }
    return t;
}

template<class ContainerType>
void SparseTensor<ContainerType>::clear_unused_values()
{
    //now erase unused elements and redefine indices
    std::vector<int> unique_idx;
    for(unsigned i=0; i<3; i++)
        for(unsigned j=0; j<3; j++)
            if(isSet(i,j))
                unique_insert( unique_idx, mat_idx_(i,j));

    std::vector<ContainerType> tmp(unique_idx.size());
    for(unsigned i=0; i<unique_idx.size(); i++)
        tmp[i] = values_[unique_idx[i]];
    values_.swap(tmp);
}
///@endcond


}//namespace dg
