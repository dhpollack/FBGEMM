/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <gtest/gtest.h>

#include "QuantizationHelpers.h"
#include "TestUtils.h"
#include "bench/BenchUtils.h"
#include "fbgemm/Fbgemm.h"
#include "src/RefImplementations.h"

using namespace std;
using namespace fbgemm;

vector<matrix_op_t> transposeVals{matrix_op_t::NoTranspose,
                                  matrix_op_t::Transpose};

vector<QuantizationGranularity> qGranularityVals{
    QuantizationGranularity::TENSOR,
    QuantizationGranularity::GROUP,
    QuantizationGranularity::OUT_CHANNEL};

namespace {
class fbgemmGConvAcc32Test
    : public testing::TestWithParam<tuple<matrix_op_t, matrix_op_t>> {};
class fbgemmGConvAcc32WithQuantGranularityTest
    : public testing::TestWithParam<tuple<
          matrix_op_t,
          matrix_op_t,
          QuantizationGranularity,
          bool,
          bool>> {};
}; // namespace

INSTANTIATE_TEST_CASE_P(
    InstantiationName,
    fbgemmGConvAcc32Test,
    ::testing::Combine(
        ::testing::Values(matrix_op_t::NoTranspose),
        ::testing::ValuesIn(transposeVals)));

INSTANTIATE_TEST_CASE_P(
    InstantiationName,
    fbgemmGConvAcc32WithQuantGranularityTest,
    ::testing::Combine(
        ::testing::Values(matrix_op_t::NoTranspose),
        ::testing::ValuesIn(transposeVals),
        ::testing::ValuesIn(qGranularityVals),
        ::testing::Bool(), // A symmetric
        ::testing::Bool())); // B symmetric
/**
 * @brief Shapes for unit test.
 */
static vector<conv_param_t<>> GetShapes_() {
  vector<conv_param_t<>> shapes = {
      // MB, IC, OC, {IH, IW}, G, {KH, KW}, {stride_h, stride_w}, {pad_t, pad_l,
      // pad_b, pad_r}
      conv_param_t<>(1, 32, 32, {3, 3}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      conv_param_t<>(1, 32, 32, {4, 4}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      conv_param_t<>(1, 32, 32, {3, 5}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      conv_param_t<>(1, 32, 32, {5, 3}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      conv_param_t<>(1, 8, 8, {5, 5}, 2, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      conv_param_t<>(1, 128, 128, {56, 48}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      conv_param_t<>(1, 128, 128, {48, 56}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // the line below is from resnext101-32x4d
      conv_param_t<>(1, 128, 128, {56, 56}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      conv_param_t<>(2, 128, 128, {56, 56}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),

      // The following lines are commented to reduce test time but still valid
      // when we want more extensive testings.
      // conv_param_t<>(1, 64, 64, {3, 3}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 64, 64, {4, 4}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 64, 64, {3, 5}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 64, 64, {5, 3}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 16, 16, {5, 5}, 2, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 256, 256, {56, 48}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 256, 256, {48, 56}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 256, 256, {56, 56}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(2, 256, 256, {56, 56}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),

      // conv_param_t<>(1, 128, 128, {3, 3}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 128, 128, {4, 4}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 128, 128, {3, 5}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 128, 128, {5, 3}, 8, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 32, 32, {5, 5}, 2, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 512, 512, {56, 48}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 512, 512, {48, 56}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(1, 512, 512, {56, 56}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
      // conv_param_t<>(2, 512, 512, {56, 56}, 32, {3, 3}, {1, 1}, {1, 1, 1, 1}),
  };
  return shapes;
}

/**
 * @brief Unit test for uint8 activations, int8 weights, and 32-bit
 * accumulation. Output processing: requantization -> nothing
 */
TEST_P(fbgemmGConvAcc32WithQuantGranularityTest, requantizeTest) {
  vector<conv_param_t<>> shapes(GetShapes_());
  matrix_op_t atrans, btrans;
  QuantizationGranularity q_granularity;
  bool a_symmetric, b_symmetric;
  tie(atrans, btrans, q_granularity, a_symmetric, b_symmetric) = GetParam();

  for (auto conv_p : shapes) {
    int R = conv_p.K[0];
    int S = conv_p.K[1];
    int G = conv_p.G;
    int OC = conv_p.OC;
    int OH = conv_p.OUT_DIM[0];
    int OW = conv_p.OUT_DIM[1];
    int IC_per_G = conv_p.IC / conv_p.G;
    int OC_per_G = conv_p.OC / conv_p.G;

    // activations
    aligned_vector<uint8_t> Aint8(
        conv_p.MB * conv_p.IN_DIM[0] * conv_p.IN_DIM[1] * conv_p.IC, 0);

    // weights
    // when btrans == Transpose, the weight matrix is in layout G K/G (R S C/G)
    // instead of G (R S C/G) K/G
    aligned_vector<int8_t> Bint8(R * S * conv_p.G * IC_per_G * OC_per_G, 0);
    aligned_vector<int8_t> Bint8_tr(R * S * G * IC_per_G * OC_per_G, 0);

    aligned_vector<int32_t> Cint32_ref(conv_p.MB * OH * OW * OC, 0);
    aligned_vector<int32_t> Cint32_fb(Cint32_ref.size(), 0);
    aligned_vector<uint8_t> Cint8_ref(Cint32_ref.size(), 0);
    aligned_vector<uint8_t> Cint8_fb(Cint32_ref.size(), 0);

    randFill<uint8_t>(Aint8, 0, 5);
    int32_t Aint8_zero_point = a_symmetric ? 0 : 4;

    randFill<int8_t>(Bint8, -4, 4);

    // computing column offset
    vector<int32_t> col_offsets(G * OC_per_G);

    int ncols_per_quant_group = G * OC_per_G;
    if (q_granularity == QuantizationGranularity::GROUP) {
      ncols_per_quant_group = OC_per_G;
    } else if (q_granularity == QuantizationGranularity::OUT_CHANNEL) {
      ncols_per_quant_group = 1;
    }

    aligned_vector<int32_t> Bint8_zero_point(
        G * OC_per_G / ncols_per_quant_group);
    if (b_symmetric) {
      randFill(Bint8_zero_point, -3, -1);
    } else {
      randFill(Bint8_zero_point, 0, 0);
    }

    // matrix dimensions after im2col for each GEMM.
    // For each group, there is one GEMM of the following dimensions
    int MDim = conv_p.MB * OH * OW;
    int NDim = OC_per_G;
    int KDim = R * S * IC_per_G;

    vector<uint8_t> Aint8_im2col(MDim * KDim * G);
    im2col_ref(conv_p, Aint8.data(), Aint8_zero_point, Aint8_im2col.data());

    vector<int32_t> row_offsets(MDim);

    aligned_vector<float> C_multiplier(Bint8_zero_point.size());
    randFill(C_multiplier, 0.1234f / 2, 0.1234f * 3 / 2);
    int32_t C_zero_pt = 5;

    // reference implementation
    // conv_ref expects weights to be in G (R S C/G) K/G
    int8_t* rightBData = Bint8.data();
    if (btrans == matrix_op_t::Transpose) {
      transposeConvWeights(conv_p, Bint8.data(), Bint8_tr.data());
      rightBData = Bint8_tr.data();
    }
    for (int g = 0; g < G; ++g) {
      col_offsets_with_zero_pt_s8acc32_ref(
          R * S * IC_per_G,
          OC_per_G,
          OC_per_G,
          rightBData + g * R * S * IC_per_G * OC_per_G,
          Bint8_zero_point.data() + g * OC_per_G / ncols_per_quant_group,
          col_offsets.data() + g * OC_per_G,
          ncols_per_quant_group);
    }
    conv_ref(
        conv_p, Aint8.data(), Aint8_zero_point, rightBData, Cint32_ref.data());

    for (int g = 0; g < G; ++g) {
      row_offsets_u8acc32_ref(
          MDim,
          KDim,
          KDim * G,
          Aint8_im2col.data() + g * KDim,
          row_offsets.data());

      requantize_u8acc32_ref(
          MDim,
          NDim,
          G * NDim,
          Cint32_ref.data() + g * NDim,
          Cint8_ref.data() + g * NDim,
          C_multiplier.data() + g * NDim / ncols_per_quant_group,
          C_zero_pt,
          Aint8_zero_point,
          Bint8_zero_point.data() + g * NDim / ncols_per_quant_group,
          row_offsets.data(),
          col_offsets.data() + g * NDim,
          nullptr,
          ncols_per_quant_group);
    }

    PackWeightMatrixForGConv<int8_t> packedWeights(
        btrans, conv_p, Bint8.data(), nullptr);

    // TODO: Uncomment once we support multiple threads in fbgemmGroupwiseConv
    // #ifdef _OPENMP
    // #pragma omp parallel
    // #endif
    {
      vector<int32_t> row_offset_buf(rowOffsetBufferSizeGConv(conv_p));

      DoNothing<> doNothingObj{};

      int num_threads = fbgemm_get_num_threads();
      int tid = fbgemm_get_thread_num();

      if (q_granularity == QuantizationGranularity::TENSOR) {
        ReQuantizeOutput<false, QuantizationGranularity::TENSOR> reqObj(
            doNothingObj,
            C_multiplier.data(),
            C_zero_pt,
            Aint8_zero_point,
            Bint8_zero_point.data(),
            Bint8_zero_point[0] ? row_offset_buf.data() : nullptr,
            col_offsets.data(),
            nullptr,
            G * NDim,
            G);

        fbgemmGroupwiseConv(
            conv_p,
            Aint8.data(),
            Aint8_zero_point,
            Bint8_zero_point[0] ? row_offset_buf.data() : nullptr,
            packedWeights,
            Cint8_fb.data(),
            Cint32_fb.data(),
            reqObj,
            tid,
            num_threads);
      } else if (q_granularity == QuantizationGranularity::GROUP) {
        ReQuantizeOutput<false, QuantizationGranularity::GROUP> reqObj(
            doNothingObj,
            C_multiplier.data(),
            C_zero_pt,
            Aint8_zero_point,
            Bint8_zero_point.data(),
            row_offset_buf.data(),
            col_offsets.data(),
            nullptr,
            G * NDim,
            G);

        fbgemmGroupwiseConv(
            conv_p,
            Aint8.data(),
            Aint8_zero_point,
            row_offset_buf.data(),
            packedWeights,
            Cint8_fb.data(),
            Cint32_fb.data(),
            reqObj,
            tid,
            num_threads);

      } else {
        ReQuantizeOutput<false, QuantizationGranularity::OUT_CHANNEL> reqObj(
            doNothingObj,
            C_multiplier.data(),
            C_zero_pt,
            Aint8_zero_point,
            Bint8_zero_point.data(),
            row_offset_buf.data(),
            col_offsets.data(),
            nullptr,
            G * NDim,
            G);

        fbgemmGroupwiseConv(
            conv_p,
            Aint8.data(),
            Aint8_zero_point,
            row_offset_buf.data(),
            packedWeights,
            Cint8_fb.data(),
            Cint32_fb.data(),
            reqObj,
            tid,
            num_threads);
      }
    } // omp parallel

    compare_validate_buffers(
        Cint8_ref.data(),
        Cint8_fb.data(),
        MDim,
        NDim * G,
        NDim * G,
        static_cast<uint8_t>(0));
  } // for each shape
}

/**
 * @brief Unit test for uint8 activations, int8 weights, and 32-bit
 * accumulation. Output processing: nothing
 */
TEST_P(fbgemmGConvAcc32Test, NoRequantizeTest) {
  vector<conv_param_t<>> shapes(GetShapes_());
  matrix_op_t atrans, btrans;
  tie(atrans, btrans) = GetParam();

  for (auto conv_p : shapes) {
    int R = conv_p.K[0];
    int S = conv_p.K[1];
    int G = conv_p.G;
    int OC = conv_p.OC;
    int OH = conv_p.OUT_DIM[0];
    int OW = conv_p.OUT_DIM[1];
    int IC_per_G = conv_p.IC / conv_p.G;
    int OC_per_G = conv_p.OC / conv_p.G;

    // activations
    aligned_vector<uint8_t> Aint8(
        conv_p.MB * conv_p.IN_DIM[0] * conv_p.IN_DIM[1] * conv_p.IC, 0);

    // weights
    // when btrans == Transpose, the weight matrix is in layout G K/G (R S C/G)
    // instead of G (R S C/G) K/G
    aligned_vector<int8_t> Bint8(R * S * conv_p.G * IC_per_G * OC_per_G, 0);
    aligned_vector<int8_t> Bint8_tr(R * S * conv_p.G * IC_per_G * OC_per_G, 0);

    aligned_vector<int32_t> Cint32_ref(conv_p.MB * OH * OW * OC, 0);
    aligned_vector<int32_t> Cint32_fb(Cint32_ref.size(), 0);

    randFill<uint8_t>(Aint8, 0, 5);
    int32_t Aint8_zero_point = 4;

    randFill<int8_t>(Bint8, -4, 4);

    // matrix dimensions after im2col for each GEMM.
    // For each group, there is one GEMM of the following dimensions
    int MDim = conv_p.MB * OH * OW;
    int NDim = OC_per_G;
    // int KDim = R * S * IC_per_G;

    // reference implementation
    // conv_ref expects weights to be in G (R S C/G) K/G
    int8_t* rightBData = Bint8.data();
    if (btrans == matrix_op_t::Transpose) {
      transposeConvWeights(conv_p, Bint8.data(), Bint8_tr.data());
      rightBData = Bint8_tr.data();
    }
    conv_ref(
        conv_p, Aint8.data(), Aint8_zero_point, rightBData, Cint32_ref.data());

    PackWeightMatrixForGConv<int8_t> packedWeights(
        btrans, conv_p, Bint8.data(), nullptr);

    // TODO: Uncomment once we support multiple threads in fbgemmGroupwiseConv
    // #ifdef _OPENMP
    // #pragma omp parallel
    // #endif
    {
      vector<int32_t> row_offset_buf(rowOffsetBufferSizeGConv(conv_p));

      DoNothing<int32_t, int32_t> doNothingObj{};

      int num_threads = fbgemm_get_num_threads();
      int tid = fbgemm_get_thread_num();

      fbgemmGroupwiseConv(
          conv_p,
          Aint8.data(),
          Aint8_zero_point,
          row_offset_buf.data(),
          packedWeights,
          Cint32_fb.data(),
          Cint32_fb.data(),
          doNothingObj,
          tid,
          num_threads);
    }

    compare_validate_buffers(
        Cint32_ref.data(),
        Cint32_fb.data(),
        MDim,
        NDim * G,
        NDim * G,
        static_cast<int32_t>(0));
  } // for each shape
}
