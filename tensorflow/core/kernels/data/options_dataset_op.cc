/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "tensorflow/core/kernels/data/options_dataset_op.h"

#include "absl/memory/memory.h"
#include "tensorflow/core/data/name_utils.h"
#include "tensorflow/core/framework/dataset.h"
#include "tensorflow/core/framework/dataset_options.pb.h"
#include "tensorflow/core/framework/partial_tensor_shape.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/profiler/lib/traceme.h"

namespace tensorflow {
namespace data {

/* static */ constexpr const char* const OptionsDatasetOp::kDatasetType;
/* static */ constexpr const char* const OptionsDatasetOp::kInputDataset;
/* static */ constexpr const char* const OptionsDatasetOp::kOutputTypes;
/* static */ constexpr const char* const OptionsDatasetOp::kOutputShapes;
/* static */ constexpr const char* const OptionsDatasetOp::kSerializedOptions;
/* static */ constexpr const char* const OptionsDatasetOp::kAddressList;
/* static */ constexpr const char* const OptionsDatasetOp::kThreadList;
/* static */ constexpr const char* const OptionsDatasetOp::kTargetThreadpoolSize;

class OptionsDatasetOp::Dataset : public DatasetBase {
 public:
  Dataset(OpKernelContext* ctx, const DatasetBase* input,
          const string& serialized_options,
          const std::vector<tstring>& address_list,
          const std::vector<int>& thread_list,
          const int &target_threadpool_size)
      : DatasetBase(DatasetContext(ctx)),
        input_(input),
        serialized_options_(serialized_options),
        address_list_(address_list),
        thread_list_(thread_list),
        target_threadpool_size_(target_threadpool_size) {
    input_->Ref();
    Options options;
    OP_REQUIRES(ctx, options.ParseFromString(serialized_options),
                errors::InvalidArgument(absl::StrCat(
                    "Could not parse ", OptionsDatasetOp::kSerializedOptions,
                    " as valid Options.")));
    if (target_threadpool_size > 0) {
      options.mutable_threading_options()->set_private_threadpool_size(target_threadpool_size);
    }
    set_options(options);
  }

  ~Dataset() override { input_->Unref(); }

  std::unique_ptr<IteratorBase> MakeIteratorInternal(
      const string& prefix) const override {
    DCHECK(false) << "OptionsDatasetOp::Dataset::MakeIteratorInternal is not "
                     "expected to be called because it is supposed to forward "
                     "the iterator to its input dataset(s).";
    LOG(ERROR) << "Datasets of type " << type_string()
               << " forwards its iterator to its input dataset. "
                  "`MakeIteratorInternal` is not implemented.";
    return nullptr;
  }

  const DataTypeVector& output_dtypes() const override {
    return input_->output_dtypes();
  }
  const std::vector<PartialTensorShape>& output_shapes() const override {
    return input_->output_shapes();
  }

  int64_t Cardinality() const override { return input_->Cardinality(); }

  string DebugString() const override {
    return name_utils::DatasetDebugString(kDatasetType);
  }

  Status InputDatasets(std::vector<const DatasetBase*>* inputs) const override {
    inputs->push_back(input_);
    return Status::OK();
  }

  Status CheckExternalState() const override {
    return input_->CheckExternalState();
  }

 protected:
  Status AsGraphDefInternal(SerializationContext* ctx,
                            DatasetGraphDefBuilder* b,
                            Node** output) const override {
    Node* input_graph_node = nullptr;
    TF_RETURN_IF_ERROR(b->AddInputDataset(ctx, input_, &input_graph_node));
    AttrValue serialized_options_attr;
    b->BuildAttrValue(serialized_options_, &serialized_options_attr);
    AttrValue address_list_attr;
    b->BuildAttrValue(address_list_, &address_list_attr);
    AttrValue thread_list_attr;
    b->BuildAttrValue(thread_list_, &thread_list_attr);
    AttrValue target_threadpool_size_attr;
    b->BuildAttrValue(target_threadpool_size_, &target_threadpool_size_attr);

    TF_RETURN_IF_ERROR(b->AddDataset(
        this, {input_graph_node},
        {std::make_pair(kSerializedOptions, serialized_options_attr), 
         std::make_pair(kAddressList, address_list_attr),
         std::make_pair(kThreadList, thread_list_attr),
         std::make_pair(kTargetThreadpoolSize, target_threadpool_size_attr)}, output));
    return Status::OK();
  }

 private:
  const DatasetBase* input_;
  const tstring serialized_options_;
  const std::vector<tstring> address_list_;
  const std::vector<int> thread_list_;
  const int target_threadpool_size_;
};

void OptionsDatasetOp::MakeDataset(OpKernelContext* ctx, DatasetBase** output) {
  DatasetBase* input;
  OP_REQUIRES_OK(ctx, GetDatasetFromVariantTensor(ctx->input(0), &input));

  OP_REQUIRES(
      ctx, address_list_.size() == thread_list_.size(),
      errors::InvalidArgument("address_list length must equal with thread_list length"));

  *output = new Dataset(ctx, input, serialized_options_, address_list_, thread_list_, target_threadpool_size_);
}

OptionsDatasetOp::OptionsDatasetOp(OpKernelConstruction* ctx)
    : DatasetOpKernel(ctx) {
  OP_REQUIRES_OK(ctx, ctx->GetAttr(kSerializedOptions, &serialized_options_));
  OP_REQUIRES_OK(ctx, ctx->GetAttr(kAddressList, &address_list_));
  OP_REQUIRES_OK(ctx, ctx->GetAttr(kThreadList, &thread_list_));
  OP_REQUIRES_OK(ctx, ctx->GetAttr(kTargetThreadpoolSize, &target_threadpool_size_));
}

namespace {
REGISTER_KERNEL_BUILDER(Name("OptionsDataset").Device(DEVICE_CPU).Priority(2),
                        OptionsDatasetOp);
REGISTER_KERNEL_BUILDER(Name("OptionsDataset")
                            .Device(DEVICE_GPU)
                            .HostMemory("input_dataset")
                            .HostMemory("handle")
                            .Priority(1),
                        OptionsDatasetOp);
}  // namespace
}  // namespace data
}  // namespace tensorflow
