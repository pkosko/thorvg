#include "Common.h"

/************************************************************************/
/* Drawing Commands                                                     */
/************************************************************************/

void tvgDrawCmds(tvg::Canvas* canvas) {
  if (!canvas) return;

  canvas->reserve(5);  // reserve 5 shape nodes (optional)

  // Prepare Shape1
  auto shape1 = tvg::Shape::gen();
  shape1->appendRect(-100, -100, 1000, 1000, 50, 50);
  shape1->fill(255, 255, 255, 255);
  if (canvas->push(move(shape1)) != tvg::Result::Success) return;

  // Prepare Shape2
  auto shape2 = tvg::Shape::gen();
  shape2->appendRect(-100, -100, 250, 250, 50, 50);
  shape2->fill(0, 0, 255, 255);
  if (canvas->push(move(shape2)) != tvg::Result::Success) return;

  // Prepare Shape3
  auto shape3 = tvg::Shape::gen();
  shape3->appendRect(500, 500, 550, 550, 0, 0);
  shape3->fill(0, 255, 255, 255);
  if (canvas->push(move(shape3)) != tvg::Result::Success) return;

  // Prepare Shape4
  auto shape4 = tvg::Shape::gen();
  shape4->appendCircle(800, 100, 200, 200);
  shape4->fill(255, 255, 0, 255);
  if (canvas->push(move(shape4)) != tvg::Result::Success) return;

  // Prepare Shape5
  auto shape5 = tvg::Shape::gen();
  shape5->appendCircle(200, 650, 250, 200);
  shape5->fill(0, 0, 0, 255);
  if (canvas->push(move(shape5)) != tvg::Result::Success) return;
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
