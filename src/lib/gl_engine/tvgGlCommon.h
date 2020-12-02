/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd. All rights reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _TVG_GL_COMMON_H_
#define _TVG_GL_COMMON_H_

#include <GLES2/gl2.h>
#include <assert.h>
#include "tvgCommon.h"
#include "tvgRender.h"

#define GL_CHECK(x)                                                                      \
  x;                                                                                     \
  do {                                                                                   \
    GLenum glError = glGetError();                                                       \
    if (glError != GL_NO_ERROR) {                                                        \
      printf("glGetError() = %i (0x%.8x) at line %s : %i\n", glError, glError, __FILE__, \
             __LINE__);                                                                  \
      assert(0);                                                                         \
    }                                                                                    \
  } while (0)

#define EGL_CHECK(x)                                                                        \
  x;                                                                                        \
  do {                                                                                      \
    EGLint eglError = eglGetError();                                                        \
    if (eglError != EGL_SUCCESS) {                                                          \
      printf("eglGetError() = %i (0x%.8x) at line %s : %i\n", eglError, eglError, __FILE__, \
             __LINE__);                                                                     \
      assert(0);                                                                            \
    }                                                                                       \
  } while (0)

class GlGeometry;

struct GlShape {
  float viewWd;
  float viewHt;
  RenderUpdateFlag updateFlag;
  unique_ptr<GlGeometry> geometry;
};

#endif /* _TVG_GL_COMMON_H_ */
