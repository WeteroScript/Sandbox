#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "SandboxGame.hpp"
#include <cstdio>

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    SandboxGame game;
    if (!game.initialize())
    {
        std::fprintf(stderr, "Sandbox initialization failed: %s\\n", SDL_GetError());
        return 1;
    }
    return game.run();
}
