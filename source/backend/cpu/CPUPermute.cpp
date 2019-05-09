//
//  CPUPermute.cpp
//  MNN
//
//  Created by MNN on 2018/07/18.
//  Copyright © 2018, Alibaba Group Holding Limited
//

#include "CPUPermute.hpp"
#include "CPUBackend.hpp"
#include "CommonOptFunction.h"
#include "Macro.h"
#include "TensorUtils.hpp"

namespace MNN {

CPUPermute::CPUPermute(Backend *b, const MNN::Op *op) : MNN::Execution(b) {
    auto shape = op->main_as_Permute()->dims();
    for (int i = 0; i < shape->size(); ++i) {
        mDims.push_back(shape->data()[i]);
    }
}

ErrorCode CPUPermute::onResize(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs) {

    return NO_ERROR;
}

ErrorCode CPUPermute::onExecute(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs) {
    MNN_ASSERT(1 == inputs.size());
    MNN_ASSERT(1 == outputs.size());

    auto &input  = inputs[0]->buffer();
    auto &output = outputs[0]->buffer();

    // We can not permute batch axis
    MNN_ASSERT(mDims[0] == 0);
    
    // Currently don't support batch reshape, but support multi batch
    MNN_ASSERT(output.dim[0].extent == input.dim[0].extent);
    MNN_ASSERT(output.dimensions == input.dimensions);
    MNN_ASSERT(2 <= output.dimensions && output.dimensions <= 4); // 2 <= tensor dim <= 4

    int areaInput  = 1;
    int areaOutput = 1;
    for (int i = 2; i < input.dimensions; ++i) {
        areaInput *= input.dim[i].extent;
        areaOutput *= output.dim[i].extent;
    }
    int inputBatchSize  = ALIGN_UP4(input.dim[1].extent) * areaInput;
    int outputBatchSize = ALIGN_UP4(output.dim[1].extent) * areaOutput;

    auto originInput  = (const float *)input.host;
    auto originOutput = (float *)output.host;
    
    {
        bool noChange = true;
        for (int i = 0; i < (int)mDims.size(); ++i) {
            if (mDims[i] != i) {
                noChange = false;
                break;
            }
        }
        // mDims[i] == i, no change at all.
        if (noChange) {
            ::memcpy(originOutput, originInput, inputs[0]->size());
            return NO_ERROR;
        }
    }
    
    int inputHeight   = 1;
    if (input.dimensions > 2) {
        inputHeight = input.dim[2].extent;
    }
    int inputWidth    = 1;
    if (input.dimensions > 3) {
        inputWidth = input.dim[3].extent;
    }
    int inputRealArea = inputWidth * inputHeight;
    const int inputStrides[4] = {inputBatchSize, inputRealArea * 4, inputWidth * 4, 4}; // original input stride of N, C4, H and W
    int outputHeight  = 1;
    if (output.dimensions > 2) {
        outputHeight = output.dim[2].extent;
    }
    int outputWidth   = 1;
    if (output.dimensions > 3) {
        outputWidth = output.dim[3].extent;
    }
    const int outputChannelAlign4  = ALIGN_UP4(output.dim[1].extent);
    
    int strides[4][4];  // map from change of output index to change of input index on N, C4, H and W
    
    for (int i = 0; i < 4; ++i) {
        // maybe input tensor dim < 4. In this case, we process it as its shape == 4
        int dim = i;
        if (i < (int)mDims.size()) {
            dim = mDims[i];
        }
        int temp = inputStrides[dim];
        if (dim == 1) {
            strides[i][0] = strides[i][1] = strides[i][2] = 1;
            strides[i][3] = temp - 3;
        } else {
            strides[i][0] = strides[i][1] = strides[i][2] = strides[i][3] = temp;
        }
    }
    int ocTotalStride = strides[1][0] + strides[1][1] + strides[1][2] + strides[1][3];
    // compute prefix sum of output 0 dim stride to avoid frequent assignment of variables in the deepest loops
    for (int i = 1; i < 4; ++i) {
        strides[1][i] += strides[1][i - 1];
    }
    
    for (int b = 0; b < input.dim[0].extent; ++b) {
        auto inputCurrent  = originInput + inputBatchSize * b;
        auto outputCurrent = originOutput + outputBatchSize * b;
        
        for (int oz = 0, outputIndex = 0, inputIndex = 0; oz < outputChannelAlign4; oz += 4) {
            const int inputIndex1 = inputIndex;
            for (int oy = 0; oy < outputHeight; ++oy) {
                const int inputIndex2 = inputIndex;
                for (int ox = 0; ox < outputWidth; ++ox) {
                    outputCurrent[outputIndex++] = inputCurrent[inputIndex];
                    outputCurrent[outputIndex++] = inputCurrent[inputIndex + strides[1][0]];
                    outputCurrent[outputIndex++] = inputCurrent[inputIndex + strides[1][1]];
                    outputCurrent[outputIndex++] = inputCurrent[inputIndex + strides[1][2]];
                    inputIndex += strides[3][ox % 4];
                }
                inputIndex = inputIndex2 + strides[2][oy % 4];
            }
            inputIndex = inputIndex1 + ocTotalStride;
        }
        
    }

    return NO_ERROR;
}

class CPUPermuteCreator : public CPUBackend::Creator {
public:
    virtual Execution *onCreate(const std::vector<Tensor *> &inputs, const std::vector<Tensor *> &outputs,
                                const MNN::Op *op, Backend *backend) const override {
        return new CPUPermute(backend, op);
    }
};

REGISTER_CPU_OP_CREATOR(CPUPermuteCreator, OpType_Permute);
} // namespace MNN
