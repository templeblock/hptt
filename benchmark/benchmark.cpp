/**
 * @author: Paul Springer (springer@aices.rwth-aachen.de)
 */


#include <memory>
#include <vector>
#include <numeric>
#include <string>
#include <string.h>
#include <algorithm>
#include <iostream>
#include <omp.h>
#include <stdlib.h>
#include <math.h>
#include <cmath>
#include <complex>

#include "../src/hptt.h"

#include "defines.h"

template<typename floatType>
static double getZeroThreashold();
template<>
double getZeroThreashold<double>() { return 1e-16;}
template<>
double getZeroThreashold<DoubleComplex>() { return 1e-16;}
template<>
double getZeroThreashold<float>() { return 1e-6;}
template<>
double getZeroThreashold<FloatComplex>() { return 1e-6;}


int equal_(const floatType *A, const floatType*B, int total_size){
  int error = 0;
   for(int i=0;i < total_size ; ++i){
      if( A[i] != A[i] || B[i] != B[i]  || std::isinf(std::abs(A[i])) || std::isinf(std::abs(B[i])) ){
         error += 1; //test for NaN or Inf
         continue;
      }
      double Aabs = std::abs(A[i]);
      double Babs = std::abs(B[i]);
      double max = std::max(Aabs, Babs);
      double diff = Aabs - Babs;
      diff = (diff < 0) ? -diff : diff;
      if(diff > 0){
         double relError = diff / max;
         if(relError > 4e-5 && std::min(Aabs,Babs) > getZeroThreashold<floatType>()*5 ){
//            fprintf(stderr,"%.3e  %.3e %.3e\n",relError, A[i], B[i]);
            error += 1;
         }
      }
   }
   return (error == 0) ? 1 : 0;
}

void restore(const floatType* A, floatType* B, size_t n)
{
   for(size_t i=0;i < n ; ++i)
      B[i] = A[i];
}

void transpose_ref( uint32_t *size, uint32_t *perm, int dim, const floatType* __restrict__ A, floatType alpha, floatType* __restrict__ B, floatType beta);


int main(int argc, char *argv[]) 
{
  int numThreads = 1;
  if( getenv("OMP_NUM_THREADS") != NULL )
     numThreads = atoi(getenv("OMP_NUM_THREADS"));
  printf("numThreads: %d\n",numThreads);
  floatType alpha = 2.;
  floatType beta = 4.;
  //beta = 0; 

  if( argc < 2 ){
     printf("Usage: <dim> <permutation each index separated by ' '> <size of each index separated by ' '>\n");
     exit(-1);
  }
  int dim = atoi(argv[1]);
  if( argc < 2 + 2*dim ){
     printf("Error: not enough indices for permutation provided.");
     exit(-1);
  }
  uint32_t perm[dim];
  std::string perm_str = "";
  for(int i=0;i < dim ; ++i){
     perm[i] = atoi(argv[2+i]);
     perm_str += std::to_string(perm[i]) + ",";
  }
  uint32_t size[dim];
  std::string size_str = "";
  size_t total_size = 1;
  for(int i=0;i < dim ; ++i){
     size[i] = atoi(argv[2+dim+i]);
     size_str += std::to_string(size[i]) + ",";
     total_size *= size[i];
  }

  int nRepeat = 5;

  // Allocating memory for tensors
  int largerThanL3 = 1024*1024*100/sizeof(double);
  floatType *A, *B, *B_ref, *B_orig, *B_proto, *B_hptt;
  double *trash1, *trash2;
  int ret = posix_memalign((void**) &trash1, 64, sizeof(double) * largerThanL3);
  ret += posix_memalign((void**) &trash2, 64, sizeof(double) * largerThanL3);
  ret += posix_memalign((void**) &B, 64, sizeof(floatType) * total_size);
  ret += posix_memalign((void**) &A, 64, sizeof(floatType) * total_size);
  ret += posix_memalign((void**) &B_ref, 64, sizeof(floatType) * total_size);
#ifdef ORIG_TTC
  ret += posix_memalign((void**) &B_orig, 64, sizeof(floatType) * total_size);
#endif
  ret += posix_memalign((void**) &B_proto, 64, sizeof(floatType) * total_size);
#ifdef RELEASE_HPTT
  ret += posix_memalign((void**) &B_hptt, 64, sizeof(floatType) * total_size);
#endif
  if( ret ){
     printf("ALLOC ERROR\n");
     exit(-1);
  }

  // initialize data
#pragma omp parallel for
  for(int i=0;i < total_size; ++i)
     A[i] = (((i+1)*13 % 1000) - 500.) / 1000.;
#pragma omp parallel for
  for(int i=0;i < total_size ; ++i){
     B[i] = (((i+1)*17 % 1000) - 500.) / 1000.;
#ifdef ORIG_TTC
     B_orig[i] = B[i];
#endif
     B_ref[i]  = B[i];
#ifdef RELEASE_HPTT
     B_hptt[i] = B[i];
#endif
     B_proto[i] = B[i];
  }

#pragma omp parallel for
  for(int i=0;i < largerThanL3; ++i)
  {
     trash1[i] = ((i+1)*13)%10000;
     trash2[i] = ((i+1)*13)%10000;
  }
  
  {  //hptt prototype
     int perm_[dim];
     int size_[dim];
     for(int i=0;i < dim ; ++i){
        perm_[i] = (int)perm[i];
        size_[i] = (int)size[i];
     }
     //library warm-up
     auto plan2 = hptt::create_plan( perm_, dim, 
           alpha, A, size_, NULL, 
           beta, B_proto, NULL, 
           hptt::ESTIMATE, numThreads);

     auto plan = hptt::create_plan( perm_, dim, 
           alpha, A, size_, NULL, 
           beta, B_proto, NULL, 
           hptt::ESTIMATE, numThreads);

     double minTime = 1e200;
     for(int i=0;i < nRepeat ; ++i){
        restore(B, B_proto, total_size);
        hptt::trashCache(trash1, trash2, largerThanL3);
        auto begin_time = omp_get_wtime();
        // Execute transpose
        plan->execute();
        auto elapsed_time = omp_get_wtime() - begin_time;
        minTime = (elapsed_time < minTime) ? elapsed_time : minTime;
     }
     printf("HPTT (proto) %d %s %s: %.2f ms. %.2f GiB/s\n", dim, perm_str.c_str(), size_str.c_str(), minTime*1000, sizeof(floatType)*total_size*3/1024./1024./1024 / minTime);
  }

  { // reference
     double minTime = 1e200;
     for(int i=0;i < nRepeat ; ++i){
        restore(B, B_ref, total_size);
        hptt::trashCache(trash1, trash2, largerThanL3);
        auto begin_time = omp_get_wtime();
        transpose_ref( size, perm, dim, A, alpha, B_ref, beta);
        double elapsed_time = omp_get_wtime() - begin_time;
        minTime = (elapsed_time < minTime) ? elapsed_time : minTime;
     }
     printf("TTC (ref) %d %s %s: %.2f ms. %.2f GiB/s\n", dim, perm_str.c_str(), size_str.c_str(), minTime*1000, sizeof(floatType)*total_size*3/1024./1024./1024 / minTime);
  }

  // Verification
  if( !equal_(B_ref, B_proto, total_size) )
     fprintf(stderr, "error in ttc_proto\n");

  return 0;
}
