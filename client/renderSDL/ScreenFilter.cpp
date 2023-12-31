/*
 * ScreenFilter.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "ScreenFilter.h"

/*#include "../../lib/CConfigHandler.h"
#include "../gui/CGuiHandler.h"
#include "../eventsSDL/NotificationHandler.h"
#include "../gui/WindowHandler.h"
#include "CMT.h"
#include "SDL_Extensions.h"*/

#include <SDL.h>
#include <SDL_image.h>

#ifndef __APPLE__
PFNGLCREATESHADERPROC glCreateShader;
PFNGLSHADERSOURCEPROC glShaderSource;
PFNGLCOMPILESHADERPROC glCompileShader;
PFNGLGETSHADERIVPROC glGetShaderiv;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
PFNGLDELETESHADERPROC glDeleteShader;
PFNGLATTACHSHADERPROC glAttachShader;
PFNGLCREATEPROGRAMPROC glCreateProgram;
PFNGLLINKPROGRAMPROC glLinkProgram;
PFNGLVALIDATEPROGRAMPROC glValidateProgram;
PFNGLGETPROGRAMIVPROC glGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
PFNGLUSEPROGRAMPROC glUseProgram;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
PFNGLUNIFORM2FVPROC glUniform2fv;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
PFNGLUNIFORM1IPROC glUniform1i;
#endif

#ifndef __APPLE__
bool initGLExtensions() {
	glCreateShader = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
	glShaderSource = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
	glCompileShader = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
	glGetShaderiv = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
	glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
	glDeleteShader = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
	glAttachShader = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
	glCreateProgram = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
	glLinkProgram = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
	glValidateProgram = (PFNGLVALIDATEPROGRAMPROC)SDL_GL_GetProcAddress("glValidateProgram");
	glGetProgramiv = (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
	glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
	glUseProgram = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)SDL_GL_GetProcAddress("glGetUniformLocation");
    glUniform2fv = (PFNGLUNIFORM2FVPROC)SDL_GL_GetProcAddress("glUniform2fv");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)SDL_GL_GetProcAddress("glUniformMatrix4fv");
    glUniform1i = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");

	return glCreateShader && glShaderSource && glCompileShader && glGetShaderiv && 
		glGetShaderInfoLog && glDeleteShader && glAttachShader && glCreateProgram &&
		glLinkProgram && glValidateProgram && glGetProgramiv && glGetProgramInfoLog &&
		glUseProgram && glGetUniformLocation && glUniform2fv && glUniformMatrix4fv && glUniform1i;
}
#endif

GLuint compileShader(const char* source, GLuint shaderType) {
	std::cout << "Compilando shader:" << std::endl << source << std::endl;
	// Create ID for shader
	GLuint result = glCreateShader(shaderType);
	// Define shader text
	glShaderSource(result, 1, &source, NULL);
	// Compile shader
	glCompileShader(result);

	//Check vertex shader for errors
	GLint shaderCompiled = GL_FALSE;
	glGetShaderiv( result, GL_COMPILE_STATUS, &shaderCompiled );
	if( shaderCompiled != GL_TRUE ) {
		std::cout << "Error en la compilación: " << result << "!" << std::endl;
		GLint logLength;
		glGetShaderiv(result, GL_INFO_LOG_LENGTH, &logLength);
		if (logLength > 0)
		{
			GLchar *log = (GLchar*)malloc(logLength);
			glGetShaderInfoLog(result, logLength, &logLength, log);
			std::cout << "Shader compile log:" << log << std::endl;
			free(log);
		}
		glDeleteShader(result);
		result = 0;
	} else {
		std::cout << "Shader compilado correctamente. Id = " << result << std::endl;
	}
	std::cout << "OpenGL version: " << (const char*)glGetString(GL_VERSION) << std::endl;
	std::cout << "GLSL version: " << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
	return result;
}

GLuint compileProgram(const char* vtx, const char* frag) {
	GLuint programId = 0;
	GLuint vtxShaderId, fragShaderId;

	programId = glCreateProgram();
	vtxShaderId = compileShader(vtx, GL_VERTEX_SHADER);
	fragShaderId = compileShader(frag, GL_FRAGMENT_SHADER);

	if(vtxShaderId && fragShaderId) {
		// Associate shader with program
		glAttachShader(programId, vtxShaderId);
		glAttachShader(programId, fragShaderId);
		glLinkProgram(programId);
		glValidateProgram(programId);

		// Check the status of the compile/link
		GLint logLen;
		glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &logLen);
		if(logLen > 0) {
			char* log = (char*) malloc(logLen * sizeof(char));
			// Show any errors as appropriate
			glGetProgramInfoLog(programId, logLen, &logLen, log);
			std::cout << "Prog Info Log: " << std::endl << log << std::endl;
			free(log);
		}
	}
	if(vtxShaderId) {
		glDeleteShader(vtxShaderId);
	}
	if(fragShaderId) {
		glDeleteShader(fragShaderId);
	}
	return programId;
}

void setUniform2fv(GLint id, const char * name, int a, int b)
{
    float fv[2] = { (float)a, (float)b };
    GLuint loc = glGetUniformLocation(id, name);
    glUniform2fv(loc, 1, fv);
}
void setUniformMatrix4fv(GLint id, const char * name, float matrix[16])
{
    GLuint loc = glGetUniformLocation(id, name);
    glUniformMatrix4fv(loc, 1, GL_FALSE, matrix);
}
void setUniform1i(GLint id, const char * name, int val)
{
    GLuint loc = glGetUniformLocation(id, name);
    glUniform1i(loc, val);
}

void presentBackBuffer(SDL_Renderer *renderer, SDL_Window* win, SDL_Texture* backBuffer, GLuint programId) {
	GLint oldProgramId;
	SDL_SetRenderTarget(renderer, NULL);
	SDL_RenderClear(renderer);

	SDL_GL_BindTexture(backBuffer, NULL, NULL);
	if(programId != 0) {
		glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgramId);
		glUseProgram(programId);
	}

	GLfloat minx, miny, maxx, maxy;
	GLfloat minu, maxu, minv, maxv;

	minx = 0.0f;
	miny = 0.0f;
	maxx = SDL_GetWindowSurface(win)->w;
	maxy = SDL_GetWindowSurface(win)->h;

	minu = 0.0f;
	maxu = 1.0f;
	minv = 0.0f;
	maxv = 1.0f;

	glBegin(GL_TRIANGLE_STRIP);
		glTexCoord2f(minu, minv);
		glVertex2f(minx, miny);
		glTexCoord2f(maxu, minv);
		glVertex2f(maxx, miny);
		glTexCoord2f(minu, maxv);
		glVertex2f(minx, maxy);
		glTexCoord2f(maxu, maxv);
		glVertex2f(maxx, maxy);
	glEnd();
	SDL_GL_SwapWindow(win);

	if(programId != 0) {
		glUseProgram(oldProgramId);
	}
}

