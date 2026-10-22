#pragma once

// Include once, in the file that defines the game's entry point.

#include <SDL3/SDL_main.h>

#define ANIMAL_MAIN(AppType)               \
    int main(int argc, char** argv) {      \
        AppType app;                       \
        return app.run(argc, argv);        \
    }
