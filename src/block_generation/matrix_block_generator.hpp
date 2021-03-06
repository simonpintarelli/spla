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
#ifndef SPLA_MATRIX_BLOCK_GENERATOR_HPP
#define SPLA_MATRIX_BLOCK_GENERATOR_HPP

#include "spla/config.h"
#include "util/common_types.hpp"

namespace spla {
struct BlockInfo {
  IntType globalRowIdx, globalColIdx; // Indices of first element in block in global matrix
  IntType globalSubRowIdx, globalSubColIdx; // Indices of first element in block in global matrix without offset
  IntType localRowIdx, localColIdx; // Indices of first element in block on assigned mpi rank
  IntType numRows, numCols; // Size of block
  IntType mpiRank; // negative value indicates mirrored on all ranks
};

class MatrixBlockGenerator {
  public:

  virtual auto get_block_info(IntType blockIdx) -> BlockInfo =0;

  virtual auto get_block_info(IntType blockRowIdx, IntType blockColIdx) -> BlockInfo =0;

  virtual auto num_blocks() -> IntType = 0;

  virtual auto num_block_rows() -> IntType = 0;

  virtual auto num_block_cols() -> IntType = 0;

  virtual auto max_rows_in_block() -> IntType = 0;

  virtual auto max_cols_in_block() -> IntType = 0;

  virtual auto local_rows(IntType rank) -> IntType = 0;

  virtual auto local_cols(IntType rank) -> IntType = 0;

  virtual ~MatrixBlockGenerator() = default;
};
}

#endif
