#ifndef CAFFE_VISION_LAYERS_HPP_
#define CAFFE_VISION_LAYERS_HPP_

#include <string>
#include <utility>
#include <vector>

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/common_layers.hpp"
#include "caffe/data_layers.hpp"
#include "caffe/layer.hpp"
#include "caffe/loss_layers.hpp"
#include "caffe/neuron_layers.hpp"
#include "caffe/proto/caffe.pb.h"

#include "caffe/util/densecrf_pairwise.hpp"

namespace caffe {

/**
 * @brief Convolves the input image with a bank of learned filters,
 *        and (optionally) adds biases.
 *
 *   Caffe convolves by reduction to matrix multiplication. This achieves
 *   high-throughput and generality of input and filter dimensions but comes at
 *   the cost of memory for matrices. This makes use of efficiency in BLAS.
 *
 *   The input is "im2col" transformed to a channel K' x H x W data matrix
 *   for multiplication with the N x K' x H x W filter matrix to yield a
 *   N' x H x W output matrix that is then "col2im" restored. K' is the
 *   input channel * kernel height * kernel width dimension of the unrolled
 *   inputs so that the im2col matrix has a column for each input region to
 *   be filtered. col2im restores the output spatial structure by rolling up
 *   the output channel N' columns of the output matrix.
 */
template <typename Dtype>
class ConvolutionLayer : public Layer<Dtype> {
 public:
  /**
   * @param param provides ConvolutionParameter convolution_param,
   *    with ConvolutionLayer options:
   *  - num_output. The number of filters.
   *  - kernel_size / kernel_h / kernel_w. The filter dimensions, given by
   *  kernel_size for square filters or kernel_h and kernel_w for rectangular
   *  filters.
   *  - stride / stride_h / stride_w (\b optional, default 1). The filter
   *  stride, given by stride_size for equal dimensions or stride_h and stride_w
   *  for different strides. By default the convolution is dense with stride 1.
   *  - pad / pad_h / pad_w (\b optional, default 0). The zero-padding for
   *  convolution, given by pad for equal dimensions or pad_h and pad_w for
   *  different padding. Input padding is computed implicitly instead of
   *  actually padding.
   *  - hole / hole_h / hole_w (\b optional, default 1). Stride
   *  between consecutive filter taps before performing convolution.
   *  This is aka atrous algorithm in the context of undecimated wavelet transform.
   *  - group (\b optional, default 1). The number of filter groups. Group
   *  convolution is a method for reducing parameterization by selectively
   *  connecting input and output channels. The input and output channel dimensions must be divisible
   *  by the number of groups. For group @f$ \geq 1 @f$, the
   *  convolutional filters' input and output channels are separated s.t. each
   *  group takes 1 / group of the input channels and makes 1 / group of the
   *  output channels. Concretely 4 input channels, 8 output channels, and
   *  2 groups separate input channels 1-2 and output channels 1-4 into the
   *  first group and input channels 3-4 and output channels 5-8 into the second
   *  group.
   *  - bias_term (\b optional, default true). Whether to have a bias.
   *  - engine: convolution has CAFFE (matrix multiplication) and CUDNN (library
   *    kernels + stream parallelism) engines.
   */
  explicit ConvolutionLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_CONVOLUTION;
  }
  virtual inline int MinBottomBlobs() const { return 1; }
  virtual inline int MinTopBlobs() const { return 1; }
  virtual inline bool EqualNumBottomTopBlobs() const { return true; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  int kernel_h_, kernel_w_;
  int stride_h_, stride_w_;
  int num_;
  int channels_;
  int pad_h_, pad_w_;
  int hole_h_, hole_w_;
  int height_, width_;
  int group_;
  int num_output_;
  int height_out_, width_out_;
  bool bias_term_;
  bool is_1x1_;

  /// M_ is the channel dimension of the output for a single group, which is the
  /// leading dimension of the filter matrix.
  int M_;
  /// K_ is the dimension of an unrolled input for a single group, which is the
  /// leading dimension of the data matrix.
  int K_;
  /// N_ is the spatial dimension of the output, the H x W, which are the last
  /// dimensions of the data and filter matrices.
  int N_;
  Blob<Dtype> col_buffer_;
  Blob<Dtype> bias_multiplier_;
};

#ifdef USE_CUDNN
/*
 * @brief cuDNN implementation of ConvolutionLayer.
 *        Fallback to ConvolutionLayer for CPU mode.
 *
 * cuDNN accelerates convolution through forward kernels for filtering and bias
 * plus backward kernels for the gradient w.r.t. the filters, biases, and
 * inputs. Caffe + cuDNN further speeds up the computation through forward
 * parallelism across groups and backward parallelism across gradients.
 *
 * The CUDNN engine does not have memory overhead for matrix buffers. For many
 * input and filter regimes the CUDNN engine is faster than the CAFFE engine,
 * but for fully-convolutional models and large inputs the CAFFE engine can be
 * faster as long as it fits in memory.
*/
template <typename Dtype>
class CuDNNConvolutionLayer : public ConvolutionLayer<Dtype> {
 public:
  explicit CuDNNConvolutionLayer(const LayerParameter& param)
      : ConvolutionLayer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual ~CuDNNConvolutionLayer();

 protected:
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  cudnnHandle_t* handle_;
  cudaStream_t*  stream_;
  vector<cudnnTensor4dDescriptor_t> bottom_descs_, top_descs_;
  cudnnTensor4dDescriptor_t    bias_desc_;
  cudnnFilterDescriptor_t      filter_desc_;
  vector<cudnnConvolutionDescriptor_t> conv_descs_;
  int bottom_offset_, top_offset_, weight_offset_, bias_offset_;
};
#endif

/**
 * @brief A helper for image operations that rearranges image regions into
 *        column vectors.  Used by ConvolutionLayer to perform convolution
 *        by matrix multiplication.
 *
 * TODO(dox): thorough documentation for Forward, Backward, and proto params.
 */
template <typename Dtype>
class Im2colLayer : public Layer<Dtype> {
 public:
  explicit Im2colLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_IM2COL;
  }
  virtual inline int ExactNumBottomBlobs() const { return 1; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  int kernel_h_, kernel_w_;
  int stride_h_, stride_w_;
  int channels_;
  int height_, width_;
  int pad_h_, pad_w_;
  int hole_h_, hole_w_;
};

/**
 * @brief Changes the spatial resolution by bi-linear interpolation.
 *        The target size is specified in terms of pixels. 
 *        The start and end pixels of the input are mapped to the start
 *        and end pixels of the output.
 */
template <typename Dtype>
class InterpLayer : public Layer<Dtype> {
 public:
  explicit InterpLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_INTERP;
  }
  virtual inline int ExactNumBottomBlobs() const { return 1; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  
  int num_, channels_;
  int height_in_, width_in_;
  int height_out_, width_out_;
  int pad_beg_, pad_end_;
  int height_in_eff_, width_in_eff_;
};

/**
 * @brief Adds bg_bias to the scores of the background channel and
 * fg_bias to the scores of each of the foreground channels
 */
template <typename Dtype>
class BiasChannelLayer : public Layer<Dtype> {
 public:
  explicit BiasChannelLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_BIAS_CHANNEL;
  }
  virtual inline int ExactNumBottomBlobs() const { return 2; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  
  int num_, channels_, height_, width_;
  int max_labels_;
  Dtype bg_bias_, fg_bias_;
  // set of ignore labels
  std::set<int> ignore_label_;
};

/**
 * @brief Counts the number of occurrences of a given value across
   spatial positions.
   Ignores values in the ignore_label list.
   It assumes that the input is integers in the [0 num_labels) range.
   Input:  [num 1 height width]
   Output: [num num_labels 1 1]
 */
template <typename Dtype>
class HistogramLayer : public Layer<Dtype> {
 public:
  explicit HistogramLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_HISTOGRAM;
  }
  virtual inline int ExactNumBottomBlobs() const { return 1; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
     const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {}
  
  int num_, height_, width_;
  int num_labels_;
  // set of ignore labels
  std::set<int> ignore_label_;
};

/**
   @brief The output equals to bottom[1] except for the positions in bottom[0]
   where the value of bottom[0] equals to ignore_label
 */
template <typename Dtype>
class IgnoreOverlayLayer : public Layer<Dtype> {
 public:
  explicit IgnoreOverlayLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_IGNORE_OVERLAY;
  }
  virtual inline int ExactNumBottomBlobs() const { return 2; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {}
  
  int num_, channels_, height_, width_;
  int ignore_label_;
};

/**
 * @brief Aggregates the scores (log-probabilities) from a larger label
 * space to a smaller label space via a softmax operation.
 * The mapping between the two label spaces is provided via a text file.
 */
template <typename Dtype>
class ChannelAggregatorLayer : public Layer<Dtype> {
 public:
  explicit ChannelAggregatorLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_CHANNEL_AGGREGATOR;
  }
  virtual inline int ExactNumBottomBlobs() const { return 1; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  
  int num_, channels_in_, height_, width_;
  int channels_out_;
  std::vector<std::vector<int> > label_map_;
};

/**
 * @brief Adds to the input scores (specified in bottom[0]) adaptive biases in the
 * channels (listed in bottom[1]) so as they win on a target pre-defined portion
 * of the image.
 *
 * The result is only approximate (i.e., the target portions are not reached exactly).
 * In particular, the algorithm has an inner loop over the labels and at each iteration
 * ensures that the particular label wins in at least a portion_ fraction of the pixels.
 * When moving to the next labels we may lose some of the pixels that were claimed before.
 * We have an outer loop of num_iter_ iterations in which we repeat the process.
 * 
 * We always visit the background score first. To alleviate the effect of visit order
 * dependence, we iterate over the foreground classes which are present in random order.
 *
 * If suppress_others_ is true, then we make sure that the labels not listed in bottom[1]
 * score lower throughout the image in comparison to both the background and the existing
 * foreground labels.
 */
template <typename Dtype>
class AdaptiveBiasChannelLayer : public Layer<Dtype> {
 public:
  explicit AdaptiveBiasChannelLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_ADAPTIVE_BIAS_CHANNEL;
  }
  virtual inline int ExactNumBottomBlobs() const { return 2; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
 
  int num_, channels_, height_, width_;
  int max_labels_;

  // configurable params
  int num_iter_;
  Dtype bg_portion_, fg_portion_;
  bool suppress_others_;
  Dtype margin_others_;
};

/**
 * @brief Censors (i.e., sets to "ignore label") the labels not
 * included in the input weak label set.
 * INPUTS:
 * 0: (num, 1, height, width)
 * 1: (num, max_labels, 1, 1)
 */
template <typename Dtype>
class CensorLabelLayer : public Layer<Dtype> {
 public:
  explicit CensorLabelLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_CENSOR_LABEL;
  }
  virtual inline int ExactNumBottomBlobs() const { return 2; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  
  int num_, channels_, height_, width_;
  int max_labels_;
  int ignore_label_;
};

/**
 * @brief Finds the unique labels in (num, 1, height, width) input discarding
 * the positions, resulting into a (num, max_labels, 1, 1) summary output
 */
template <typename Dtype>
class UniqueLabelLayer : public Layer<Dtype> {
 public:
  explicit UniqueLabelLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_UNIQUE_LABEL;
  }
  virtual inline int ExactNumBottomBlobs() const { return 1; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  
  int num_, channels_, height_, width_;
  int max_labels_;
  // set of ignore labels
  std::set<Dtype> ignore_label_;
  // set of forced labels
  std::set<Dtype> force_label_;
};

/**
 * @brief Pads (if pad >= 0) or crops (if pad < 0) parts of the input.
 */
template <typename Dtype>
class PaddingLayer : public Layer<Dtype> {
 public:
  explicit PaddingLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_PADDING;
  }
  virtual inline int ExactNumBottomBlobs() const { return 1; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  int pad_beg_, pad_end_;
  bool pad_pos_;
  int num_, channels_;
  int height_in_, width_in_;
  int height_out_, width_out_;
};

/*
  MatReadLayer
*/
template <typename Dtype>
class MatReadLayer : public Layer<Dtype> {
 public:
  explicit MatReadLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_MAT_READ;
  }
  virtual inline int ExactNumBottomBlobs() const { return 0; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  bool reshape_;
  int batch_size_;
  int iter_;
  string prefix_;
  vector<string> fnames_;
};

/*
  MatWriteLayer
*/
template <typename Dtype>
class MatWriteLayer : public Layer<Dtype> {
 public:
  explicit MatWriteLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_MAT_WRITE;
  }
  virtual inline int ExactNumTopBlobs() const { return 0; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  int iter_;
  int period_;
  string prefix_;
  vector<string> fnames_;
};

// Forward declare PoolingLayer and SplitLayer for use in LRNLayer.
template <typename Dtype> class PoolingLayer;
template <typename Dtype> class SplitLayer;

/**
 * @brief Normalize the input in a local region across or within feature maps.
 *
 * TODO(dox): thorough documentation for Forward, Backward, and proto params.
 */
template <typename Dtype>
class LRNLayer : public Layer<Dtype> {
 public:
  explicit LRNLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_LRN;
  }
  virtual inline int ExactNumBottomBlobs() const { return 1; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  virtual void CrossChannelForward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void CrossChannelForward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void WithinChannelForward(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void CrossChannelBackward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  virtual void CrossChannelBackward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  virtual void WithinChannelBackward(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  int size_;
  int pre_pad_;
  Dtype alpha_;
  Dtype beta_;
  Dtype k_;
  int num_;
  int channels_;
  int height_;
  int width_;

  // Fields used for normalization ACROSS_CHANNELS
  // scale_ stores the intermediate summing results
  Blob<Dtype> scale_;

  // Fields used for normalization WITHIN_CHANNEL
  shared_ptr<SplitLayer<Dtype> > split_layer_;
  vector<Blob<Dtype>*> split_top_vec_;
  shared_ptr<PowerLayer<Dtype> > square_layer_;
  Blob<Dtype> square_input_;
  Blob<Dtype> square_output_;
  vector<Blob<Dtype>*> square_bottom_vec_;
  vector<Blob<Dtype>*> square_top_vec_;
  shared_ptr<PoolingLayer<Dtype> > pool_layer_;
  Blob<Dtype> pool_output_;
  vector<Blob<Dtype>*> pool_top_vec_;
  shared_ptr<PowerLayer<Dtype> > power_layer_;
  Blob<Dtype> power_output_;
  vector<Blob<Dtype>*> power_top_vec_;
  shared_ptr<EltwiseLayer<Dtype> > product_layer_;
  Blob<Dtype> product_input_;
  vector<Blob<Dtype>*> product_bottom_vec_;
};


/**
 * @brief Pools the input image by taking the max, average, etc. within regions.
 *
 * TODO(dox): thorough documentation for Forward, Backward, and proto params.
 */
template <typename Dtype>
class PoolingLayer : public Layer<Dtype> {
 public:
  explicit PoolingLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_POOLING;
  }
  virtual inline int ExactNumBottomBlobs() const { return 1; }
  virtual inline int MinTopBlobs() const { return 1; }
  // MAX POOL layers can output an extra top blob for the mask;
  // others can only output the pooled inputs.
  virtual inline int MaxTopBlobs() const {
    return (this->layer_param_.pooling_param().pool() ==
            PoolingParameter_PoolMethod_MAX) ? 2 : 1;
  }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  int kernel_h_, kernel_w_;
  int stride_h_, stride_w_;
  int pad_h_, pad_w_;
  int channels_;
  int height_, width_;
  int pooled_height_, pooled_width_;
  bool global_pooling_;
  Blob<Dtype> rand_idx_;
  Blob<int> max_idx_;
};

#ifdef USE_CUDNN
/*
 * @brief cuDNN implementation of PoolingLayer.
 *        Fallback to PoolingLayer for CPU mode.
*/
template <typename Dtype>
class CuDNNPoolingLayer : public PoolingLayer<Dtype> {
 public:
  explicit CuDNNPoolingLayer(const LayerParameter& param)
      : PoolingLayer<Dtype>(param) {}
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual ~CuDNNPoolingLayer();
  // Currently, cuDNN does not support the extra top blob.
  virtual inline int MinTopBlobs() const { return -1; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  cudnnHandle_t             handle_;
  cudnnTensor4dDescriptor_t bottom_desc_, top_desc_;
  cudnnPoolingDescriptor_t  pooling_desc_;
  cudnnPoolingMode_t        mode_;
};
#endif

/**
 * jay add
 * @brief DenseCRF
 *        
 *
 */
template <typename Dtype>
class DenseCRFLayer : public Layer<Dtype> {
 public:
  explicit DenseCRFLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual ~DenseCRFLayer();

  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline LayerParameter_LayerType type() const {
    return LayerParameter_LayerType_DENSE_CRF;
  }
  // will take DCNN output, image (optional) and image_dim as input
  virtual inline int MinBottomBlobs() const { return 2; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);
  // TODOs
  //  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
  //      const vector<Blob<Dtype>*>& top);
  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
  //  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
  //      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);

  virtual void SetupPairwiseFunctions(const vector<Blob<Dtype>*>& bottom);
  virtual void ClearPairwiseFunctions();

  virtual void SetupUnaryEnergy(const Dtype* bottom);

  virtual void ComputeMap(Dtype* top_inf);

  virtual void RunInference();
  virtual void StartInference();
  virtual void StepInference();

  virtual void ExpAndNormalize(float* out, const float* in, float scale);

  virtual void AllocateAllData();
  virtual void DeAllocateAllData();

  
  // TODO: compute the energy = unary + pairwise
  //virtual void ComputeUnaryEnergy();
  //virtual void ComputePairwiseEnergy();

  bool has_image;

  int num_;
  int pad_height_;   // may have padded rows
  int pad_width_;    // may have padded cols

  int M_;   // number of input feature (channel)
  int W_;   // effective width   (<= pad_width_)
  int H_;   // effective height  (<= pad_height_)
  int N_;   // = W_ * H_

  int max_iter_;

  // Gaussian pairwise potential with weight and positional standard deviation
  std::vector<float> pos_w_;
  std::vector<float> pos_xy_std_;
  
  // Bilateral pairwise potential with weight, positional std, and color std
  std::vector<float> bi_w_;
  std::vector<float> bi_xy_std_;
  std::vector<float> bi_rgb_std_;

  std::vector<PairwisePotential*> pairwise_;

  int unary_element_;  // size of unary energy
  int map_element_;    // size of map result

  float* unary_;     // unary energy
  float* current_;   // current inference values, will copy to top[0]
  float* next_;      // next inference values
  float* tmp_;       // buffer

  /// sum_multiplier is used to carry out sum using BLAS
  Blob<Dtype> sum_multiplier_;
  /// scale is an intermediate Blob to hold temporary results.
  Blob<Dtype> scale_;
  /// norm_data is an intermediate Blob to hold temporary results.
  Blob<Dtype> norm_data_;

  // the output format is probability or score (score = log(probability))
  bool output_prob_;
};



}  // namespace caffe

#endif  // CAFFE_VISION_LAYERS_HPP_
