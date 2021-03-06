/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "paddle/operators/math/detail/hl_activation_functions.h"
#include "paddle/platform/hostdevice.h"

#include <type_traits>

namespace paddle {
namespace operators {
namespace math {
namespace detail {

namespace forward {

template <typename T>
DEVICE inline T sigmoid(const T a) {
  const T min = SIGMOID_THRESHOLD_MIN;
  const T max = SIGMOID_THRESHOLD_MAX;
  T tmp = (a < min) ? min : ((a > max) ? max : a);
  return static_cast<T>(1.0) / (static_cast<T>(1.0) + exp(-tmp));
}

template <typename T>
DEVICE inline T tanh(const T a) {
  T tmp = -2.0 * a;
  tmp = (tmp > EXP_MAX_INPUT) ? EXP_MAX_INPUT : tmp;
  return (2.0 / (1.0 + exp(tmp))) - 1.0;
}

template <class T>
class lstm {
 public:
  HOSTDEVICE void operator()(T &valueIn, T &valueIg, T &valueFg, T &valueOg,
                             T &prevState, T &state, T &stateAtv, T &output,
                             T &checkI, T &checkF, T &checkO) {
#if 0
    // TODO(qingqing) support to activation speficed by users
    valueIn = actInput(valueIn);
    valueIg = actGate(valueIg + prevState * checkI);
    valueFg = actGate(valueFg + prevState * checkF);
    state = valueIn * valueIg + prevState * valueFg;
    valueOg = actGate(valueOg + state * checkO);
    stateAtv = actState(state);
    output = valueOg * stateAtv;
#else
    valueIn = tanh<T>(valueIn);
    valueIg = sigmoid<T>(valueIg + prevState * checkI);
    valueFg = sigmoid<T>(valueFg + prevState * checkF);
    state = valueIn * valueIg + prevState * valueFg;
    valueOg = sigmoid<T>(valueOg + state * checkO);
    stateAtv = tanh<T>(state);
    output = valueOg * stateAtv;
#endif
  }
#ifndef __NVCC__
#ifndef __AVX__  // If not compiled with AVX instructs. Disable AVX by default
  static const bool avx = false;
#else
  // Only float support AVX optimization
  static const bool avx = std::is_same<T, float>::value;

  HOSTDEVICE void operator()(__m256 &valueIn, __m256 &valueIg, __m256 &valueFg,
                             __m256 &valueOg, __m256 &prevState, __m256 &state,
                             __m256 &stateAtv, __m256 &output, __m256 &checkI,
                             __m256 &checkF, __m256 &checkO,
                             hppl::Active<__m256>::forward actInput,
                             hppl::Active<__m256>::forward actGate,
                             hppl::Active<__m256>::forward actState) {
    valueIn = actInput(valueIn);
    valueIg = actGate(_mm256_add_ps(valueIg, _mm256_mul_ps(prevState, checkI)));
    valueFg = actGate(_mm256_add_ps(valueFg, _mm256_mul_ps(prevState, checkF)));
    state = _mm256_add_ps(_mm256_mul_ps(valueIn, valueIg),
                          _mm256_mul_ps(prevState, valueFg));
    valueOg = actGate(_mm256_add_ps(valueOg, _mm256_mul_ps(state, checkO)));
    stateAtv = actState(state);
    output = _mm256_mul_ps(valueOg, stateAtv);
  }
#endif
#endif
};

}  // namespace forward

namespace backward {

template <typename T>
DEVICE inline T sigmoid(const T a, const T b) {
  return a * b * (1.0 - b);
}

template <typename T>
DEVICE inline T tanh(const T a, const T b) {
  return a * (1.0 - b * b);
}

template <class T>
class lstm {
 public:
  HOSTDEVICE void operator()(T &valueIn, T &valueIg, T &valueFg, T &valueOg,
                             T &gradIn, T &gradIg, T &gradFg, T &gradOg,
                             T &prevState, T &prevStateGrad, T &state,
                             T &stateGrad, T &stateAtv, T &outputGrad,
                             T &checkI, T &checkF, T &checkO, T &checkIGrad,
                             T &checkFGrad, T &checkOGrad) {
#if 0
    // TODO(qingqing) support to activation speficed by users
    gradOg = actGate(outputGrad * stateAtv, valueOg);
    stateGrad += actState(outputGrad * valueOg, stateAtv) + gradOg * checkO;
    gradIn = actInput(stateGrad * valueIg, valueIn);
    gradIg = actGate(stateGrad * valueIn, valueIg);
    gradFg = actGate(stateGrad * prevState, valueFg);
    prevStateGrad = gradIg * checkI + gradFg * checkF + stateGrad * valueFg;
    checkIGrad = gradIg * prevState;
    checkFGrad = gradFg * prevState;
    checkOGrad = gradOg * state;
#else
    gradOg = sigmoid<T>(outputGrad * stateAtv, valueOg);
    stateGrad += tanh<T>(outputGrad * valueOg, stateAtv) + gradOg * checkO;
    gradIn = tanh<T>(stateGrad * valueIg, valueIn);
    gradIg = sigmoid<T>(stateGrad * valueIn, valueIg);
    gradFg = sigmoid<T>(stateGrad * prevState, valueFg);
    prevStateGrad = gradIg * checkI + gradFg * checkF + stateGrad * valueFg;
    checkIGrad = gradIg * prevState;
    checkFGrad = gradFg * prevState;
    checkOGrad = gradOg * state;
#endif
  }
#ifndef __NVCC__
#ifndef __AVX__  // If not compiled with AVX instructs. Disable AVX by default
  static const bool avx = false;
#else
  // Only float support AVX optimization
  static const bool avx = std::is_same<T, float>::value;
  HOSTDEVICE void operator()(__m256 &valueIn, __m256 &valueIg, __m256 &valueFg,
                             __m256 &valueOg, __m256 &gradIn, __m256 &gradIg,
                             __m256 &gradFg, __m256 &gradOg, __m256 &prevState,
                             __m256 &prevStateGrad, __m256 &state,
                             __m256 &stateGrad, __m256 &stateAtv,
                             __m256 &outputGrad, __m256 &checkI, __m256 &checkF,
                             __m256 &checkO, __m256 &checkIGrad,
                             __m256 &checkFGrad, __m256 &checkOGrad,
                             hppl::Active<__m256>::backward actInput,
                             hppl::Active<__m256>::backward actGate,
                             hppl::Active<__m256>::backward actState) {
    gradOg = actGate(_mm256_mul_ps(outputGrad, stateAtv), valueOg);
    stateGrad = _mm256_add_ps(
        actState(_mm256_mul_ps(outputGrad, valueOg), stateAtv), stateGrad);
    stateGrad = _mm256_add_ps(_mm256_mul_ps(gradOg, checkO), stateGrad);
    gradIn = actInput(_mm256_mul_ps(stateGrad, valueIg), valueIn);
    gradIg = actGate(_mm256_mul_ps(stateGrad, valueIn), valueIg);
    gradFg = actGate(_mm256_mul_ps(stateGrad, prevState), valueFg);
    prevStateGrad = _mm256_add_ps(_mm256_mul_ps(gradIg, checkI),
                                  _mm256_mul_ps(gradFg, checkF));
    prevStateGrad =
        _mm256_add_ps(_mm256_mul_ps(stateGrad, valueFg), prevStateGrad);
    checkIGrad = _mm256_mul_ps(gradIg, prevState);
    checkFGrad = _mm256_mul_ps(gradFg, prevState);
    checkOGrad = _mm256_mul_ps(gradOg, state);
  }
#endif
#endif
};

}  // namespace backward

}  // namespace detail
}  // namespace math
}  // namespace operators
}  // namespace paddle
