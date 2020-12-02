#include "Common.h"

/************************************************************************/
/* Drawing Commands                                                     */
/************************************************************************/
tvg::Shape* pShape = nullptr;
tvg::Shape* pShape2 = nullptr;
tvg::Shape* pShape3 = nullptr;

void tvgDrawCmds(tvg::Canvas* canvas) {
  if (!canvas) return;

  // Shape1
  auto shape = tvg::Shape::gen();

  /* Acquire shape pointer to access it again.
     instead, you should consider not to interrupt this pointer life-cycle. */
  pShape = shape.get();

  shape->appendRect(-285, -300, 200, 200, 0, 0);
  shape->appendRect(-185, -200, 300, 300, 100, 100);
  shape->appendCircle(115, 100, 100, 100);
  shape->appendCircle(115, 200, 170, 100);

  // LinearGradient
  auto fill = tvg::LinearGradient::gen();
  fill->linear(-285, -300, 285, 300);

  // Gradient Color Stops
  tvg::Fill::ColorStop colorStops[3];
  colorStops[0] = {0, 255, 0, 0, 255};
  colorStops[1] = {0.5, 255, 255, 0, 255};
  colorStops[2] = {1, 255, 255, 255, 255};

  fill->colorStops(colorStops, 3);
  shape->fill(move(fill));
  shape->translate(385, 400);
  if (canvas->push(move(shape)) != tvg::Result::Success) return;

  // Shape2
  auto shape2 = tvg::Shape::gen();
  pShape2 = shape2.get();
  shape2->appendRect(-50, -50, 100, 100, 0, 0);
  shape2->translate(400, 400);

  // LinearGradient
  auto fill2 = tvg::LinearGradient::gen();
  fill2->linear(-50, -50, 50, 50);

  // Gradient Color Stops
  tvg::Fill::ColorStop colorStops2[2];
  colorStops2[0] = {0, 0, 0, 0, 255};
  colorStops2[1] = {1, 255, 255, 255, 255};

  fill2->colorStops(colorStops2, 2);
  shape2->fill(move(fill2));
  if (canvas->push(move(shape2)) != tvg::Result::Success) return;

  // Shape3
  auto shape3 = tvg::Shape::gen();
  pShape3 = shape3.get();

  /* Look, how shape3's origin is different with shape2
     The center of the shape is the anchor point for transformation. */
  shape3->appendRect(100, 100, 150, 100, 20, 20);

  // RadialGradient
  auto fill3 = tvg::RadialGradient::gen();
  fill3->radial(175, 150, 75);

  // Gradient Color Stops
  tvg::Fill::ColorStop colorStops3[4];
  colorStops3[0] = {0, 0, 127, 0, 127};
  colorStops3[1] = {0.25, 0, 170, 170, 170};
  colorStops3[2] = {0.5, 200, 0, 200, 200};
  colorStops3[3] = {1, 255, 255, 255, 255};

  fill3->colorStops(colorStops3, 4);

  shape3->fill(move(fill3));
  shape3->translate(400, 400);
  if (canvas->push(move(shape3)) != tvg::Result::Success) return;
}

void tvgUpdateCmds(tvg::Canvas* canvas, float progress) {
  if (!canvas) return;

  /* Update shape directly.
     You can update only necessary properties of this shape,
     while retaining other properties. */

  // Update Shape1
  pShape->scale(1 - 0.75 * progress);
  pShape->rotate(360 * progress);

  // Update shape for drawing (this may work asynchronously)
  if (canvas->update(pShape) != tvg::Result::Success) return;

  // Update Shape2
  pShape2->rotate(360 * progress);
  pShape2->translate(400 + progress * 300, 400);
  if (canvas->update(pShape2) != tvg::Result::Success) return;

  // Update Shape3
  pShape3->rotate(-360 * progress);
  pShape3->scale(0.5 + progress);
  if (canvas->update(pShape3) != tvg::Result::Success) return;
}

/************************************************************************/
/* Sw Engine Test Code                                                  */
/************************************************************************/

static unique_ptr<tvg::SwCanvas> swCanvas;

void tvgSwTest(uint32_t* buffer) {
  // Create a Canvas
  swCanvas = tvg::SwCanvas::gen();
  swCanvas->target(buffer, WIDTH, WIDTH, HEIGHT, tvg::SwCanvas::ARGB8888);

  /* Push the shape into the Canvas drawing list
     When this shape is into the canvas list, the shape could update & prepare
     internal data asynchronously for coming rendering.
     Canvas keeps this shape node unless user call canvas->clear() */
  tvgDrawCmds(swCanvas.get());
}

void transitSwCb(Elm_Transit_Effect* effect, Elm_Transit* transit, double progress) {
  tvgUpdateCmds(swCanvas.get(), progress);

  // Update Efl Canvas
  Eo* img = (Eo*)effect;
  evas_object_image_data_update_add(img, 0, 0, WIDTH, HEIGHT);
  evas_object_image_pixels_dirty_set(img, EINA_TRUE);
}

void drawSwView(void* data, Eo* obj) {
  if (swCanvas->draw() == tvg::Result::Success) {
    swCanvas->sync();
  }
}

/************************************************************************/
/* GL Engine Test Code                                                  */
/************************************************************************/

static unique_ptr<tvg::GlCanvas> glCanvas;

void initGLview(Evas_Object* obj) {
  static constexpr auto BPP = 4;

  // Create a Canvas
  glCanvas = tvg::GlCanvas::gen();
  glCanvas->target(nullptr, WIDTH * BPP, WIDTH, HEIGHT);

  /* Push the shape into the Canvas drawing list
     When this shape is into the canvas list, the shape could update & prepare
     internal data asynchronously for coming rendering.
     Canvas keeps this shape node unless user call canvas->clear() */
  tvgDrawCmds(glCanvas.get());
}

void drawGLview(Evas_Object* obj) {
  auto gl = elm_glview_gl_api_get(obj);
  gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  gl->glClear(GL_COLOR_BUFFER_BIT);

  if (glCanvas->draw() == tvg::Result::Success) {
    glCanvas->sync();
  }
}

void transitGlCb(Elm_Transit_Effect* effect, Elm_Transit* transit, double progress) {
  tvgUpdateCmds(glCanvas.get(), progress);
}

/************************************************************************/
/* Main Code                                                            */
/************************************************************************/

int main(int argc, char** argv) {
  tvg::CanvasEngine tvgEngine = tvg::CanvasEngine::Sw;

  if (argc > 1) {
    if (!strcmp(argv[1], "gl")) tvgEngine = tvg::CanvasEngine::Gl;
  }

  // Initialize ThorVG Engine
  if (tvgEngine == tvg::CanvasEngine::Sw) {
    cout << "tvg engine: software" << endl;
  } else {
    cout << "tvg engine: opengl" << endl;
  }

  // Threads Count
  auto threads = std::thread::hardware_concurrency();

  // Initialize ThorVG Engine
  if (tvg::Initializer::init(tvgEngine, threads) == tvg::Result::Success) {
    elm_init(argc, argv);

    Elm_Transit* transit = elm_transit_add();

    if (tvgEngine == tvg::CanvasEngine::Sw) {
      auto view = createSwView();
      elm_transit_effect_add(transit, transitSwCb, view, nullptr);
    } else {
      auto view = createGlView();
      elm_transit_effect_add(transit, transitGlCb, view, nullptr);
    }

    elm_transit_duration_set(transit, 2);
    elm_transit_repeat_times_set(transit, -1);
    elm_transit_auto_reverse_set(transit, EINA_TRUE);
    elm_transit_go(transit);

    elm_run();
    elm_shutdown();

    // Terminate ThorVG Engine
    tvg::Initializer::term(tvgEngine);

  } else {
    cout << "engine is not supported" << endl;
  }
  return 0;
}
