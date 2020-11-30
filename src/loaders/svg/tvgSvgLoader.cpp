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
#include <stddef.h>
#include <fstream>
#include <string.h>
#include <float.h>
#include <math.h>
#include "tvgLoaderMgr.h"
#include "tvgXmlParser.h"
#include "tvgSvgLoader.h"

/************************************************************************/
/* Internal Class Implementation                                        */
/************************************************************************/

typedef SvgNode* (*FactoryMethod)(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength);
typedef SvgStyleGradient* (*GradientFactoryMethod)(SvgLoaderData* loader, const char* buf, unsigned bufLength);


static char* _skipSpace(const char* str, const char* end)
{
    while (((end != nullptr && str < end) || (end == nullptr && *str != '\0')) && isspace(*str))
        ++str;
    return (char*)str;
}


static string* _copyId(const char* str)
{
    if (str == nullptr) return nullptr;

    return new string(str);
}


static const char* _skipComma(const char* content)
{
    content = _skipSpace(content, nullptr);
    if (*content == ',') return content + 1;
    return content;
}


static bool _parseNumber(const char** content, float* number)
{
    char* end = nullptr;

    *number = strtof(*content, &end);
    //If the start of string is not number
    if ((*content) == end) return false;
    //Skip comma if any
    *content = _skipComma(end);
    return true;
}

/**
 * According to https://www.w3.org/TR/SVG/coords.html#Units
 *
 * TODO
 * Since this documentation is not obvious, more clean recalculation with dpi
 * is required, but for now default w3 constants would be used
 */
static float _toFloat(SvgParser* svgParse, const char* str, SvgParserLengthType type)
{
    float parsedValue = strtof(str, nullptr);

    if (strstr(str, "cm")) parsedValue = parsedValue * 35.43307;
    else if (strstr(str, "mm")) parsedValue = parsedValue * 3.543307;
    else if (strstr(str, "pt")) parsedValue = parsedValue * 1.25;
    else if (strstr(str, "pc")) parsedValue = parsedValue * 15;
    else if (strstr(str, "in")) parsedValue = parsedValue * 90;
    else if (strstr(str, "%")) {
        if (type == SvgParserLengthType::Vertical) parsedValue = (parsedValue / 100.0) * svgParse->global.h;
        else if (type == SvgParserLengthType::Horizontal) parsedValue = (parsedValue / 100.0) * svgParse->global.w;
        else //if other then it's radius
        {
            float max = svgParse->global.w;
            if (max < svgParse->global.h)
                max = svgParse->global.h;
            parsedValue = (parsedValue / 100.0) * max;
        }
    }

    //TODO: Implement 'em', 'ex' attributes

    return parsedValue;
}


static float _gradientToFloat(SvgParser* svgParse, const char* str, SvgParserLengthType type)
{
    char* end = nullptr;

    float parsedValue = strtof(str, &end);
    float max = 1;

    /**
    * That is according to Units in here
    *
    * https://www.w3.org/TR/2015/WD-SVG2-20150915/coords.html
    */
    if (type == SvgParserLengthType::Vertical) max = svgParse->global.h;
    else if (type == SvgParserLengthType::Horizontal) max = svgParse->global.w;
    else if (type == SvgParserLengthType::Other) max = sqrt(pow(svgParse->global.h, 2) + pow(svgParse->global.w, 2)) / sqrt(2.0);

    if (strstr(str, "%")) parsedValue = parsedValue / 100.0;
    else if (strstr(str, "cm")) parsedValue = parsedValue * 35.43307;
    else if (strstr(str, "mm")) parsedValue = parsedValue * 3.543307;
    else if (strstr(str, "pt")) parsedValue = parsedValue * 1.25;
    else if (strstr(str, "pc")) parsedValue = parsedValue * 15;
    else if (strstr(str, "in")) parsedValue = parsedValue * 90;
    //TODO: Implement 'em', 'ex' attributes

    //Transform into global percentage
    parsedValue = parsedValue / max;

    return parsedValue;
}


static float _toOffset(const char* str)
{
    char* end = nullptr;

    float parsedValue = strtof(str, &end);

    if (strstr(str, "%")) parsedValue = parsedValue / 100.0;

    return parsedValue;
}


static int _toOpacity(const char* str)
{
    char* end = nullptr;
    int a = 0;
    float opacity = strtof(str, &end);

    if (end && (*end == '\0')) a = lrint(opacity * 255);
    return a;
}


#define _PARSE_TAG(Type, Name, Name1, Tags_Array, Default)                        \
    static Type _to##Name1(const char* str)                                       \
    {                                                                             \
        unsigned int i;                                                           \
                                                                                  \
        for (i = 0; i < sizeof(Tags_Array) / sizeof(Tags_Array[0]); i++) {        \
            if (!strcmp(str, Tags_Array[i].tag)) return Tags_Array[i].Name;       \
        }                                                                         \
        return Default;                                                           \
    }


/* parse the line cap used during stroking a path.
 * Value:    butt | round | square | inherit
 * Initial:    butt
 * https://www.w3.org/TR/SVG/painting.html
 */
static constexpr struct
{
    StrokeCap lineCap;
    const char* tag;
} lineCapTags[] = {
    { StrokeCap::Butt, "butt" },
    { StrokeCap::Round, "round" },
    { StrokeCap::Square, "square" }
};


_PARSE_TAG(StrokeCap, lineCap, LineCap, lineCapTags, StrokeCap::Butt)


/* parse the line join used during stroking a path.
 * Value:   miter | round | bevel | inherit
 * Initial:    miter
 * https://www.w3.org/TR/SVG/painting.html
 */
static constexpr struct
{
    StrokeJoin lineJoin;
    const char* tag;
} lineJoinTags[] = {
    { StrokeJoin::Miter, "miter" },
    { StrokeJoin::Round, "round" },
    { StrokeJoin::Bevel, "bevel" }
};


_PARSE_TAG(StrokeJoin, lineJoin, LineJoin, lineJoinTags, StrokeJoin::Miter)


/* parse the fill rule used during filling a path.
 * Value:   nonzero | evenodd | inherit
 * Initial:    nonzero
 * https://www.w3.org/TR/SVG/painting.html
 */
static constexpr struct
{
    SvgFillRule fillRule;
    const char* tag;
} fillRuleTags[] = {
    { SvgFillRule::OddEven, "evenodd" }
};


_PARSE_TAG(SvgFillRule, fillRule, FillRule, fillRuleTags, SvgFillRule::Winding)


/* parse the dash pattern used during stroking a path.
 * Value:   none | <dasharray> | inherit
 * Initial:    none
 * https://www.w3.org/TR/SVG/painting.html
 */
static inline void
_parseDashArray(const char *str, SvgDash* dash)
{
    char *end = nullptr;

    while (*str) {
        // skip white space, comma
        str = _skipComma(str);
        (*dash).array.push(strtof(str, &end));
        str = _skipComma(end);
    }
    //If dash array size is 1, it means that dash and gap size are the same.
    if ((*dash).array.cnt == 1) (*dash).array.push((*dash).array.list[0]);
}

static string* _idFromUrl(const char* url)
{
    char tmp[50];
    int i = 0;

    url = _skipSpace(url, nullptr);
    if ((*url) == '(') {
        ++url;
        url = _skipSpace(url, nullptr);
    }

    if ((*url) == '#') ++url;

    while ((*url) != ')') {
        tmp[i++] = *url;
        ++url;
    }
    tmp[i] = '\0';

    return new string(tmp);
}


static unsigned char _parserColor(const char* value, char** end)
{
    float r;

    r = strtof(value + 4, end);
    *end = _skipSpace(*end, nullptr);
    if (**end == '%') r = 255 * r / 100;
    *end = _skipSpace(*end, nullptr);

    if (r < 0 || r > 255) {
        *end = nullptr;
        return 0;
    }

    return lrint(r);
}


static constexpr struct
{
    const char* name;
    unsigned int value;
} colors[] = {
    { "aliceblue", 0xfff0f8ff },
    { "antiquewhite", 0xfffaebd7 },
    { "aqua", 0xff00ffff },
    { "aquamarine", 0xff7fffd4 },
    { "azure", 0xfff0ffff },
    { "beige", 0xfff5f5dc },
    { "bisque", 0xffffe4c4 },
    { "black", 0xff000000 },
    { "blanchedalmond", 0xffffebcd },
    { "blue", 0xff0000ff },
    { "blueviolet", 0xff8a2be2 },
    { "brown", 0xffa52a2a },
    { "burlywood", 0xffdeb887 },
    { "cadetblue", 0xff5f9ea0 },
    { "chartreuse", 0xff7fff00 },
    { "chocolate", 0xffd2691e },
    { "coral", 0xffff7f50 },
    { "cornflowerblue", 0xff6495ed },
    { "cornsilk", 0xfffff8dc },
    { "crimson", 0xffdc143c },
    { "cyan", 0xff00ffff },
    { "darkblue", 0xff00008b },
    { "darkcyan", 0xff008b8b },
    { "darkgoldenrod", 0xffb8860b },
    { "darkgray", 0xffa9a9a9 },
    { "darkgrey", 0xffa9a9a9 },
    { "darkgreen", 0xff006400 },
    { "darkkhaki", 0xffbdb76b },
    { "darkmagenta", 0xff8b008b },
    { "darkolivegreen", 0xff556b2f },
    { "darkorange", 0xffff8c00 },
    { "darkorchid", 0xff9932cc },
    { "darkred", 0xff8b0000 },
    { "darksalmon", 0xffe9967a },
    { "darkseagreen", 0xff8fbc8f },
    { "darkslateblue", 0xff483d8b },
    { "darkslategray", 0xff2f4f4f },
    { "darkslategrey", 0xff2f4f4f },
    { "darkturquoise", 0xff00ced1 },
    { "darkviolet", 0xff9400d3 },
    { "deeppink", 0xffff1493 },
    { "deepskyblue", 0xff00bfff },
    { "dimgray", 0xff696969 },
    { "dimgrey", 0xff696969 },
    { "dodgerblue", 0xff1e90ff },
    { "firebrick", 0xffb22222 },
    { "floralwhite", 0xfffffaf0 },
    { "forestgreen", 0xff228b22 },
    { "fuchsia", 0xffff00ff },
    { "gainsboro", 0xffdcdcdc },
    { "ghostwhite", 0xfff8f8ff },
    { "gold", 0xffffd700 },
    { "goldenrod", 0xffdaa520 },
    { "gray", 0xff808080 },
    { "grey", 0xff808080 },
    { "green", 0xff008000 },
    { "greenyellow", 0xffadff2f },
    { "honeydew", 0xfff0fff0 },
    { "hotpink", 0xffff69b4 },
    { "indianred", 0xffcd5c5c },
    { "indigo", 0xff4b0082 },
    { "ivory", 0xfffffff0 },
    { "khaki", 0xfff0e68c },
    { "lavender", 0xffe6e6fa },
    { "lavenderblush", 0xfffff0f5 },
    { "lawngreen", 0xff7cfc00 },
    { "lemonchiffon", 0xfffffacd },
    { "lightblue", 0xffadd8e6 },
    { "lightcoral", 0xfff08080 },
    { "lightcyan", 0xffe0ffff },
    { "lightgoldenrodyellow", 0xfffafad2 },
    { "lightgray", 0xffd3d3d3 },
    { "lightgrey", 0xffd3d3d3 },
    { "lightgreen", 0xff90ee90 },
    { "lightpink", 0xffffb6c1 },
    { "lightsalmon", 0xffffa07a },
    { "lightseagreen", 0xff20b2aa },
    { "lightskyblue", 0xff87cefa },
    { "lightslategray", 0xff778899 },
    { "lightslategrey", 0xff778899 },
    { "lightsteelblue", 0xffb0c4de },
    { "lightyellow", 0xffffffe0 },
    { "lime", 0xff00ff00 },
    { "limegreen", 0xff32cd32 },
    { "linen", 0xfffaf0e6 },
    { "magenta", 0xffff00ff },
    { "maroon", 0xff800000 },
    { "mediumaquamarine", 0xff66cdaa },
    { "mediumblue", 0xff0000cd },
    { "mediumorchid", 0xffba55d3 },
    { "mediumpurple", 0xff9370d8 },
    { "mediumseagreen", 0xff3cb371 },
    { "mediumslateblue", 0xff7b68ee },
    { "mediumspringgreen", 0xff00fa9a },
    { "mediumturquoise", 0xff48d1cc },
    { "mediumvioletred", 0xffc71585 },
    { "midnightblue", 0xff191970 },
    { "mintcream", 0xfff5fffa },
    { "mistyrose", 0xffffe4e1 },
    { "moccasin", 0xffffe4b5 },
    { "navajowhite", 0xffffdead },
    { "navy", 0xff000080 },
    { "oldlace", 0xfffdf5e6 },
    { "olive", 0xff808000 },
    { "olivedrab", 0xff6b8e23 },
    { "orange", 0xffffa500 },
    { "orangered", 0xffff4500 },
    { "orchid", 0xffda70d6 },
    { "palegoldenrod", 0xffeee8aa },
    { "palegreen", 0xff98fb98 },
    { "paleturquoise", 0xffafeeee },
    { "palevioletred", 0xffd87093 },
    { "papayawhip", 0xffffefd5 },
    { "peachpuff", 0xffffdab9 },
    { "peru", 0xffcd853f },
    { "pink", 0xffffc0cb },
    { "plum", 0xffdda0dd },
    { "powderblue", 0xffb0e0e6 },
    { "purple", 0xff800080 },
    { "red", 0xffff0000 },
    { "rosybrown", 0xffbc8f8f },
    { "royalblue", 0xff4169e1 },
    { "saddlebrown", 0xff8b4513 },
    { "salmon", 0xfffa8072 },
    { "sandybrown", 0xfff4a460 },
    { "seagreen", 0xff2e8b57 },
    { "seashell", 0xfffff5ee },
    { "sienna", 0xffa0522d },
    { "silver", 0xffc0c0c0 },
    { "skyblue", 0xff87ceeb },
    { "slateblue", 0xff6a5acd },
    { "slategray", 0xff708090 },
    { "slategrey", 0xff708090 },
    { "snow", 0xfffffafa },
    { "springgreen", 0xff00ff7f },
    { "steelblue", 0xff4682b4 },
    { "tan", 0xffd2b48c },
    { "teal", 0xff008080 },
    { "thistle", 0xffd8bfd8 },
    { "tomato", 0xffff6347 },
    { "turquoise", 0xff40e0d0 },
    { "violet", 0xffee82ee },
    { "wheat", 0xfff5deb3 },
    { "white", 0xffffffff },
    { "whitesmoke", 0xfff5f5f5 },
    { "yellow", 0xffffff00 },
    { "yellowgreen", 0xff9acd32 }
};


static void _toColor(const char* str, uint8_t* r, uint8_t* g, uint8_t* b, string** ref)
{
    unsigned int i, len = strlen(str);
    char *red, *green, *blue;
    unsigned char tr, tg, tb;

    if (len == 4 && str[0] == '#') {
        //Case for "#456" should be interprete as "#445566"
        if (isxdigit(str[1]) && isxdigit(str[2]) && isxdigit(str[3])) {
            char tmp[3] = { '\0', '\0', '\0' };
            tmp[0] = str[1];
            tmp[1] = str[1];
            *r = strtol(tmp, nullptr, 16);
            tmp[0] = str[2];
            tmp[1] = str[2];
            *g = strtol(tmp, nullptr, 16);
            tmp[0] = str[3];
            tmp[1] = str[3];
            *b = strtol(tmp, nullptr, 16);
        }
    } else if (len == 7 && str[0] == '#') {
        if (isxdigit(str[1]) && isxdigit(str[2]) && isxdigit(str[3]) && isxdigit(str[4]) && isxdigit(str[5]) && isxdigit(str[6])) {
            char tmp[3] = { '\0', '\0', '\0' };
            tmp[0] = str[1];
            tmp[1] = str[2];
            *r = strtol(tmp, nullptr, 16);
            tmp[0] = str[3];
            tmp[1] = str[4];
            *g = strtol(tmp, nullptr, 16);
            tmp[0] = str[5];
            tmp[1] = str[6];
            *b = strtol(tmp, nullptr, 16);
        }
    } else if (len >= 10 && (str[0] == 'r' || str[0] == 'R') && (str[1] == 'g' || str[1] == 'G') && (str[2] == 'b' || str[2] == 'B') && str[3] == '(' && str[len - 1] == ')') {
        tr = _parserColor(str + 4, &red);
        if (red && *red == ',') {
            tg = _parserColor(red + 1, &green);
            if (green && *green == ',') {
                tb = _parserColor(green + 1, &blue);
                if (blue && blue[0] == ')' && blue[1] == '\0') {
                    *r = tr;
                    *g = tg;
                    *b = tb;
                }
            }
        }
    } else if (len >= 3 && !strncmp(str, "url", 3)) {
        *ref = _idFromUrl((const char*)(str + 3));
    } else {
        //Handle named color
        for (i = 0; i < (sizeof(colors) / sizeof(colors[0])); i++) {
            if (!strcasecmp(colors[i].name, str)) {
                *r = (((uint8_t*)(&(colors[i].value)))[2]);
                *g = (((uint8_t*)(&(colors[i].value)))[1]);
                *b = (((uint8_t*)(&(colors[i].value)))[0]);
            }
        }
    }
}


static char* _parseNumbersArray(char* str, float* points, int* ptCount)
{
    int count = 0;
    char* end = nullptr;

    str = _skipSpace(str, nullptr);
    while (isdigit(*str) || *str == '-' || *str == '+' || *str == '.') {
        points[count++] = strtof(str, &end);
        str = end;
        str = _skipSpace(str, nullptr);
        if (*str == ',') ++str;
        //Eat the rest of space
        str = _skipSpace(str, nullptr);
    }
    *ptCount = count;
    return str;
}


enum class MatrixState {
    Unknown,
    Matrix,
    Translate,
    Rotate,
    Scale,
    SkewX,
    SkewY
};


#define MATRIX_DEF(Name, Value)     \
    {                               \
#Name, sizeof(#Name), Value \
    }


static constexpr struct
{
    const char* tag;
    int sz;
    MatrixState state;
} matrixTags[] = {
    MATRIX_DEF(matrix, MatrixState::Matrix),
    MATRIX_DEF(translate, MatrixState::Translate),
    MATRIX_DEF(rotate, MatrixState::Rotate),
    MATRIX_DEF(scale, MatrixState::Scale),
    MATRIX_DEF(skewX, MatrixState::SkewX),
    MATRIX_DEF(skewY, MatrixState::SkewY)
};


static void _matrixCompose(const Matrix* m1,
                            const Matrix* m2,
                            Matrix* dst)
{
    float a11, a12, a13, a21, a22, a23, a31, a32, a33;

    a11 = (m1->e11 * m2->e11) + (m1->e12 * m2->e21) + (m1->e13 * m2->e31);
    a12 = (m1->e11 * m2->e12) + (m1->e12 * m2->e22) + (m1->e13 * m2->e32);
    a13 = (m1->e11 * m2->e13) + (m1->e12 * m2->e23) + (m1->e13 * m2->e33);

    a21 = (m1->e21 * m2->e11) + (m1->e22 * m2->e21) + (m1->e23 * m2->e31);
    a22 = (m1->e21 * m2->e12) + (m1->e22 * m2->e22) + (m1->e23 * m2->e32);
    a23 = (m1->e21 * m2->e13) + (m1->e22 * m2->e23) + (m1->e23 * m2->e33);

    a31 = (m1->e31 * m2->e11) + (m1->e32 * m2->e21) + (m1->e33 * m2->e31);
    a32 = (m1->e31 * m2->e12) + (m1->e32 * m2->e22) + (m1->e33 * m2->e32);
    a33 = (m1->e31 * m2->e13) + (m1->e32 * m2->e23) + (m1->e33 * m2->e33);

    dst->e11 = a11;
    dst->e12 = a12;
    dst->e13 = a13;
    dst->e21 = a21;
    dst->e22 = a22;
    dst->e23 = a23;
    dst->e31 = a31;
    dst->e32 = a32;
    dst->e33 = a33;
}


/* parse transform attribute
 * https://www.w3.org/TR/SVG/coords.html#TransformAttribute
 */
static Matrix* _parseTransformationMatrix(const char* value)
{
    unsigned int i;
    float points[8];
    int ptCount = 0;
    float sx, sy;
    MatrixState state = MatrixState::Unknown;
    Matrix* matrix = (Matrix*)calloc(1, sizeof(Matrix));
    char* str = (char*)value;
    char* end = str + strlen(str);

    *matrix = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    while (str < end) {
        if (isspace(*str) || (*str == ',')) {
            ++str;
            continue;
        }
        for (i = 0; i < sizeof(matrixTags) / sizeof(matrixTags[0]); i++) {
            if (!strncmp(matrixTags[i].tag, str, matrixTags[i].sz - 1)) {
                state = matrixTags[i].state;
                str += (matrixTags[i].sz - 1);
            }
        }
        if (state == MatrixState::Unknown) goto error;

        str = _skipSpace(str, end);
        if (*str != '(') goto error;
        ++str;
        str = _parseNumbersArray(str, points, &ptCount);
        if (*str != ')') goto error;
        ++str;

        if (state == MatrixState::Matrix) {
            Matrix tmp;

            if (ptCount != 6) goto error;

            tmp = { points[0], points[2], points[4], points[1], points[3], points[5], 0, 0, 1 };
            _matrixCompose(matrix, &tmp, matrix);
        } else if (state == MatrixState::Translate) {
            Matrix tmp;

            if (ptCount == 1) {
                tmp = { 1, 0, points[0], 0, 1, 0, 0, 0, 1 };
                _matrixCompose(matrix, &tmp, matrix);
            } else if (ptCount == 2) {
                tmp = { 1, 0, points[0], 0, 1, points[1], 0, 0, 1 };
                _matrixCompose(matrix, &tmp, matrix);
            } else goto error;
        } else if (state == MatrixState::Rotate) {
            Matrix tmp;
            float c, s;
            //Transform to signed.
            points[0] = fmod(points[0], 360);
            if (points[0] < 0) points[0] += 360;

            c = cosf(points[0] * (M_PI / 180.0));
            s = sinf(points[0] * (M_PI / 180.0));
            if (ptCount == 1) {
                tmp = { c, -s, 0, s, c, 0, 0, 0, 1 };
                _matrixCompose(matrix, &tmp, matrix);
            } else if (ptCount == 3) {
                tmp = { 1, 0, points[1], 0, 1, points[2], 0, 0, 1 };
                _matrixCompose(matrix, &tmp, matrix);

                tmp = { c, -s, 0, s, c, 0, 0, 0, 1 };
                _matrixCompose(matrix, &tmp, matrix);

                tmp = { 1, 0, -points[1], 0, 1, -points[2], 0, 0, 1 };
                _matrixCompose(matrix, &tmp, matrix);
            } else {
                goto error;
            }
        } else if (state == MatrixState::Scale) {
            Matrix tmp;
            if (ptCount < 1 || ptCount > 2) goto error;

            sx = points[0];
            sy = sx;
            if (ptCount == 2) sy = points[1];
            tmp = { sx, 0, 0, 0, sy, 0, 0, 0, 1 };
            _matrixCompose(matrix, &tmp, matrix);
        }
    }
error:
    return matrix;
}


#define LENGTH_DEF(Name, Value)     \
    {                               \
#Name, sizeof(#Name), Value \
    }


static constexpr struct
{
    const char* tag;
    int sz;
    SvgLengthType type;
} lengthTags[] = {
    LENGTH_DEF(%, SvgLengthType::Percent),
    LENGTH_DEF(px, SvgLengthType::Px),
    LENGTH_DEF(pc, SvgLengthType::Pc),
    LENGTH_DEF(pt, SvgLengthType::Pt),
    LENGTH_DEF(mm, SvgLengthType::Mm),
    LENGTH_DEF(cm, SvgLengthType::Cm),
    LENGTH_DEF(in, SvgLengthType::In)
};


static float _parseLength(const char* str, SvgLengthType* type)
{
    unsigned int i;
    float value;
    int sz = strlen(str);

    *type = SvgLengthType::Px;
    for (i = 0; i < sizeof(lengthTags) / sizeof(lengthTags[0]); i++) {
        if (lengthTags[i].sz - 1 == sz && !strncmp(lengthTags[i].tag, str, sz)) *type = lengthTags[i].type;
    }
    value = strtof(str, nullptr);
    return value;
}


static bool _parseStyleAttr(void* data, const char* key, const char* value);


static bool _attrParseSvgNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;
    SvgDocNode* doc = &(node->node.doc);
    SvgLengthType type;

    //TODO: handle length unit.
    if (!strcmp(key, "width")) {
        doc->w = _parseLength(value, &type);
    } else if (!strcmp(key, "height")) {
        doc->h = _parseLength(value, &type);
    } else if (!strcmp(key, "viewBox")) {
        if (_parseNumber(&value, &doc->vx)) {
            if (_parseNumber(&value, &doc->vy)) {
                if (_parseNumber(&value, &doc->vw)) {
                    _parseNumber(&value, &doc->vh);
                    loader->svgParse->global.h = doc->vh;
                }
                loader->svgParse->global.w = doc->vw;
            }
            loader->svgParse->global.y = doc->vy;
        }
        loader->svgParse->global.x = doc->vx;
    } else if (!strcmp(key, "preserveAspectRatio")) {
        if (!strcmp(value, "none")) doc->preserveAspect = false;
    } else if (!strcmp(key, "style")) {
        return simpleXmlParseW3CAttribute(value, _parseStyleAttr, loader);
    } else {
        return _parseStyleAttr(loader, key, value);
    }
    return true;
}


//https://www.w3.org/TR/SVGTiny12/painting.html#SpecifyingPaint
static void _handlePaintAttr(SvgPaint* paint, const char* value)
{
    if (!strcmp(value, "none")) {
        //No paint property
        paint->none = true;
        return;
    }
    paint->none = false;
    if (!strcmp(value, "currentColor")) {
        paint->curColor = true;
        return;
    }
    _toColor(value, &paint->r, &paint->g, &paint->b, &paint->url);
}


static void _handleColorAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    SvgStyleProperty* style = node->style;
    _toColor(value, &style->r, &style->g, &style->b, nullptr);
}


static void _handleFillAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    SvgStyleProperty* style = node->style;
    style->fill.flags = (SvgFillFlags)((int)style->fill.flags | (int)SvgFillFlags::Paint);
    _handlePaintAttr(&style->fill.paint, value);
}


static void _handleStrokeAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    SvgStyleProperty* style = node->style;
    style->stroke.flags = (SvgStrokeFlags)((int)style->stroke.flags | (int)SvgStrokeFlags::Paint);
    _handlePaintAttr(&style->stroke.paint, value);
}


static void _handleStrokeOpacityAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    node->style->stroke.flags = (SvgStrokeFlags)((int)node->style->stroke.flags | (int)SvgStrokeFlags::Opacity);
    node->style->stroke.opacity = _toOpacity(value);
}

static void _handleStrokeDashArrayAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    node->style->stroke.flags = (SvgStrokeFlags)((int)node->style->stroke.flags | (int)SvgStrokeFlags::Dash);
    _parseDashArray(value, &node->style->stroke.dash);
}

static void _handleStrokeWidthAttr(SvgLoaderData* loader, SvgNode* node, const char* value)
{
    node->style->stroke.flags = (SvgStrokeFlags)((int)node->style->stroke.flags | (int)SvgStrokeFlags::Width);
    node->style->stroke.width = _toFloat(loader->svgParse, value, SvgParserLengthType::Horizontal);
}


static void _handleStrokeLineCapAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    node->style->stroke.flags = (SvgStrokeFlags)((int)node->style->stroke.flags | (int)SvgStrokeFlags::Cap);
    node->style->stroke.cap = _toLineCap(value);
}


static void _handleStrokeLineJoinAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    node->style->stroke.flags = (SvgStrokeFlags)((int)node->style->stroke.flags | (int)SvgStrokeFlags::Join);
    node->style->stroke.join = _toLineJoin(value);
}


static void _handleFillRuleAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    node->style->fill.flags = (SvgFillFlags)((int)node->style->fill.flags | (int)SvgFillFlags::FillRule);
    node->style->fill.fillRule = _toFillRule(value);
}


static void _handleOpacityAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    node->style->opacity = _toOpacity(value);
}


static void _handleFillOpacityAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    node->style->fill.flags = (SvgFillFlags)((int)node->style->fill.flags | (int)SvgFillFlags::Opacity);
    node->style->fill.opacity = _toOpacity(value);
}


static void _handleTransformAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    node->transform = _parseTransformationMatrix(value);
}

static void _handleClipPathAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    SvgStyleProperty* style = node->style;
    style->comp.flags = (SvgCompositeFlags)((int)style->comp.flags | (int)SvgCompositeFlags::ClipPath);

    int len = strlen(value);
    if (len >= 3 && !strncmp(value, "url", 3)) style->comp.url = _idFromUrl((const char*)(value + 3));
}

static void _handleDisplayAttr(TVG_UNUSED SvgLoaderData* loader, SvgNode* node, const char* value)
{
    //TODO : The display attribute can have various values as well as "none".
    //       The default is "inline" which means visible and "none" means invisible.
    //       Depending on the type of node, additional functionality may be required.
    //       refer to https://developer.mozilla.org/en-US/docs/Web/SVG/Attribute/display
    if (!strcmp(value, "none")) node->display = false;
    else node->display = true;
}


typedef void (*styleMethod)(SvgLoaderData* loader, SvgNode* node, const char* value);


#define STYLE_DEF(Name, Name1)                       \
    {                                                \
#Name, sizeof(#Name), _handle##Name1##Attr \
    }


static constexpr struct
{
    const char* tag;
    int sz;
    styleMethod tagHandler;
} styleTags[] = {
    STYLE_DEF(color, Color),
    STYLE_DEF(fill, Fill),
    STYLE_DEF(fill-rule, FillRule),
    STYLE_DEF(fill-opacity, FillOpacity),
    STYLE_DEF(opacity, Opacity),
    STYLE_DEF(stroke, Stroke),
    STYLE_DEF(stroke-width, StrokeWidth),
    STYLE_DEF(stroke-linejoin, StrokeLineJoin),
    STYLE_DEF(stroke-linecap, StrokeLineCap),
    STYLE_DEF(stroke-opacity, StrokeOpacity),
    STYLE_DEF(stroke-dasharray, StrokeDashArray),
    STYLE_DEF(transform, Transform),
    STYLE_DEF(clip-path, ClipPath),
    STYLE_DEF(display, Display)
};


static bool _parseStyleAttr(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;
    unsigned int i;
    int sz;
    if (!key || !value) return false;

    //Trim the white space
    key = _skipSpace(key, nullptr);

    value = _skipSpace(value, nullptr);

    sz = strlen(key);
    for (i = 0; i < sizeof(styleTags) / sizeof(styleTags[0]); i++) {
        if (styleTags[i].sz - 1 == sz && !strncmp(styleTags[i].tag, key, sz)) {
            styleTags[i].tagHandler(loader, node, value);
            return true;
        }
    }

    return true;
}

/* parse g node
 * https://www.w3.org/TR/SVG/struct.html#Groups
 */
static bool _attrParseGNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;

    if (!strcmp(key, "style")) {
        return simpleXmlParseW3CAttribute(value, _parseStyleAttr, loader);
    } else if (!strcmp(key, "transform")) {
        node->transform = _parseTransformationMatrix(value);
    } else if (!strcmp(key, "id")) {
        node->id = _copyId(value);
    } else if (!strcmp(key, "clip-path")) {
        _handleClipPathAttr(loader, node, value);
    } else {
        return _parseStyleAttr(loader, key, value);
    }
    return true;
}


/* parse clipPath node
 * https://www.w3.org/TR/SVG/struct.html#Groups
 */
static bool _attrParseClipPathNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;

    if (!strcmp(key, "style")) {
        return simpleXmlParseW3CAttribute(value, _parseStyleAttr, loader);
    } else if (!strcmp(key, "transform")) {
        node->transform = _parseTransformationMatrix(value);
    } else if (!strcmp(key, "id")) {
        node->id = _copyId(value);
    } else {
        return _parseStyleAttr(loader, key, value);
    }
    return true;
}

static SvgNode* _createNode(SvgNode* parent, SvgNodeType type)
{
    SvgNode* node = (SvgNode*)calloc(1, sizeof(SvgNode));

    //Default fill property
    node->style = (SvgStyleProperty*)calloc(1, sizeof(SvgStyleProperty));

    //Update the default value of stroke and fill
    //https://www.w3.org/TR/SVGTiny12/painting.html#SpecifyingPaint
    node->style->fill.paint.none = false;
    //Default fill opacity is 1
    node->style->fill.opacity = 255;
    node->style->opacity = 255;

    //Default fill rule is nonzero
    node->style->fill.fillRule = SvgFillRule::Winding;

    //Default stroke is none
    node->style->stroke.paint.none = true;
    //Default stroke opacity is 1
    node->style->stroke.opacity = 255;
    //Default stroke width is 1
    node->style->stroke.width = 1;
    //Default line cap is butt
    node->style->stroke.cap = StrokeCap::Butt;
    //Default line join is miter
    node->style->stroke.join = StrokeJoin::Miter;
    node->style->stroke.scale = 1.0;

    //Default display is true("inline").
    node->display = true;

    node->parent = parent;
    node->type = type;

    if (parent) parent->child.push(node);
    return node;
}


static SvgNode* _createDefsNode(TVG_UNUSED SvgLoaderData* loader, TVG_UNUSED SvgNode* parent, const char* buf, unsigned bufLength)
{
    SvgNode* node = _createNode(nullptr, SvgNodeType::Defs);
    simpleXmlParseAttributes(buf, bufLength, nullptr, node);
    return node;
}


static SvgNode* _createGNode(TVG_UNUSED SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::G);

    simpleXmlParseAttributes(buf, bufLength, _attrParseGNode, loader);
    return loader->svgParse->node;
}


static SvgNode* _createSvgNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::Doc);
    SvgDocNode* doc = &(loader->svgParse->node->node.doc);

    doc->preserveAspect = true;
    simpleXmlParseAttributes(buf, bufLength, _attrParseSvgNode, loader);

    return loader->svgParse->node;
}


static SvgNode* _createMaskNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::Unknown);

    loader->svgParse->node->display = false;

    return loader->svgParse->node;
}


static SvgNode* _createClipPathNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::ClipPath);

    loader->svgParse->node->display = false;

    simpleXmlParseAttributes(buf, bufLength, _attrParseClipPathNode, loader);

    return loader->svgParse->node;
}

static bool _attrParsePathNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;
    SvgPathNode* path = &(node->node.path);

    if (!strcmp(key, "d")) {
        //Temporary: need to copy
        path->path = _copyId(value);
    } else if (!strcmp(key, "style")) {
        return simpleXmlParseW3CAttribute(value, _parseStyleAttr, loader);
    } else if (!strcmp(key, "clip-path")) {
        _handleClipPathAttr(loader, node, value);
    } else if (!strcmp(key, "id")) {
        node->id = _copyId(value);
    } else {
        return _parseStyleAttr(loader, key, value);
    }
    return true;
}


static SvgNode* _createPathNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::Path);

    simpleXmlParseAttributes(buf, bufLength, _attrParsePathNode, loader);

    return loader->svgParse->node;
}


static constexpr struct
{
    const char* tag;
    SvgParserLengthType type;
    int sz;
    size_t offset;
} circleTags[] = {
    {"cx", SvgParserLengthType::Horizontal, sizeof("cx"), offsetof(SvgCircleNode, cx)},
    {"cy", SvgParserLengthType::Vertical, sizeof("cy"), offsetof(SvgCircleNode, cy)},
    {"r", SvgParserLengthType::Other, sizeof("r"), offsetof(SvgCircleNode, r)}
};


/* parse the attributes for a circle element.
 * https://www.w3.org/TR/SVG/shapes.html#CircleElement
 */
static bool _attrParseCircleNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;
    SvgCircleNode* circle = &(node->node.circle);
    unsigned int i;
    unsigned char* array;
    int sz = strlen(key);

    array = (unsigned char*)circle;
    for (i = 0; i < sizeof(circleTags) / sizeof(circleTags[0]); i++) {
        if (circleTags[i].sz - 1 == sz && !strncmp(circleTags[i].tag, key, sz)) {
            *((float*)(array + circleTags[i].offset)) = _toFloat(loader->svgParse, value, circleTags[i].type);
            return true;
        }
    }

    if (!strcmp(key, "style")) {
        return simpleXmlParseW3CAttribute(value, _parseStyleAttr, loader);
    } else if (!strcmp(key, "clip-path")) {
        _handleClipPathAttr(loader, node, value);
    } else if (!strcmp(key, "id")) {
        node->id = _copyId(value);
    } else {
        return _parseStyleAttr(loader, key, value);
    }
    return true;
}


static SvgNode* _createCircleNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::Circle);

    simpleXmlParseAttributes(buf, bufLength, _attrParseCircleNode, loader);
    return loader->svgParse->node;
}


static constexpr struct
{
    const char* tag;
    SvgParserLengthType type;
    int sz;
    size_t offset;
} ellipseTags[] = {
    {"cx", SvgParserLengthType::Horizontal, sizeof("cx"), offsetof(SvgEllipseNode, cx)},
    {"cy", SvgParserLengthType::Vertical, sizeof("cy"), offsetof(SvgEllipseNode, cy)},
    {"rx", SvgParserLengthType::Horizontal, sizeof("rx"), offsetof(SvgEllipseNode, rx)},
    {"ry", SvgParserLengthType::Vertical, sizeof("ry"), offsetof(SvgEllipseNode, ry)}
};


/* parse the attributes for an ellipse element.
 * https://www.w3.org/TR/SVG/shapes.html#EllipseElement
 */
static bool _attrParseEllipseNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;
    SvgEllipseNode* ellipse = &(node->node.ellipse);
    unsigned int i;
    unsigned char* array;
    int sz = strlen(key);

    array = (unsigned char*)ellipse;
    for (i = 0; i < sizeof(ellipseTags) / sizeof(ellipseTags[0]); i++) {
        if (ellipseTags[i].sz - 1 == sz && !strncmp(ellipseTags[i].tag, key, sz)) {
            *((float*)(array + ellipseTags[i].offset)) = _toFloat(loader->svgParse, value, ellipseTags[i].type);
            return true;
        }
    }

    if (!strcmp(key, "id")) {
        node->id = _copyId(value);
    } else if (!strcmp(key, "style")) {
        return simpleXmlParseW3CAttribute(value, _parseStyleAttr, loader);
    } else if (!strcmp(key, "clip-path")) {
        _handleClipPathAttr(loader, node, value);
    } else {
        return _parseStyleAttr(loader, key, value);
    }
    return true;
}


static SvgNode* _createEllipseNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::Ellipse);

    simpleXmlParseAttributes(buf, bufLength, _attrParseEllipseNode, loader);
    return loader->svgParse->node;
}


static bool _attrParsePolygonPoints(const char* str, float** points, int* ptCount)
{
    float tmp[50];
    int tmpCount = 0;
    int count = 0;
    float num;
    float *pointArray = nullptr, *tmpArray;

    while (_parseNumber(&str, &num)) {
        tmp[tmpCount++] = num;
        if (tmpCount == 50) {
            tmpArray = (float*)realloc(pointArray, (count + tmpCount) * sizeof(float));
            if (!tmpArray) goto error_alloc;
            pointArray = tmpArray;
            memcpy(&pointArray[count], tmp, tmpCount * sizeof(float));
            count += tmpCount;
            tmpCount = 0;
        }
    }

    if (tmpCount > 0) {
        tmpArray = (float*)realloc(pointArray, (count + tmpCount) * sizeof(float));
        if (!tmpArray) goto error_alloc;
        pointArray = tmpArray;
        memcpy(&pointArray[count], tmp, tmpCount * sizeof(float));
        count += tmpCount;
    }
    *ptCount = count;
    *points = pointArray;
    return true;

error_alloc:
    //LOG: allocation for point array failed. out of memory
    return false;
}


/* parse the attributes for a polygon element.
 * https://www.w3.org/TR/SVG/shapes.html#PolylineElement
 */
static bool _attrParsePolygonNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;
    SvgPolygonNode* polygon = nullptr;

    if (node->type == SvgNodeType::Polygon) polygon = &(node->node.polygon);
    else polygon = &(node->node.polyline);

    if (!strcmp(key, "points")) {
        return _attrParsePolygonPoints(value, &polygon->points, &polygon->pointsCount);
    } else if (!strcmp(key, "style")) {
        return simpleXmlParseW3CAttribute(value, _parseStyleAttr, loader);
    } else if (!strcmp(key, "clip-path")) {
        _handleClipPathAttr(loader, node, value);
    } else if (!strcmp(key, "id")) {
        node->id = _copyId(value);
    } else {
        return _parseStyleAttr(loader, key, value);
    }
    return true;
}


static SvgNode* _createPolygonNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::Polygon);

    simpleXmlParseAttributes(buf, bufLength, _attrParsePolygonNode, loader);
    return loader->svgParse->node;
}


static SvgNode* _createPolylineNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::Polyline);

    simpleXmlParseAttributes(buf, bufLength, _attrParsePolygonNode, loader);
    return loader->svgParse->node;
}

static constexpr struct
{
    const char* tag;
    SvgParserLengthType type;
    int sz;
    size_t offset;
} rectTags[] = {
    {"x", SvgParserLengthType::Horizontal, sizeof("x"), offsetof(SvgRectNode, x)},
    {"y", SvgParserLengthType::Vertical, sizeof("y"), offsetof(SvgRectNode, y)},
    {"width", SvgParserLengthType::Horizontal, sizeof("width"), offsetof(SvgRectNode, w)},
    {"height", SvgParserLengthType::Vertical, sizeof("height"), offsetof(SvgRectNode, h)},
    {"rx", SvgParserLengthType::Horizontal, sizeof("rx"), offsetof(SvgRectNode, rx)},
    {"ry", SvgParserLengthType::Vertical, sizeof("ry"), offsetof(SvgRectNode, ry)}
};


/* parse the attributes for a rect element.
 * https://www.w3.org/TR/SVG/shapes.html#RectElement
 */
static bool _attrParseRectNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;
    SvgRectNode* rect = &(node->node.rect);
    unsigned int i;
    unsigned char* array;
    bool ret = true;
    int sz = strlen(key);

    array = (unsigned char*)rect;
    for (i = 0; i < sizeof(rectTags) / sizeof(rectTags[0]); i++) {
        if (rectTags[i].sz - 1 == sz && !strncmp(rectTags[i].tag, key, sz)) {
            *((float*)(array + rectTags[i].offset)) = _toFloat(loader->svgParse, value, rectTags[i].type);

            //Case if only rx or ry is declared
            if (!strncmp(rectTags[i].tag, "rx", sz)) rect->hasRx = true;
            if (!strncmp(rectTags[i].tag, "ry", sz)) rect->hasRy = true;

            if ((rect->rx > FLT_EPSILON) && (rect->ry <= FLT_EPSILON) && rect->hasRx && !rect->hasRy) rect->ry = rect->rx;
            if ((rect->ry > FLT_EPSILON) && (rect->rx <= FLT_EPSILON) && !rect->hasRx && rect->hasRy) rect->rx = rect->ry;
            return ret;
        }
    }

    if (!strcmp(key, "id")) {
        node->id = _copyId(value);
    } else if (!strcmp(key, "style")) {
        ret = simpleXmlParseW3CAttribute(value, _parseStyleAttr, loader);
    } else if (!strcmp(key, "clip-path")) {
        _handleClipPathAttr(loader, node, value);
    } else {
        ret = _parseStyleAttr(loader, key, value);
    }

    return ret;
}


static SvgNode* _createRectNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::Rect);
    if (loader->svgParse->node) {
        loader->svgParse->node->node.rect.hasRx = loader->svgParse->node->node.rect.hasRy = false;
    }

    simpleXmlParseAttributes(buf, bufLength, _attrParseRectNode, loader);
    return loader->svgParse->node;
}


static constexpr struct
{
    const char* tag;
    SvgParserLengthType type;
    int sz;
    size_t offset;
} lineTags[] = {
    {"x1", SvgParserLengthType::Horizontal, sizeof("x1"), offsetof(SvgLineNode, x1)},
    {"y1", SvgParserLengthType::Vertical, sizeof("y1"), offsetof(SvgLineNode, y1)},
    {"x2", SvgParserLengthType::Horizontal, sizeof("x2"), offsetof(SvgLineNode, x2)},
    {"y2", SvgParserLengthType::Vertical, sizeof("y2"), offsetof(SvgLineNode, y2)}
};


/* parse the attributes for a rect element.
 * https://www.w3.org/TR/SVG/shapes.html#LineElement
 */
static bool _attrParseLineNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode* node = loader->svgParse->node;
    SvgLineNode* line = &(node->node.line);
    unsigned int i;
    unsigned char* array;
    int sz = strlen(key);

    array = (unsigned char*)line;
    for (i = 0; i < sizeof(lineTags) / sizeof(lineTags[0]); i++) {
        if (lineTags[i].sz - 1 == sz && !strncmp(lineTags[i].tag, key, sz)) {
            *((float*)(array + lineTags[i].offset)) = _toFloat(loader->svgParse, value, lineTags[i].type);
            return true;
        }
    }

    if (!strcmp(key, "id")) {
        node->id = _copyId(value);
    } else if (!strcmp(key, "style")) {
        return simpleXmlParseW3CAttribute(value, _parseStyleAttr, loader);
    } else if (!strcmp(key, "clip-path")) {
        _handleClipPathAttr(loader, node, value);
    } else {
        return _parseStyleAttr(loader, key, value);
    }
    return true;
}


static SvgNode* _createLineNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::Line);

    simpleXmlParseAttributes(buf, bufLength, _attrParseLineNode, loader);
    return loader->svgParse->node;
}


static string* _idFromHref(const char* href)
{
    href = _skipSpace(href, nullptr);
    if ((*href) == '#') href++;
    return new string(href);
}


static SvgNode* _getDefsNode(SvgNode* node)
{
    if (!node) return nullptr;

    while (node->parent != nullptr) {
        node = node->parent;
    }

    if (node->type == SvgNodeType::Doc) return node->node.doc.defs;

    return nullptr;
}


static SvgNode* _findChildById(SvgNode* node, const char* id)
{
    if (!node) return nullptr;

    auto child = node->child.list;
    for (uint32_t i = 0; i < node->child.cnt; ++i, ++child) {
        if (((*child)->id != nullptr) && !strcmp((*child)->id->c_str(), id)) return (*child);
    }
    return nullptr;
}

static SvgNode* _findNodeById(SvgNode *node, string* id)
{
    SvgNode* result = nullptr;
    if (node->id && !node->id->compare(*id)) return node;

    if (node->child.cnt > 0) {
        auto child = node->child.list;
        for (uint32_t i = 0; i < node->child.cnt; ++i, ++child) {
            result = _findNodeById(*child, id);
            if (result) break;
        }
    }
    return result;
}

static void _cloneGradStops(SvgVector<Fill::ColorStop*>* dst, SvgVector<Fill::ColorStop*>* src)
{
    for (uint32_t i = 0; i < src->cnt; ++i) {
        auto stop = static_cast<Fill::ColorStop *>(malloc(sizeof(Fill::ColorStop)));
        *stop = *src->list[i];
        dst->push(stop);
    }
}


static SvgStyleGradient* _cloneGradient(SvgStyleGradient* from)
{
    SvgStyleGradient* grad;

    if (!from) return nullptr;

    grad = (SvgStyleGradient*)calloc(1, sizeof(SvgStyleGradient));
    grad->type = from->type;
    grad->id = from->id ? _copyId(from->id->c_str()) : nullptr;
    grad->ref = from->ref ? _copyId(from->ref->c_str()) : nullptr;
    grad->spread = from->spread;
    grad->usePercentage = from->usePercentage;
    grad->userSpace = from->userSpace;
    if (from->transform) {
        grad->transform = (Matrix*)calloc(1, sizeof(Matrix));
        memcpy(grad->transform, from->transform, sizeof(Matrix));
    }
    if (grad->type == SvgGradientType::Linear) {
        grad->linear = (SvgLinearGradient*)calloc(1, sizeof(SvgLinearGradient));
        memcpy(grad->linear, from->linear, sizeof(SvgLinearGradient));
    } else if (grad->type == SvgGradientType::Radial) {
        grad->radial = (SvgRadialGradient*)calloc(1, sizeof(SvgRadialGradient));
        memcpy(grad->radial, from->radial, sizeof(SvgRadialGradient));
    }

    _cloneGradStops(&grad->stops, &from->stops);
    return grad;
}


static void _copyAttr(SvgNode* to, SvgNode* from)
{
    //Copy matrix attribute
    if (from->transform) {
        to->transform = (Matrix*)calloc(1, sizeof(Matrix));
        memcpy(to->transform, from->transform, sizeof(Matrix));
    }
    //Copy style attribute;
    memcpy(to->style, from->style, sizeof(SvgStyleProperty));

    //Copy node attribute
    switch (from->type) {
        case SvgNodeType::Circle: {
            to->node.circle.cx = from->node.circle.cx;
            to->node.circle.cy = from->node.circle.cy;
            to->node.circle.r = from->node.circle.r;
            break;
        }
        case SvgNodeType::Ellipse: {
            to->node.ellipse.cx = from->node.ellipse.cx;
            to->node.ellipse.cy = from->node.ellipse.cy;
            to->node.ellipse.rx = from->node.ellipse.rx;
            to->node.ellipse.ry = from->node.ellipse.ry;
            break;
        }
        case SvgNodeType::Rect: {
            to->node.rect.x = from->node.rect.x;
            to->node.rect.y = from->node.rect.y;
            to->node.rect.w = from->node.rect.w;
            to->node.rect.h = from->node.rect.h;
            to->node.rect.rx = from->node.rect.rx;
            to->node.rect.ry = from->node.rect.ry;
            to->node.rect.hasRx = from->node.rect.hasRx;
            to->node.rect.hasRy = from->node.rect.hasRy;
            break;
        }
        case SvgNodeType::Line: {
            to->node.line.x1 = from->node.line.x1;
            to->node.line.y1 = from->node.line.y1;
            to->node.line.x2 = from->node.line.x2;
            to->node.line.y2 = from->node.line.y2;
            break;
        }
        case SvgNodeType::Path: {
            to->node.path.path = new string(from->node.path.path->c_str());
            break;
        }
        case SvgNodeType::Polygon: {
            to->node.polygon.pointsCount = from->node.polygon.pointsCount;
            to->node.polygon.points = (float*)malloc(to->node.polygon.pointsCount * sizeof(float));
            memcpy(to->node.polygon.points, from->node.polygon.points, to->node.polygon.pointsCount * sizeof(float));
            break;
        }
        case SvgNodeType::Polyline: {
            to->node.polyline.pointsCount = from->node.polyline.pointsCount;
            to->node.polyline.points = (float*)malloc(to->node.polyline.pointsCount * sizeof(float));
            memcpy(to->node.polyline.points, from->node.polyline.points, to->node.polyline.pointsCount * sizeof(float));
            break;
        }
        default: {
            break;
        }
    }
}


static void _cloneNode(SvgNode* from, SvgNode* parent)
{
    SvgNode* newNode;
    if (!from || !parent) return;

    newNode = _createNode(parent, from->type);
    _copyAttr(newNode, from);

    auto child = from->child.list;
    for (uint32_t i = 0; i < from->child.cnt; ++i, ++child) {
        _cloneNode(*child, newNode);
    }
}


static bool _attrParseUseNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgNode *defs, *nodeFrom, *node = loader->svgParse->node;
    string* id;

    if (!strcmp(key, "xlink:href")) {
        id = _idFromHref(value);
        defs = _getDefsNode(node);
        nodeFrom = _findChildById(defs, id->c_str());
        _cloneNode(nodeFrom, node);
        delete id;
    } else if (!strcmp(key, "clip-path")) {
        _handleClipPathAttr(loader, node, value);
    } else {
        _attrParseGNode(data, key, value);
    }
    return true;
}


static SvgNode* _createUseNode(SvgLoaderData* loader, SvgNode* parent, const char* buf, unsigned bufLength)
{
    loader->svgParse->node = _createNode(parent, SvgNodeType::G);

    simpleXmlParseAttributes(buf, bufLength, _attrParseUseNode, loader);
    return loader->svgParse->node;
}

//TODO: Implement 'text' primitive
static constexpr struct
{
    const char* tag;
    int sz;
    FactoryMethod tagHandler;
} graphicsTags[] = {
    {"use", sizeof("use"), _createUseNode},
    {"circle", sizeof("circle"), _createCircleNode},
    {"ellipse", sizeof("ellipse"), _createEllipseNode},
    {"path", sizeof("path"), _createPathNode},
    {"polygon", sizeof("polygon"), _createPolygonNode},
    {"rect", sizeof("rect"), _createRectNode},
    {"polyline", sizeof("polyline"), _createPolylineNode},
    {"line", sizeof("line"), _createLineNode}
};


static constexpr struct
{
    const char* tag;
    int sz;
    FactoryMethod tagHandler;
} groupTags[] = {
    {"defs", sizeof("defs"), _createDefsNode},
    {"g", sizeof("g"), _createGNode},
    {"svg", sizeof("svg"), _createSvgNode},
    {"mask", sizeof("mask"), _createMaskNode},
    {"clipPath", sizeof("clipPath"), _createClipPathNode}
};


#define FIND_FACTORY(Short_Name, Tags_Array)                                           \
    static FactoryMethod                                                               \
        _find##Short_Name##Factory(const char* name)                                   \
    {                                                                                  \
        unsigned int i;                                                                \
        int sz = strlen(name);                                                         \
                                                                                       \
        for (i = 0; i < sizeof(Tags_Array) / sizeof(Tags_Array[0]); i++) {             \
            if (Tags_Array[i].sz - 1 == sz && !strncmp(Tags_Array[i].tag, name, sz)) { \
                return Tags_Array[i].tagHandler;                                       \
            }                                                                          \
        }                                                                              \
        return nullptr;                                                                \
    }

FIND_FACTORY(Group, groupTags)
FIND_FACTORY(Graphics, graphicsTags)


FillSpread _parseSpreadValue(const char* value)
{
    auto spread = FillSpread::Pad;

    if (!strcmp(value, "reflect")) {
        spread = FillSpread::Reflect;
    } else if (!strcmp(value, "repeat")) {
        spread = FillSpread::Repeat;
    }

    return spread;
}


static void _handleRadialCxAttr(SvgLoaderData* loader, SvgRadialGradient* radial, const char* value)
{
    radial->cx = _gradientToFloat(loader->svgParse, value, SvgParserLengthType::Horizontal);
    if (!loader->svgParse->gradient.parsedFx) radial->fx = radial->cx;
}


static void _handleRadialCyAttr(SvgLoaderData* loader, SvgRadialGradient* radial, const char* value)
{
    radial->cy = _gradientToFloat(loader->svgParse, value, SvgParserLengthType::Vertical);
    if (!loader->svgParse->gradient.parsedFy) radial->fy = radial->cy;
}


static void _handleRadialFxAttr(SvgLoaderData* loader, SvgRadialGradient* radial, const char* value)
{
    radial->fx = _gradientToFloat(loader->svgParse, value, SvgParserLengthType::Horizontal);
    loader->svgParse->gradient.parsedFx = true;
}


static void _handleRadialFyAttr(SvgLoaderData* loader, SvgRadialGradient* radial, const char* value)
{
    radial->fy = _gradientToFloat(loader->svgParse, value, SvgParserLengthType::Vertical);
    loader->svgParse->gradient.parsedFy = true;
}


static void _handleRadialRAttr(SvgLoaderData* loader, SvgRadialGradient* radial, const char* value)
{
    radial->r = _gradientToFloat(loader->svgParse, value, SvgParserLengthType::Other);
}


static void _recalcRadialCxAttr(SvgLoaderData* loader, SvgRadialGradient* radial, bool userSpace)
{
    if (!userSpace) radial->cx = radial->cx * loader->svgParse->global.w;
}


static void _recalcRadialCyAttr(SvgLoaderData* loader, SvgRadialGradient* radial, bool userSpace)
{
    if (!userSpace) radial->cy = radial->cy * loader->svgParse->global.h;
}


static void _recalcRadialFxAttr(SvgLoaderData* loader, SvgRadialGradient* radial, bool userSpace)
{
    if (!userSpace) radial->fx = radial->fx * loader->svgParse->global.w;
}


static void _recalcRadialFyAttr(SvgLoaderData* loader, SvgRadialGradient* radial, bool userSpace)
{
    if (!userSpace) radial->fy = radial->fy * loader->svgParse->global.h;
}


static void _recalcRadialRAttr(SvgLoaderData* loader, SvgRadialGradient* radial, bool userSpace)
{
    if (!userSpace) radial->r = radial->r * (sqrt(pow(loader->svgParse->global.h, 2) + pow(loader->svgParse->global.w, 2)) / sqrt(2.0));
}


typedef void (*radialMethod)(SvgLoaderData* loader, SvgRadialGradient* radial, const char* value);
typedef void (*radialMethodRecalc)(SvgLoaderData* loader, SvgRadialGradient* radial, bool userSpace);


#define RADIAL_DEF(Name, Name1)                                                          \
    {                                                                                    \
#Name, sizeof(#Name), _handleRadial##Name1##Attr, _recalcRadial##Name1##Attr             \
    }


static constexpr struct
{
    const char* tag;
    int sz;
    radialMethod tagHandler;
    radialMethodRecalc tagRecalc;
} radialTags[] = {
    RADIAL_DEF(cx, Cx),
    RADIAL_DEF(cy, Cy),
    RADIAL_DEF(fx, Fx),
    RADIAL_DEF(fy, Fy),
    RADIAL_DEF(r, R)
};


static bool _attrParseRadialGradientNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgStyleGradient* grad = loader->svgParse->styleGrad;
    SvgRadialGradient* radial = grad->radial;
    unsigned int i;
    int sz = strlen(key);

    for (i = 0; i < sizeof(radialTags) / sizeof(radialTags[0]); i++) {
        if (radialTags[i].sz - 1 == sz && !strncmp(radialTags[i].tag, key, sz)) {
            radialTags[i].tagHandler(loader, radial, value);
            return true;
        }
    }

    if (!strcmp(key, "id")) {
        grad->id = _copyId(value);
    } else if (!strcmp(key, "spreadMethod")) {
        grad->spread = _parseSpreadValue(value);
    } else if (!strcmp(key, "xlink:href")) {
        grad->ref = _idFromHref(value);
    } else if (!strcmp(key, "gradientUnits") && !strcmp(value, "userSpaceOnUse")) {
        grad->userSpace = true;
    }

    return true;
}


static SvgStyleGradient* _createRadialGradient(SvgLoaderData* loader, const char* buf, unsigned bufLength)
{
    unsigned int i = 0;
    SvgStyleGradient* grad = (SvgStyleGradient*)calloc(1, sizeof(SvgStyleGradient));
    loader->svgParse->styleGrad = grad;

    grad->type = SvgGradientType::Radial;
    grad->userSpace = false;
    grad->radial = (SvgRadialGradient*)calloc(1, sizeof(SvgRadialGradient));
    /**
    * Default values of gradient
    */
    grad->radial->cx = 0.5;
    grad->radial->cy = 0.5;
    grad->radial->fx = 0.5;
    grad->radial->fy = 0.5;
    grad->radial->r = 0.5;

    loader->svgParse->gradient.parsedFx = false;
    loader->svgParse->gradient.parsedFy = false;
    simpleXmlParseAttributes(buf, bufLength,
        _attrParseRadialGradientNode, loader);

    for (i = 0; i < sizeof(radialTags) / sizeof(radialTags[0]); i++) {
        radialTags[i].tagRecalc(loader, grad->radial, grad->userSpace);
    }

    grad->usePercentage = true;

    return loader->svgParse->styleGrad;
}


static bool _attrParseStops(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    auto stop = loader->svgParse->gradStop;

    if (!strcmp(key, "offset")) {
        stop->offset = _toOffset(value);
    } else if (!strcmp(key, "stop-opacity")) {
        stop->a = _toOpacity(value);
    } else if (!strcmp(key, "stop-color")) {
        _toColor(value, &stop->r, &stop->g, &stop->b, nullptr);
    } else if (!strcmp(key, "style")) {
        simpleXmlParseW3CAttribute(value,
            _attrParseStops, data);
    }

    return true;
}


static void _handleLinearX1Attr(SvgLoaderData* loader, SvgLinearGradient* linear, const char* value)
{
    linear->x1 = _gradientToFloat(loader->svgParse, value, SvgParserLengthType::Horizontal);
}


static void _handleLinearY1Attr(SvgLoaderData* loader, SvgLinearGradient* linear, const char* value)
{
    linear->y1 = _gradientToFloat(loader->svgParse, value, SvgParserLengthType::Vertical);
}


static void _handleLinearX2Attr(SvgLoaderData* loader, SvgLinearGradient* linear, const char* value)
{
    linear->x2 = _gradientToFloat(loader->svgParse, value, SvgParserLengthType::Horizontal);
}


static void _handleLinearY2Attr(SvgLoaderData* loader, SvgLinearGradient* linear, const char* value)
{
    linear->y2 = _gradientToFloat(loader->svgParse, value, SvgParserLengthType::Vertical);
}


static void _recalcLinearX1Attr(SvgLoaderData* loader, SvgLinearGradient* linear, bool userSpace)
{
    if (!userSpace) linear->x1 = linear->x1 * loader->svgParse->global.w;
}


static void _recalcLinearY1Attr(SvgLoaderData* loader, SvgLinearGradient* linear, bool userSpace)
{
    if (!userSpace) linear->y1 = linear->y1 * loader->svgParse->global.h;
}


static void _recalcLinearX2Attr(SvgLoaderData* loader, SvgLinearGradient* linear, bool userSpace)
{
    if (!userSpace) linear->x2 = linear->x2 * loader->svgParse->global.w;
}


static void _recalcLinearY2Attr(SvgLoaderData* loader, SvgLinearGradient* linear, bool userSpace)
{
    if (!userSpace) linear->y2 = linear->y2 * loader->svgParse->global.h;
}


typedef void (*Linear_Method)(SvgLoaderData* loader, SvgLinearGradient* linear, const char* value);
typedef void (*Linear_Method_Recalc)(SvgLoaderData* loader, SvgLinearGradient* linear, bool userSpace);


#define LINEAR_DEF(Name, Name1)                                                          \
    {                                                                                    \
#Name, sizeof(#Name), _handleLinear##Name1##Attr, _recalcLinear##Name1##Attr \
    }


static constexpr struct
{
    const char* tag;
    int sz;
    Linear_Method tagHandler;
    Linear_Method_Recalc tagRecalc;
} linear_tags[] = {
    LINEAR_DEF(x1, X1),
    LINEAR_DEF(y1, Y1),
    LINEAR_DEF(x2, X2),
    LINEAR_DEF(y2, Y2)
};


static bool _attrParseLinearGradientNode(void* data, const char* key, const char* value)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    SvgStyleGradient* grad = loader->svgParse->styleGrad;
    SvgLinearGradient* linear = grad->linear;
    unsigned int i;
    int sz = strlen(key);

    for (i = 0; i < sizeof(linear_tags) / sizeof(linear_tags[0]); i++) {
        if (linear_tags[i].sz - 1 == sz && !strncmp(linear_tags[i].tag, key, sz)) {
            linear_tags[i].tagHandler(loader, linear, value);
            return true;
        }
    }

    if (!strcmp(key, "id")) {
        grad->id = _copyId(value);
    } else if (!strcmp(key, "spreadMethod")) {
        grad->spread = _parseSpreadValue(value);
    } else if (!strcmp(key, "xlink:href")) {
        grad->ref = _idFromHref(value);
    } else if (!strcmp(key, "gradientUnits") && !strcmp(value, "userSpaceOnUse")) {
        grad->userSpace = true;
    } else if (!strcmp(key, "gradientTransform")) {
        grad->transform = _parseTransformationMatrix(value);
    }

    return true;
}


static SvgStyleGradient* _createLinearGradient(SvgLoaderData* loader, const char* buf, unsigned bufLength)
{
    SvgStyleGradient* grad = (SvgStyleGradient*)calloc(1, sizeof(SvgStyleGradient));
    loader->svgParse->styleGrad = grad;
    unsigned int i;

    grad->type = SvgGradientType::Linear;
    grad->userSpace = false;
    grad->linear = (SvgLinearGradient*)calloc(1, sizeof(SvgLinearGradient));
    /**
    * Default value of x2 is 100%
    */
    grad->linear->x2 = 1;
    simpleXmlParseAttributes(buf, bufLength, _attrParseLinearGradientNode, loader);

    for (i = 0; i < sizeof(linear_tags) / sizeof(linear_tags[0]); i++) {
        linear_tags[i].tagRecalc(loader, grad->linear, grad->userSpace);
    }

    grad->usePercentage = true;

    return loader->svgParse->styleGrad;
}


#define GRADIENT_DEF(Name, Name1)            \
    {                                        \
#Name, sizeof(#Name), _create##Name1         \
    }


/**
 * For all Gradients lengths would be calculated into percentages related to
 * canvas width and height.
 *
 * if user then recalculate actual pixels into percentages
 */
static constexpr struct
{
    const char* tag;
    int sz;
    GradientFactoryMethod tagHandler;
} gradientTags[] = {
    GRADIENT_DEF(linearGradient, LinearGradient),
    GRADIENT_DEF(radialGradient, RadialGradient)
};


static GradientFactoryMethod _findGradientFactory(const char* name)
{
    unsigned int i;
    int sz = strlen(name);

    for (i = 0; i < sizeof(gradientTags) / sizeof(gradientTags[0]); i++) {
        if (gradientTags[i].sz - 1 == sz && !strncmp(gradientTags[i].tag, name, sz)) {
            return gradientTags[i].tagHandler;
        }
    }
    return nullptr;
}


static constexpr struct
{
    const char* tag;
    size_t sz;
} popArray[] = {
    {"g", sizeof("g")},
    {"svg", sizeof("svg")},
    {"defs", sizeof("defs")},
    {"mask", sizeof("mask")},
    {"clipPath", sizeof("clipPath")}
};


static void _svgLoaderParerXmlClose(SvgLoaderData* loader, const char* content)
{
    unsigned int i;

    content = _skipSpace(content, nullptr);

    for (i = 0; i < sizeof(popArray) / sizeof(popArray[0]); i++) {
        if (!strncmp(content, popArray[i].tag, popArray[i].sz - 1)) {
            loader->stack.pop();
            break;
        }
    }

    loader->level--;
}


static void _svgLoaderParserXmlOpen(SvgLoaderData* loader, const char* content, unsigned int length, bool empty)
{
    const char* attrs = nullptr;
    int attrsLength = 0;
    int sz = length;
    char tagName[20] = "";
    FactoryMethod method;
    GradientFactoryMethod gradientMethod;
    SvgNode *node = nullptr, *parent = nullptr;
    loader->level++;
    attrs = simpleXmlFindAttributesTag(content, length);

    if (!attrs) {
        //Parse the empty tag
        attrs = content;
        while ((attrs != nullptr) && *attrs != '>') attrs++;
    }

    if (attrs) {
        //Find out the tag name starting from content till sz length
        sz = attrs - content;
        attrsLength = length - sz;
        while ((sz > 0) && (isspace(content[sz - 1]))) sz--;
        if ((unsigned int)sz > sizeof(tagName)) return;
        strncpy(tagName, content, sz);
        tagName[sz] = '\0';
    }

    if ((method = _findGroupFactory(tagName))) {
        //Group
        if (!loader->doc) {
            if (strcmp(tagName, "svg")) return; //Not a valid svg document
            node = method(loader, nullptr, attrs, attrsLength);
            loader->doc = node;
        } else {
            if (!strcmp(tagName, "svg")) return; //Already loadded <svg>(SvgNodeType::Doc) tag
            if (loader->stack.cnt > 0) parent = loader->stack.list[loader->stack.cnt - 1];
            else parent = loader->doc;
            node = method(loader, parent, attrs, attrsLength);
        }

        if (node->type == SvgNodeType::Defs) {
            loader->doc->node.doc.defs = node;
            loader->def = node;
            if (!empty) loader->stack.push(node);
        } else {
            loader->stack.push(node);
        }
    } else if ((method = _findGraphicsFactory(tagName))) {
        if (loader->stack.cnt > 0) parent = loader->stack.list[loader->stack.cnt - 1];
        else parent = loader->doc;
        node = method(loader, parent, attrs, attrsLength);
    } else if ((gradientMethod = _findGradientFactory(tagName))) {
        SvgStyleGradient* gradient;
        gradient = gradientMethod(loader, attrs, attrsLength);
        //FIXME: The current parsing structure does not distinguish end tags.
        //       There is no way to know if the currently parsed gradient is in defs.
        //       If a gradient is declared outside of defs after defs is set, it is included in the gradients of defs.
        //       But finally, the loader has a gradient style list regardless of defs.
        //       This is only to support this when multiple gradients are declared, even if no defs are declared.
        //       refer to: https://developer.mozilla.org/en-US/docs/Web/SVG/Element/defs
        if (loader->def && loader->doc->node.doc.defs) {
            loader->def->node.defs.gradients.push(gradient);
        } else {
            loader->gradients.push(gradient);
        }
        loader->latestGradient = gradient;
    } else if (!strcmp(tagName, "stop")) {
        auto stop = static_cast<Fill::ColorStop*>(calloc(1, sizeof(Fill::ColorStop)));
        loader->svgParse->gradStop = stop;
        /* default value for opacity */
        stop->a = 255;
        simpleXmlParseAttributes(attrs, attrsLength, _attrParseStops, loader);
        if (loader->latestGradient) {
            loader->latestGradient->stops.push(stop);
        }
    }
}


static bool _svgLoaderParser(void* data, SimpleXMLType type, const char* content, unsigned int length)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;

    switch (type) {
        case SimpleXMLType::Open: {
            _svgLoaderParserXmlOpen(loader, content, length, false);
            break;
        }
        case SimpleXMLType::OpenEmpty: {
            _svgLoaderParserXmlOpen(loader, content, length, true);
            break;
        }
        case SimpleXMLType::Close: {
            _svgLoaderParerXmlClose(loader, content);
            break;
        }
        case SimpleXMLType::Data:
        case SimpleXMLType::CData:
        case SimpleXMLType::DoctypeChild: {
            break;
        }
        case SimpleXMLType::Ignored:
        case SimpleXMLType::Comment:
        case SimpleXMLType::Doctype: {
            break;
        }
        default: {
            break;
        }
    }

    return true;
}


static void _styleInherit(SvgStyleProperty* child, SvgStyleProperty* parent)
{
    if (parent == nullptr) return;
    //Inherit the property of parent if not present in child.
    //Fill
    if (!((int)child->fill.flags & (int)SvgFillFlags::Paint)) {
        child->fill.paint.r = parent->fill.paint.r;
        child->fill.paint.g = parent->fill.paint.g;
        child->fill.paint.b = parent->fill.paint.b;
        child->fill.paint.none = parent->fill.paint.none;
        child->fill.paint.curColor = parent->fill.paint.curColor;
        if (parent->fill.paint.url) child->fill.paint.url = _copyId(parent->fill.paint.url->c_str());
    }
    if (!((int)child->fill.flags & (int)SvgFillFlags::Opacity)) {
        child->fill.opacity = parent->fill.opacity;
    }
    if (!((int)child->fill.flags & (int)SvgFillFlags::FillRule)) {
        child->fill.fillRule = parent->fill.fillRule;
    }
    //Stroke
    if (!((int)child->stroke.flags & (int)SvgStrokeFlags::Paint)) {
        child->stroke.paint.r = parent->stroke.paint.r;
        child->stroke.paint.g = parent->stroke.paint.g;
        child->stroke.paint.b = parent->stroke.paint.b;
        child->stroke.paint.none = parent->stroke.paint.none;
        child->stroke.paint.curColor = parent->stroke.paint.curColor;
        child->stroke.paint.url = parent->stroke.paint.url ? _copyId(parent->stroke.paint.url->c_str()) : nullptr;
    }
    if (!((int)child->stroke.flags & (int)SvgStrokeFlags::Opacity)) {
        child->stroke.opacity = parent->stroke.opacity;
    }
    if (!((int)child->stroke.flags & (int)SvgStrokeFlags::Width)) {
        child->stroke.width = parent->stroke.width;
    }
    if (!((int)child->stroke.flags & (int)SvgStrokeFlags::Dash)) {
        if (parent->stroke.dash.array.cnt > 0) {
            child->stroke.dash.array.clear();
            for (uint32_t i = 0; i < parent->stroke.dash.array.cnt; ++i) {
                child->stroke.dash.array.push(parent->stroke.dash.array.list[i]);
            }
        }
    }
    if (!((int)child->stroke.flags & (int)SvgStrokeFlags::Cap)) {
        child->stroke.cap = parent->stroke.cap;
    }
    if (!((int)child->stroke.flags & (int)SvgStrokeFlags::Join)) {
        child->stroke.join = parent->stroke.join;
    }
}


static void _updateStyle(SvgNode* node, SvgStyleProperty* parentStyle)
{
    _styleInherit(node->style, parentStyle);

    auto child = node->child.list;
    for (uint32_t i = 0; i < node->child.cnt; ++i, ++child) {
        _updateStyle(*child, node->style);
    }
}


static SvgStyleGradient* _gradientDup(SvgVector<SvgStyleGradient*>* gradients, string* id)
{
    SvgStyleGradient* result = nullptr;

    auto gradList = gradients->list;

    for (uint32_t i = 0; i < gradients->cnt; ++i) {
        if (!((*gradList)->id->compare(*id))) {
            result = _cloneGradient(*gradList);
            break;
        }
        ++gradList;
    }

    if (result && result->ref) {
        gradList = gradients->list;
        for (uint32_t i = 0; i < gradients->cnt; ++i) {
            if (!((*gradList)->id->compare(*result->ref))) {
                if (result->stops.cnt > 0) {
                    _cloneGradStops(&result->stops, &(*gradList)->stops);
                }
                //TODO: Properly inherit other property
                break;
            }
            ++gradList;
        }
    }

    return result;
}


static void _updateGradient(SvgNode* node, SvgVector<SvgStyleGradient*>* gradidents)
{
    if (node->child.cnt > 0) {
        auto child = node->child.list;
        for (uint32_t i = 0; i < node->child.cnt; ++i, ++child) {
            _updateGradient(*child, gradidents);
        }
    } else {
        if (node->style->fill.paint.url) {
            node->style->fill.paint.gradient = _gradientDup(gradidents, node->style->fill.paint.url);
        } else if (node->style->stroke.paint.url) {
            //node->style->stroke.paint.gradient = _gradientDup(gradList, node->style->stroke.paint.url);
        }
    }
}

static void _updateComposite(SvgNode* node, SvgNode* root)
{
    if (node->style->comp.url && !node->style->comp.node) {
        SvgNode *findResult = _findNodeById(root, node->style->comp.url);
        if (findResult) node->style->comp.node = findResult;
    }
    if (node->child.cnt > 0) {
        auto child = node->child.list;
        for (uint32_t i = 0; i < node->child.cnt; ++i, ++child) {
            _updateComposite(*child, root);
        }
    }
}

static void _freeGradientStyle(SvgStyleGradient* grad)
{
    if (!grad) return;

    delete grad->id;
    delete grad->ref;
    free(grad->radial);
    free(grad->linear);
    if (grad->transform) free(grad->transform);

    for (uint32_t i = 0; i < grad->stops.cnt; ++i) {
        auto colorStop = grad->stops.list[i];
        free(colorStop);
    }
    grad->stops.clear();
    free(grad);
}

static void _freeNodeStyle(SvgStyleProperty* style)
{
    if (!style) return;

    _freeGradientStyle(style->fill.paint.gradient);
    delete style->fill.paint.url;
    _freeGradientStyle(style->stroke.paint.gradient);
    if (style->stroke.dash.array.cnt > 0) style->stroke.dash.array.clear();
    delete style->stroke.paint.url;
    free(style);
}

static void _freeNode(SvgNode* node)
{
    if (!node) return;

    auto child = node->child.list;
    for (uint32_t i = 0; i < node->child.cnt; ++i, ++child) {
        _freeNode(*child);
    }
    node->child.clear();

    delete node->id;
    free(node->transform);
    _freeNodeStyle(node->style);
    switch (node->type) {
         case SvgNodeType::Path: {
             delete node->node.path.path;
             break;
         }
         case SvgNodeType::Polygon: {
             free(node->node.polygon.points);
             break;
         }
         case SvgNodeType::Polyline: {
             free(node->node.polyline.points);
             break;
         }
         case SvgNodeType::Doc: {
             _freeNode(node->node.doc.defs);
             break;
         }
         case SvgNodeType::Defs: {
             auto gradients = node->node.defs.gradients.list;
             for (size_t i = 0; i < node->node.defs.gradients.cnt; ++i) {
                 _freeGradientStyle(*gradients);
                 ++gradients;
             }
             node->node.defs.gradients.clear();
             break;
         }
         default: {
             break;
         }
    }
    free(node);
}


static bool _svgLoaderParserForValidCheckXmlOpen(SvgLoaderData* loader, const char* content, unsigned int length)
{
    const char* attrs = nullptr;
    int sz = length;
    char tagName[20] = "";
    FactoryMethod method;
    SvgNode *node = nullptr;
    int attrsLength = 0;
    loader->level++;
    attrs = simpleXmlFindAttributesTag(content, length);

    if (!attrs) {
        //Parse the empty tag
        attrs = content;
        while ((attrs != nullptr) && *attrs != '>') attrs++;
    }

    if (attrs) {
        sz = attrs - content;
        attrsLength = length - sz;
        while ((sz > 0) && (isspace(content[sz - 1]))) sz--;
        if ((unsigned int)sz > sizeof(tagName)) return false;
        strncpy(tagName, content, sz);
        tagName[sz] = '\0';
    }

    if ((method = _findGroupFactory(tagName))) {
        if (!loader->doc) {
            if (strcmp(tagName, "svg")) return true; //Not a valid svg document
            node = method(loader, nullptr, attrs, attrsLength);
            loader->doc = node;
            loader->stack.push(node);
            return false;
        }
    }
    return true;
}


static bool _svgLoaderParserForValidCheck(void* data, SimpleXMLType type, const char* content, unsigned int length)
{
    SvgLoaderData* loader = (SvgLoaderData*)data;
    bool res = true;;

    switch (type) {
        case SimpleXMLType::Open:
        case SimpleXMLType::OpenEmpty: {
            //If 'res' is false, it means <svg> tag is found.
            res = _svgLoaderParserForValidCheckXmlOpen(loader, content, length);
            break;
        }
        default: {
            break;
        }
    }

    return res;
}


/************************************************************************/
/* External Class Implementation                                        */
/************************************************************************/

SvgLoader::SvgLoader()
{

}


SvgLoader::~SvgLoader()
{
    close();
}


void SvgLoader::run(unsigned tid)
{
    if (!simpleXmlParse(content, size, true, _svgLoaderParser, &(loaderData))) return;

    if (loaderData.doc) {
        _updateStyle(loaderData.doc, nullptr);
        auto defs = loaderData.doc->node.doc.defs;
        if (defs) _updateGradient(loaderData.doc, &defs->node.defs.gradients);

        if (loaderData.gradients.cnt > 0) _updateGradient(loaderData.doc, &loaderData.gradients);

        _updateComposite(loaderData.doc, loaderData.doc);
        if (defs) _updateComposite(loaderData.doc, defs);
    }
    root = builder.build(loaderData.doc);
};


bool SvgLoader::header()
{
    //For valid check, only <svg> tag is parsed first.
    //If the <svg> tag is found, the loaded file is valid and stores viewbox information.
    //After that, the remaining content data is parsed in order with async.
    loaderData.svgParse = (SvgParser*)malloc(sizeof(SvgParser));
    if (!loaderData.svgParse) return false;

    simpleXmlParse(content, size, true, _svgLoaderParserForValidCheck, &(loaderData));

    if (loaderData.doc && loaderData.doc->type == SvgNodeType::Doc) {
        //Return the brief resource info such as viewbox:
        vx = loaderData.doc->node.doc.vx;
        vy = loaderData.doc->node.doc.vy;
        vw = loaderData.doc->node.doc.vw;
        vh = loaderData.doc->node.doc.vh;

        w = loaderData.doc->node.doc.w;
        h = loaderData.doc->node.doc.h;

        preserveAspect = loaderData.doc->node.doc.preserveAspect;
    } else {
        //LOG: No SVG File. There is no <svg/>
        return false;
    }

    return true;
}


bool SvgLoader::open(const char* data, uint32_t size)
{
    this->content = data;
    this->size = size;

    return header();
}


bool SvgLoader::open(const string& path)
{
    ifstream f;
    f.open(path);

    if (!f.is_open())
    {
        //LOG: Failed to open file
        return false;
    } else {
        getline(f, filePath, '\0');
        f.close();

        if (filePath.empty()) return false;

        this->content = filePath.c_str();
        this->size = filePath.size();
    }

    return header();
}


bool SvgLoader::read()
{
    if (!content || size == 0) return false;

    TaskScheduler::request(this);

    return true;
}


bool SvgLoader::close()
{
    this->done();

    if (loaderData.svgParse) {
        free(loaderData.svgParse);
        loaderData.svgParse = nullptr;
    }
    auto gradients = loaderData.gradients.list;
    for (size_t i = 0; i < loaderData.gradients.cnt; ++i) {
        _freeGradientStyle(*gradients);
        ++gradients;
    }
    loaderData.gradients.clear();

    _freeNode(loaderData.doc);
    loaderData.doc = nullptr;
    loaderData.stack.clear();

    return true;
}


unique_ptr<Scene> SvgLoader::scene()
{
    this->done();
    if (root) return move(root);
    else return nullptr;
}

