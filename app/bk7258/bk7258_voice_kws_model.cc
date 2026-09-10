/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_voice_kws_model.h"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <new>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

struct bkvoice_kws_model_s
{
  tflite::MicroMutableOpResolver<6> resolver;
  tflite::MicroInterpreter *interpreter = nullptr;
  TfLiteTensor *input = nullptr;
  TfLiteTensor *output = nullptr;
};

static bool bkvoice_kws_quantized(const TfLiteTensor *tensor)
{
  return tensor != nullptr && tensor->type == kTfLiteInt8 &&
         tensor->data.int8 != nullptr &&
         std::isfinite(tensor->params.scale) && tensor->params.scale > 0.0f &&
         tensor->params.zero_point >= -128 && tensor->params.zero_point <= 127;
}

int bkvoice_kws_model_open(const struct bkvoice_kws_model_spec_s *spec,
                           void *arena, size_t arena_bytes,
                           struct bkvoice_kws_model_s **model)
{
  static const char *const labels[] = {"silence", "unknown", BKVOICE_KWS_LABEL};
  const tflite::Model *flatmodel;
  struct bkvoice_kws_model_s *instance;

  if (model == nullptr)
    {
      return -EINVAL;
    }

  *model = nullptr;
  if (spec == nullptr || spec->data == nullptr || spec->bytes < 8 ||
      spec->bytes > BKVOICE_KWS_MODEL_MAX_BYTES || arena == nullptr ||
      arena_bytes < 16 || reinterpret_cast<uintptr_t>(arena) % 16 != 0 ||
      spec->frontend == nullptr ||
      std::strcmp(spec->frontend, BKVOICE_KWS_FRONTEND_ID) != 0)
    {
      return -EINVAL;
    }

  for (unsigned int i = 0; i < BKVOICE_KWS_CLASSES; i++)
    {
      if (spec->labels[i] == nullptr || std::strcmp(spec->labels[i], labels[i]))
        {
          return -EINVAL;
        }
    }

  flatbuffers::Verifier verifier(spec->data, spec->bytes);
  if (!tflite::VerifyModelBuffer(verifier))
    {
      return -EBADMSG;
    }

  flatmodel = tflite::GetModel(spec->data);
  if (flatmodel->version() != TFLITE_SCHEMA_VERSION ||
      flatmodel->subgraphs() == nullptr || flatmodel->subgraphs()->size() != 1)
    {
      return -ENOTSUP;
    }

  const auto *graph = flatmodel->subgraphs()->Get(0);
  if (graph->inputs() == nullptr || graph->inputs()->size() != 1 ||
      graph->outputs() == nullptr || graph->outputs()->size() != 1)
    {
      return -ENOTSUP;
    }

  instance = new (std::nothrow) bkvoice_kws_model_s;
  if (instance == nullptr)
    {
      return -ENOMEM;
    }

  auto &resolver = instance->resolver;
  if (resolver.AddConv2D(tflite::Register_CONV_2D_INT8()) != kTfLiteOk ||
      resolver.AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8()) != kTfLiteOk ||
      resolver.AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8()) != kTfLiteOk ||
      resolver.AddReshape() != kTfLiteOk ||
      resolver.AddFullyConnected(tflite::Register_FULLY_CONNECTED_INT8()) != kTfLiteOk ||
      resolver.AddSoftmax(tflite::Register_SOFTMAX_INT8()) != kTfLiteOk)
    {
      delete instance;
      return -ENOTSUP;
    }

  instance->interpreter = new (std::nothrow) tflite::MicroInterpreter(
    flatmodel, resolver, static_cast<uint8_t *>(arena), arena_bytes);
  if (instance->interpreter == nullptr)
    {
      delete instance;
      return -ENOMEM;
    }

  if (instance->interpreter->AllocateTensors() != kTfLiteOk)
    {
      bkvoice_kws_model_close(instance);
      return -ENOTSUP;
    }

  instance->input = instance->interpreter->input(0);
  instance->output = instance->interpreter->output(0);
  auto *input = instance->input;
  auto *output = instance->output;
  if (!bkvoice_kws_quantized(input) || !bkvoice_kws_quantized(output) ||
      input->dims == nullptr || input->dims->size != 4 ||
      input->dims->data[0] != 1 || input->dims->data[1] != BKVOICE_KWS_ROWS ||
      input->dims->data[2] != BKVOICE_KWS_BINS || input->dims->data[3] != 1 ||
      input->bytes != BKVOICE_KWS_FEATURES || output->dims == nullptr ||
      output->dims->size != 2 || output->dims->data[0] != 1 ||
      output->dims->data[1] != BKVOICE_KWS_CLASSES ||
      output->bytes != BKVOICE_KWS_CLASSES ||
      output->params.zero_point != -128 || output->params.scale != 1.0f / 256)
    {
      bkvoice_kws_model_close(instance);
      return -EPROTO;
    }

  *model = instance;
  return 0;
}

int bkvoice_kws_model_infer(void *context, const float *features,
                           float scores[BKVOICE_KWS_CLASSES])
{
  auto *model = static_cast<bkvoice_kws_model_s *>(context);
  if (model == nullptr || features == nullptr || scores == nullptr)
    {
      return -EINVAL;
    }

  for (unsigned int i = 0; i < BKVOICE_KWS_FEATURES; i++)
    {
      if (!std::isfinite(features[i]))
        {
          return -EINVAL;
        }

      float value = features[i] / model->input->params.scale;
      value = std::round(value) + model->input->params.zero_point;
      value = value < -128.0f ? -128.0f : (value > 127.0f ? 127.0f : value);
      model->input->data.int8[i] = static_cast<int8_t>(value);
    }

  if (model->interpreter->Invoke() != kTfLiteOk)
    {
      return -EIO;
    }

  for (unsigned int i = 0; i < BKVOICE_KWS_CLASSES; i++)
    {
      scores[i] = (model->output->data.int8[i] -
                   model->output->params.zero_point) * model->output->params.scale;
    }

  return 0;
}

size_t bkvoice_kws_model_arena_used(const struct bkvoice_kws_model_s *model)
{
  return model == nullptr ? 0 : model->interpreter->arena_used_bytes();
}

void bkvoice_kws_model_close(struct bkvoice_kws_model_s *model)
{
  if (model != nullptr)
    {
      delete model->interpreter;
      delete model;
    }
}
