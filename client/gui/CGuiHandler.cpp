/*
 * CGuiHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CGuiHandler.h"
#include "../lib/CondSh.h"

#include "CIntObject.h"
#include "CursorHandler.h"
#include "ShortcutHandler.h"
#include "FramerateManager.h"
#include "WindowHandler.h"
#include "EventDispatcher.h"
#include "../eventsSDL/InputHandler.h"

#include "../CGameInfo.h"
#include "../render/Colors.h"
#include "../render/Graphics.h"
#include "../render/IFont.h"
#include "../render/EFont.h"
#include "../renderSDL/ScreenHandler.h"
#include "../renderSDL/RenderHandler.h"
#include "../renderSDL/ScreenFilter.h"
#include "../CMT.h"
#include "../CPlayerInterface.h"
#include "../battle/BattleInterface.h"

#include "../../lib/CThreadHelper.h"
#include "../../lib/CConfigHandler.h"

#include <SDL_render.h>

CGuiHandler GH;

static thread_local bool inGuiThread = false;

SObjectConstruction::SObjectConstruction(CIntObject *obj)
:myObj(obj)
{
	GH.createdObj.push_front(obj);
	GH.captureChildren = true;
}

SObjectConstruction::~SObjectConstruction()
{
	assert(GH.createdObj.size());
	assert(GH.createdObj.front() == myObj);
	GH.createdObj.pop_front();
	GH.captureChildren = GH.createdObj.size();
}

SSetCaptureState::SSetCaptureState(bool allow, ui8 actions)
{
	previousCapture = GH.captureChildren;
	GH.captureChildren = false;
	prevActions = GH.defActionsDef;
	GH.defActionsDef = actions;
}

SSetCaptureState::~SSetCaptureState()
{
	GH.captureChildren = previousCapture;
	GH.defActionsDef = prevActions;
}

void CGuiHandler::init()
{
	inGuiThread = true;

	inputHandlerInstance = std::make_unique<InputHandler>();
	eventDispatcherInstance = std::make_unique<EventDispatcher>();
	windowHandlerInstance = std::make_unique<WindowHandler>();
	screenHandlerInstance = std::make_unique<ScreenHandler>();
	renderHandlerInstance = std::make_unique<RenderHandler>();
	shortcutsHandlerInstance = std::make_unique<ShortcutHandler>();
	framerateManagerInstance = std::make_unique<FramerateManager>(settings["video"]["targetfps"].Integer());

const char * vtx = R"VTX(
varying vec4 v_color;
varying vec2 v_texCoord;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    v_color = gl_Color;
    v_texCoord = vec2(gl_MultiTexCoord0);
}
)VTX";

const char * frag = R"FRAG(
//
// PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER
//
//   by Timothy Lottes
//
// This is more along the style of a really good CGA arcade monitor.
// With RGB inputs instead of NTSC.
// The shadow mask example has the mask rotated 90 degrees for less chromatic aberration.
//
// Left it unoptimized to show the theory behind the algorithm.
//
// It is an example what I personally would want as a display option for pixel art games.
// Please take and use, change, or whatever.
//

varying vec4 v_color;
varying vec2 v_texCoord;

uniform sampler2D tex0;

// Hardness of scanline.
//  -8.0 = soft
// -16.0 = medium
float hardScan=-8.0;

// Hardness of pixels in scanline.
// -2.0 = soft
// -4.0 = hard
float hardPix=-2.0;

// Display warp.
// 0.0 = none
// 1.0/8.0 = extreme
vec2 warp=vec2(1.0/32.0,1.0/24.0); 

// Amount of shadow mask.
float maskDark=1.0;
float maskLight=1.5;

vec2 res = vec2(640.0,480.0); // /3.0

//------------------------------------------------------------------------

// sRGB to Linear.
// Assuing using sRGB typed textures this should not be needed.
float ToLinear1(float c){return(c<=0.04045)?c/12.92:pow((c+0.055)/1.055,2.4);}
vec3 ToLinear(vec3 c){return vec3(ToLinear1(c.r),ToLinear1(c.g),ToLinear1(c.b));}

// Linear to sRGB.
// Assuing using sRGB typed textures this should not be needed.
float ToSrgb1(float c){return(c<0.0031308?c*12.92:1.055*pow(c,0.41666)-0.055);}
vec3 ToSrgb(vec3 c){return vec3(ToSrgb1(c.r),ToSrgb1(c.g),ToSrgb1(c.b));}

// Nearest emulated sample given floating point position and texel offset.
// Also zero's off screen.
vec3 Fetch(vec2 pos,vec2 off){
  pos=floor(pos*res+off)/res;
  if(max(abs(pos.x-0.5),abs(pos.y-0.5))>0.5)return vec3(0.0,0.0,0.0);
  return ToLinear(texture2D(tex0,pos.xy,-16.0).rgb);}

// Distance in emulated pixels to nearest texel.
vec2 Dist(vec2 pos){pos=pos*res;return -((pos-floor(pos))-vec2(0.5));}
    
// 1D Gaussian.
float Gaus(float pos,float scale){return exp2(scale*pos*pos);}

// 3-tap Gaussian filter along horz line.
vec3 Horz3(vec2 pos,float off){
  vec3 b=Fetch(pos,vec2(-1.0,off));
  vec3 c=Fetch(pos,vec2( 0.0,off));
  vec3 d=Fetch(pos,vec2( 1.0,off));
  float dst=Dist(pos).x;
  // Convert distance to weight.
  float scale=hardPix;
  float wb=Gaus(dst-1.0,scale);
  float wc=Gaus(dst+0.0,scale);
  float wd=Gaus(dst+1.0,scale);
  // Return filtered sample.
  return (b*wb+c*wc+d*wd)/(wb+wc+wd);}

// 5-tap Gaussian filter along horz line.
vec3 Horz5(vec2 pos,float off){
  vec3 a=Fetch(pos,vec2(-2.0,off));
  vec3 b=Fetch(pos,vec2(-1.0,off));
  vec3 c=Fetch(pos,vec2( 0.0,off));
  vec3 d=Fetch(pos,vec2( 1.0,off));
  vec3 e=Fetch(pos,vec2( 2.0,off));
  float dst=Dist(pos).x;
  // Convert distance to weight.
  float scale=hardPix;
  float wa=Gaus(dst-2.0,scale);
  float wb=Gaus(dst-1.0,scale);
  float wc=Gaus(dst+0.0,scale);
  float wd=Gaus(dst+1.0,scale);
  float we=Gaus(dst+2.0,scale);
  // Return filtered sample.
  return (a*wa+b*wb+c*wc+d*wd+e*we)/(wa+wb+wc+wd+we);}

// Return scanline weight.
float Scan(vec2 pos,float off){
  float dst=Dist(pos).y;
  return Gaus(dst+off,hardScan);}

// Allow nearest three lines to effect pixel.
vec3 Tri(vec2 pos){
  vec3 a=Horz3(pos,-1.0);
  vec3 b=Horz5(pos, 0.0);
  vec3 c=Horz3(pos, 1.0);
  float wa=Scan(pos,-1.0);
  float wb=Scan(pos, 0.0);
  float wc=Scan(pos, 1.0);
  return a*wa+b*wb+c*wc;}

// Distortion of scanlines, and end of screen alpha.
vec2 Warp(vec2 pos){
  pos=pos*2.0-1.0;    
  pos*=vec2(1.0+(pos.y*pos.y)*warp.x,1.0+(pos.x*pos.x)*warp.y);
  return pos*0.5+0.5;}

// Shadow mask.
vec3 Mask(vec2 pos){
  pos.x+=pos.y*3.0;
  vec3 mask=vec3(maskDark,maskDark,maskDark);
  pos.x=fract(pos.x/6.0);
  if(pos.x<0.333)mask.r=maskLight;
  else if(pos.x<0.666)mask.g=maskLight;
  else mask.b=maskLight;
  return mask;}    

// Draw dividing bars.
float Bar(float pos,float bar){pos-=bar;return pos*pos<4.0?0.0:1.0;}

// Entry.
void main(){
  // Unmodified.
  vec2 pos=Warp(v_texCoord);
  vec4 fragColor;
  fragColor.rgb=Tri(pos)*Mask(gl_FragCoord.xy);
  fragColor.rgb=ToSrgb(fragColor.rgb);
  gl_FragColor=v_color * vec4(fragColor.rgb, 1.0);
}
)FRAG";

#ifndef __APPLE__
	if (!initGLExtensions()) {
		std::cout << "Couldn't init GL extensions!" << std::endl;
	}
	else
	{
		programId = compileProgram(vtx, frag);
	}
#endif
}

void CGuiHandler::handleEvents()
{
	events().dispatchTimer(framerate().getElapsedMilliseconds());

	//player interface may want special event handling
	if(nullptr != LOCPLINT && LOCPLINT->capturedAllEvents())
		return;

	input().processEvents();
}

void CGuiHandler::fakeMouseMove()
{
	dispatchMainThread([](){
		GH.events().dispatchMouseMoved(Point(0, 0), GH.getCursorPosition());
	});
}

void CGuiHandler::startTextInput(const Rect & whereInput)
{
	input().startTextInput(whereInput);
}

void CGuiHandler::stopTextInput()
{
	input().stopTextInput();
}

void CGuiHandler::renderFrame()
{
	{
		boost::mutex::scoped_lock interfaceLock(GH.interfaceMutex);

		if (nullptr != curInt)
			curInt->update();

		if (settings["video"]["showfps"].Bool())
			drawFPSCounter();
	}



	SDL_UpdateTexture(screenTexture, nullptr, screen->pixels, screen->pitch);

	SDL_RenderClear(mainRenderer);
	SDL_RenderCopy(mainRenderer, screenTexture, nullptr, nullptr);

	{
		boost::mutex::scoped_lock interfaceLock(GH.interfaceMutex);

		CCS->curh->render();

		windows().onFrameRendered();
	}

	//SDL_RenderPresent(mainRenderer);
	presentBackBuffer(mainRenderer, mainWindow, screenTexture, programId);
	framerate().framerateDelay(); // holds a constant FPS
}

CGuiHandler::CGuiHandler()
	: defActionsDef(0)
	, captureChildren(false)
	, curInt(nullptr)
	, fakeStatusBar(std::make_shared<EmptyStatusBar>())
{
}

CGuiHandler::~CGuiHandler() = default;

ShortcutHandler & CGuiHandler::shortcuts()
{
	assert(shortcutsHandlerInstance);
	return *shortcutsHandlerInstance;
}

FramerateManager & CGuiHandler::framerate()
{
	assert(framerateManagerInstance);
	return *framerateManagerInstance;
}

bool CGuiHandler::isKeyboardCtrlDown() const
{
	return inputHandlerInstance->isKeyboardCtrlDown();
}

bool CGuiHandler::isKeyboardAltDown() const
{
	return inputHandlerInstance->isKeyboardAltDown();
}

bool CGuiHandler::isKeyboardShiftDown() const
{
	return inputHandlerInstance->isKeyboardShiftDown();
}

const Point & CGuiHandler::getCursorPosition() const
{
	return inputHandlerInstance->getCursorPosition();
}

Point CGuiHandler::screenDimensions() const
{
	return Point(screen->w, screen->h);
}

void CGuiHandler::drawFPSCounter()
{
	int x = 7;
	int y = screen->h-20;
	int width3digitFPSIncludingPadding = 48;
	int heightFPSTextIncludingPadding = 11;
	static SDL_Rect overlay = { x, y, width3digitFPSIncludingPadding, heightFPSTextIncludingPadding};
	uint32_t black = SDL_MapRGB(screen->format, 10, 10, 10);
	SDL_FillRect(screen, &overlay, black);

	std::string fps = std::to_string(framerate().getFramerate())+" FPS";

	graphics->fonts[FONT_SMALL]->renderTextLeft(screen, fps, Colors::WHITE, Point(8, screen->h-22));
}

bool CGuiHandler::amIGuiThread()
{
	return inGuiThread;
}

void CGuiHandler::dispatchMainThread(const std::function<void()> & functor)
{
	inputHandlerInstance->dispatchMainThread(functor);
}

IScreenHandler & CGuiHandler::screenHandler()
{
	return *screenHandlerInstance;
}

IRenderHandler & CGuiHandler::renderHandler()
{
	return *renderHandlerInstance;
}

EventDispatcher & CGuiHandler::events()
{
	return *eventDispatcherInstance;
}

InputHandler & CGuiHandler::input()
{
	return *inputHandlerInstance;
}

WindowHandler & CGuiHandler::windows()
{
	assert(windowHandlerInstance);
	return *windowHandlerInstance;
}

std::shared_ptr<IStatusBar> CGuiHandler::statusbar()
{
	auto locked = currentStatusBar.lock();

	if (!locked)
		return fakeStatusBar;

	return locked;
}

void CGuiHandler::setStatusbar(std::shared_ptr<IStatusBar> newStatusBar)
{
	currentStatusBar = newStatusBar;
}

void CGuiHandler::onScreenResize()
{
	screenHandler().onScreenResize();
	windows().onScreenResize();
}
