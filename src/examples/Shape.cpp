#include "Common.h"

/************************************************************************/
/* Drawing Commands                                                     */
/************************************************************************/

void tvgDrawCmds(tvg::Canvas* canvas) {
  if (!canvas) return;

  // Prepare a Shape (Rectangle + Rectangle + Circle + Circle)
  auto shape1 = tvg::Shape::gen();
  shape1->appendRect(0, 0, 200, 200, 0, 0);          // x, y, w, h, rx, ry
  shape1->appendRect(100, 100, 300, 300, 100, 100);  // x, y, w, h, rx, ry
  shape1->appendCircle(400, 400, 100, 100);          // cx, cy, radiusW, radiusH
  shape1->appendCircle(400, 500, 170, 100);          // cx, cy, radiusW, radiusH
  shape1->fill(255, 255, 0, 255);                    // r, g, b, a

  canvas->push(move(shape1));
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

    if (tvgEngine == tvg::CanvasEngine::Sw) {
      createSwView();
    } else {
      createGlView();
    }

    elm_run();
    elm_shutdown();

    // Terminate ThorVG Engine
    tvg::Initializer::term(tvgEngine);

  } else {
    cout << "engine is not supported" << endl;
  }
  return 0;
}
