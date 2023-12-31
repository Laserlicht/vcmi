/*
 * ScreenFilter.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#ifdef __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#include <OpenGL/OpenGL.h>
#if ESSENTIAL_GL_PRACTICES_SUPPORT_GL3
#include <OpenGL/gl3.h>
#else
#include <OpenGL/gl.h>
#endif
#else
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#endif

struct SDL_Texture;
struct SDL_Window;
struct SDL_Renderer;

#ifndef __APPLE__
bool initGLExtensions();
#endif
GLuint compileShader(const char* source, GLuint shaderType);
GLuint compileProgram(const char* vtx, const char* frag);
void presentBackBuffer(SDL_Renderer *renderer, SDL_Window* win, SDL_Texture* backBuffer, GLuint programId);