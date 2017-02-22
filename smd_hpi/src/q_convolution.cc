/*!
 * Copyright (c) 2016 by Contributors
 * \file q_fully_connected.cc
 * \brief Quantized CONV operator
 * \author HPI-DeepLearning
*/

#include "./q_convolution-inl.h"
#include <mshadow/base.h>
#include <mshadow/tensor.h>
#include <memory>
#include "./binary_layer.h"
#include "./xnor_cpu.h"
#if MXNET_USE_MKL2017 == 1
#include <mkl_memory.h>
#include "../../src/operator/mkl/mkl_memory-inl.h"
#include "../../src/operator/mkl/mkl_convolution-inl.h"
#endif  // MXNET_USE_MKL2017
#if MXNET_USE_NNPACK == 1
#include "../../src/operator/nnpack/nnpack_convolution-inl.h"
#endif  // MXNET_USE_NNPACK


namespace mshadow {

    inline void QConvolutionForward(const Tensor<cpu, 4, float> &data,
                                    const Tensor<cpu, 2, float> &wmat,
                                    const Tensor<cpu, 2, float> &in_col,
                                    const Tensor<cpu, 2, float> &temp_dst,
                                    const Tensor<cpu, 4, float> &out,
                                    const mxnet::op::QConvolutionParam &param) {

      CHECK_EQ(param.stride[0], 1) << "binary convolution currently only supported with stride==1";
      CHECK_EQ(param.stride[1], 1) << "binary convolution currently only supported with stride==1";

	  auto binary_layer = std::unique_ptr<mxnet::op::BinaryLayer>(
		  new mxnet::op::BinaryLayer(data.size(1), //   input depth
								  data.size(2), //    input x
								  data.size(3), //    input y
								  param.num_filter,// number filters
								  param.kernel[0], // weight x
								  param.kernel[1],//  weight y
								  param.pad[0],//     padding
								  param.pad[1],//     padding
          wmat.shape_[1], // m*n with m=num_filter
          wmat.shape_[2], // m*n with n=weight_x * weight_y * input depth
          //in_col.shape_[0], // n*k with n=weight_x * weight_y * input depth
          //in_col.shape_[1], // n*k with k=output_x * output_y * batch_size
          //temp_dst.shape_[1], // m*k  with m=num_filter
          temp_dst.shape_[2]));// m*k with k=output_x * output_y * batch_size

      binary_layer->set_input_as_col(in_col);
      binary_layer->set_weights(wmat); // disregard first dim, 'group'?

//      mxnet::op::xnor_cpu::xnor_forward(binary_layer);

      // data is now stored in binary_layer.input/weights/alpha/beta/output
      // and should be accessed with bitshifts, as in darknet

//      binary_layer->get_output(out[i]); //convert back binary output and copy into float for next layer

    }

    template<typename DType>
    inline void QConvolutionForward(const Tensor<cpu, 4, DType> &data,
                                    const Tensor<cpu, 2, DType> &wmat,
                                    const Tensor<cpu, 2, DType> &in_col,
                                    const Tensor<cpu, 2, DType> &temp_dst,
                                    const Tensor<cpu, 4, DType> &out,
                                    const mxnet::op::QConvolutionParam &param) {
      CHECK(false) << "only float supported";
    }
}

namespace mxnet {
namespace op {


DMLC_REGISTER_PARAMETER(QConvolutionParam);

template<>
Operator* CreateOp<cpu>(QConvolutionParam param, int dtype,
                        std::vector<TShape> *in_shape,
                        std::vector<TShape> *out_shape,
                        Context ctx) {
  Operator *op = NULL;
#if MXNET_USE_MKL2017 == 1
  if ((param.dilate[0] == 1 && param.dilate[1] == 1)
      && param.kernel.ndim() == 2) {
      LOG(FATAL) << "QConvolution not supported with MKL";
    switch (dtype) {
    case mshadow::kFloat32:
      return new MKLConvolutionOp<cpu, float>(param);
    case mshadow::kFloat64:
      return new MKLConvolutionOp<cpu, double>(param);
    default:
      break;
    }
  }
  LOG(INFO) << MKLConvolutionOp<cpu, float>::getName() << " Skip MKL optimization";
#endif
#if MXNET_USE_NNPACK == 1
  const size_t batch_size = (*in_shape)[0][0];
  if ((param.dilate[0] == 1 && param.dilate[1] == 1)
      && param.kernel.ndim() == 2 && (!param.no_bias)
      && param.num_group == 1 && (batch_size == 1 ||
      ((batch_size > 1) && (param.stride[0] == 1) &&
      (param.stride[1] == 1)))) {
      LOG(FATAL) << "QConvolution not supported with NNPACK";
    switch (dtype) {
    case mshadow::kFloat32:
      return new NNPACKConvolutionOp<cpu, float>(param);
    default:
      break;
    }
  }
#endif
  MSHADOW_REAL_TYPE_SWITCH(dtype, DType, {
    op = new QConvolutionOp<cpu, DType>(param);
  })
  return op;
}

// DO_BIND_DISPATCH comes from operator_common.h
Operator *QConvolutionProp::CreateOperatorEx(Context ctx,
                                            std::vector<TShape> *in_shape,
                                            std::vector<int> *in_type) const {
  std::vector<TShape> out_shape, aux_shape;
  std::vector<int> out_type, aux_type;
  CHECK(InferType(in_type, &out_type, &aux_type));
  CHECK(InferShape(in_shape, &out_shape, &aux_shape));
  DO_BIND_DISPATCH(CreateOp, param_, (*in_type)[0], in_shape, &out_shape, ctx);
}

MXNET_REGISTER_OP_PROPERTY(QConvolution, QConvolutionProp)
.add_argument("data", "Symbol", "Input data to the ConvolutionOp.")
.add_argument("weight", "Symbol", "Weight matrix.")
.add_argument("bias", "Symbol", "Bias parameter.")
.add_arguments(QConvolutionParam::__FIELDS__())
.describe("Apply convolution to input then add a bias.");

}  // namespace op
}  // namespace mxnet