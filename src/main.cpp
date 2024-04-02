#include "app_sdl2.h"
#include "core/debug.h"

int main() {
    wb::AppSDL2 app;
    app.init();
    app.run();
    return 0;
}