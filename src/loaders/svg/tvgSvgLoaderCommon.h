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
#ifndef _TVG_SVG_LOADER_COMMON_H_
#define _TVG_SVG_LOADER_COMMON_H_

#include "tvgCommon.h"

enum class SvgNodeType {
  Doc,
  G,
  Defs,
  // Switch,  //Not support
  Animation,
  Arc,
  Circle,
  Ellipse,
  Image,
  Line,
  Path,
  Polygon,
  Polyline,
  Rect,
  Text,
  TextArea,
  Tspan,
  Use,
  Video,
  ClipPath,
  // Custome_command,   //Not support
  Unknown
};

enum class SvgLengthType {
  Percent,
  Px,
  Pc,
  Pt,
  Mm,
  Cm,
  In,
};

enum class SvgCompositeFlags {
  ClipPath = 0x01,
};

enum class SvgFillFlags {
  Paint = 0x01,
  Opacity = 0x02,
  Gradient = 0x04,
  FillRule = 0x08,
  ClipPath = 0x16
};

enum class SvgStrokeFlags {
  Paint = 0x1,
  Opacity = 0x2,
  Gradient = 0x4,
  Scale = 0x8,
  Width = 0x10,
  Cap = 0x20,
  Join = 0x40,
  Dash = 0x80,
};

enum class SvgGradientType { Linear, Radial };

enum class SvgStyleType {
  Quality,
  Fill,
  ViewportFill,
  Font,
  Stroke,
  SolidColor,
  Gradient,
  Transform,
  Opacity,
  CompOp
};

enum class SvgFillRule { Winding = 0, OddEven = 1 };

// Length type to recalculate %, pt, pc, mm, cm etc
enum class SvgParserLengthType {
  Vertical,
  Horizontal,
  // In case of, for example, radius of radial gradient
  Other
};

struct SvgNode;
struct SvgStyleGradient;

template <class T>
struct SvgVector {
  T* list = nullptr;
  uint32_t cnt = 0;
  uint32_t reserved = 0;

  void push(T element) {
    if (cnt + 1 > reserved) {
      reserved = (cnt + 1) * 2;
      list = static_cast<T*>(realloc(list, sizeof(T) * reserved));
    }
    list[cnt++] = element;
  }

  void pop() {
    if (cnt > 0) --cnt;
  }

  void clear() {
    if (list) free(list);
    list = nullptr;
    cnt = reserved = 0;
  }
};

struct SvgDocNode {
  float w;
  float h;
  float vx;
  float vy;
  float vw;
  float vh;
  SvgNode* defs;
  bool preserveAspect;
};

struct SvgGNode {};

struct SvgDefsNode {
  SvgVector<SvgStyleGradient*> gradients;
};

struct SvgArcNode {};

struct SvgEllipseNode {
  float cx;
  float cy;
  float rx;
  float ry;
};

struct SvgCircleNode {
  float cx;
  float cy;
  float r;
};

struct SvgRectNode {
  float x;
  float y;
  float w;
  float h;
  float rx;
  float ry;
  bool hasRx;
  bool hasRy;
};

struct SvgLineNode {
  float x1;
  float y1;
  float x2;
  float y2;
};

struct SvgPathNode {
  string* path;
};

struct SvgPolygonNode {
  int pointsCount;
  float* points;
};

struct SvgLinearGradient {
  float x1;
  float y1;
  float x2;
  float y2;
};

struct SvgRadialGradient {
  float cx;
  float cy;
  float fx;
  float fy;
  float r;
};

struct SvgGradientStop {
  float offset;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

struct SvgComposite {
  SvgCompositeFlags flags;
  string* url;
  SvgNode* node;
};

struct SvgPaint {
  SvgStyleGradient* gradient;
  string* url;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  bool none;
  bool curColor;
};

struct SvgDash {
  SvgVector<float> array;
};

struct SvgStyleGradient {
  SvgGradientType type;
  string* id;
  string* ref;
  FillSpread spread;
  SvgRadialGradient* radial;
  SvgLinearGradient* linear;
  Matrix* transform;
  SvgVector<Fill::ColorStop*> stops;
  bool userSpace;
  bool usePercentage;
};

struct SvgStyleFill {
  SvgFillFlags flags;
  SvgPaint paint;
  int opacity;
  SvgFillRule fillRule;
};

struct SvgStyleStroke {
  SvgStrokeFlags flags;
  SvgPaint paint;
  int opacity;
  float scale;
  float width;
  float centered;
  StrokeCap cap;
  StrokeJoin join;
  SvgDash dash;
  int dashCount;
};

struct SvgStyleProperty {
  SvgStyleFill fill;
  SvgStyleStroke stroke;
  SvgComposite comp;
  int opacity;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct SvgNode {
  SvgNodeType type;
  SvgNode* parent;
  SvgVector<SvgNode*> child;
  string* id;
  SvgStyleProperty* style;
  Matrix* transform;
  union {
    SvgGNode g;
    SvgDocNode doc;
    SvgDefsNode defs;
    SvgArcNode arc;
    SvgCircleNode circle;
    SvgEllipseNode ellipse;
    SvgPolygonNode polygon;
    SvgPolygonNode polyline;
    SvgRectNode rect;
    SvgPathNode path;
    SvgLineNode line;
  } node;
  bool display;
};

struct SvgParser {
  SvgNode* node;
  SvgStyleGradient* styleGrad;
  Fill::ColorStop* gradStop;
  struct {
    int x, y;
    uint32_t w, h;
  } global;
  struct {
    bool parsedFx;
    bool parsedFy;
  } gradient;
};

struct SvgLoaderData {
  SvgVector<SvgNode*> stack = {nullptr, 0, 0};
  SvgNode* doc = nullptr;
  SvgNode* def = nullptr;
  SvgVector<SvgStyleGradient*> gradients;
  SvgStyleGradient* latestGradient = nullptr;  // For stops
  SvgParser* svgParse = nullptr;
  int level = 0;
  bool result = false;
};

#endif
