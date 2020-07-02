/*
 * Copyright (c) 2020 ETH Zurich, Simon Frasch
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

#include "block_generation/block_cyclic_generator.hpp"
#include "block_generation/matrix_block_generator.hpp"
#include "block_generation/mirror_generator.hpp"
#include "mpi_util/mpi_check_status.hpp"
#include "spla/context_internal.hpp"
#include "spla/exceptions.hpp"
#include "spla/matrix_distribution_internal.hpp"
#include "spla/spla.hpp"
#include "tile_host.hpp"
#include "timing/timing.hpp"
#include "util/blas_interface.hpp"
#include "util/blas_threads_guard.hpp"
#include "util/common_types.hpp"
#include "util/omp_definitions.hpp"
#include "gemm/gemm_host.hpp"

namespace spla {
/*
 *    ------ H     ------
 *    |    |       |    |
 *    |    |       |    |
 *    ------       ------        -------
 *    |    |       |    |        |  |  |
 *    |    |   *   |    |    =   -------
 *    ------       ------        |  |  |
 *    |    |       |    |        -------
 *    |    |       |    |           C
 *    ------       -
 *    |    |       |    |
 *    |    |       |    |
 *    ------       -
 *      A            B
 */
template <typename T>
void gemm_ssb_host(int m, int n, int kLocal, T alpha, const T *A, int lda, const T *B, int ldb,
                   T beta, T *C, int ldc, int cRowStart, int cColStart,
                   MatrixDistributionInternal &descC, ContextInternal &ctx) {

  if (m == 0 || n == 0) {
    return;
  }

  if (descC.comm().size() == 1) {
    return gemm_host<T>(ctx.num_threads(), SPLA_OP_CONJ_TRANSPOSE, SPLA_OP_NONE, m, n, kLocal,
                        alpha, A, lda, B, ldb, beta,
                        C + cRowStart + cColStart * ldc, ldc);
  }

  HostArrayConstView2D<T> viewA(A, m, kLocal, lda);
  HostArrayConstView2D<T> viewB(B, n, kLocal, ldb);
  HostArrayView2D<T> viewC(C, n + cColStart, ldc, ldc);

  std::shared_ptr<MatrixBlockGenerator> matrixDist;
  if (descC.type() == SplaDistributionType::SPLA_DIST_BLACS_BLOCK_CYCLIC) {
    matrixDist.reset(new BlockCyclicGenerator(descC.row_block_size(), descC.col_block_size(),
                                              descC.proc_grid_rows(), descC.proc_grid_cols(), m, n,
                                              cRowStart, cColStart));
  } else {
    matrixDist.reset(new MirrorGenerator(ctx.tile_length_target(), ctx.tile_length_target(), m, n,
                                         cRowStart, cColStart));
  }
  const IntType numBlockRows = matrixDist->num_block_rows();
  const IntType numBlockCols = matrixDist->num_block_cols();

  const IntType numBlockRowsInTile =
      (ctx.tile_length_target() + matrixDist->max_rows_in_block() - 1) /
      matrixDist->max_rows_in_block();
  const IntType numBlockColsInTile =
      (ctx.tile_length_target() + matrixDist->max_cols_in_block() - 1) /
      matrixDist->max_cols_in_block();

  std::vector<TileHost<T>> tiles;
  tiles.reserve(ctx.num_tiles());

  // create tiles
  {
    auto &buffers = ctx.mpi_buffers(ctx.num_tiles());
    auto &comms = descC.get_comms(ctx.num_tiles());
    IntType idx = 0;
    for (IntType tileIdx = 0; tileIdx < ctx.num_tiles();
         ++tileIdx, ++idx) {
      tiles.emplace_back(ctx.num_threads(), comms[idx], buffers[idx],
                         matrixDist, alpha, viewA, viewB, beta, viewC,
                         numBlockRowsInTile, numBlockColsInTile);
    }
  }

  SCOPED_TIMING("inner_host_thread_multiple");

  IntType currentTileIdx = 0;

  for (IntType blockRowIdx = 0; blockRowIdx < numBlockRows;
       blockRowIdx += numBlockRowsInTile) {
    for (IntType blockColIdx = 0; blockColIdx < numBlockCols;
         blockColIdx += numBlockColsInTile) {

      IntType nextTileIdx = (currentTileIdx + 1) % ctx.num_tiles();

      if (tiles[nextTileIdx].state() == TileState::InExchange) {
        START_TIMING("finalize_exchange");
        tiles[nextTileIdx].finalize_exchange();
        STOP_TIMING("finalize_exchange");
        START_TIMING("extract");
        tiles[nextTileIdx].extract();
        STOP_TIMING("extract");
      }

      START_TIMING("blas_multiply");
      tiles[currentTileIdx].multiply(blockRowIdx, blockColIdx);
      STOP_TIMING("blas_multiply");
      START_TIMING("start_exchange");
      tiles[currentTileIdx].start_exchange();
      STOP_TIMING("start_exchange");

      currentTileIdx = nextTileIdx;
    }
  }
  for (IntType i = 0; i < ctx.num_tiles(); ++i) {
    if (tiles[i].state() == TileState::InExchange) {
      START_TIMING("finalize_exchange");
      tiles[i].finalize_exchange();
      STOP_TIMING("finalize_exchange");
      START_TIMING("extract");
      tiles[i].extract();
      STOP_TIMING("extract");
    }
  }
}

template void gemm_ssb_host<float>(int m, int n, int kLocal, float alpha,
                                   const float *A, int lda, const float *B,
                                   int ldb, float beta, float *C, int ldc,
                                   int cRowStart, int cColStart,
                                   MatrixDistributionInternal &descC,
                                   ContextInternal &ctx);

template void gemm_ssb_host<double>(int m, int n, int kLocal, double alpha, const double *A,
                                    int lda, const double *B, int ldb, double beta, double *C,
                                    int ldc, int cRowStart, int cColStart,
                                    MatrixDistributionInternal &descC, ContextInternal &ctx);

template void gemm_ssb_host<std::complex<float>>(
    int m, int n, int kLocal, std::complex<float> alpha, const std::complex<float> *A, int lda,
    const std::complex<float> *B, int ldb, std::complex<float> beta, std::complex<float> *C,
    int ldc, int cRowStart, int cColStart, MatrixDistributionInternal &descC, ContextInternal &ctx);

template void gemm_ssb_host<std::complex<double>>(
    int m, int n, int kLocal, std::complex<double> alpha, const std::complex<double> *A, int lda,
    const std::complex<double> *B, int ldb, std::complex<double> beta, std::complex<double> *C,
    int ldc, int cRowStart, int cColStart, MatrixDistributionInternal &descC, ContextInternal &ctx);
}  // namespace spla
