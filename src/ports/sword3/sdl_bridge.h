#ifndef SWORD3_SDL_BRIDGE_H
#define SWORD3_SDL_BRIDGE_H

#include <stddef.h>
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host-owned SDL entry points used as Machismo address-hook targets.
 * Each create/destroy path records the object so a guest SDL pointer cannot
 * silently mix with the ROCKNIX SDL implementation.
 */
int sword3_SDL_InitSubSystem(Uint32 flags);
int sword3_SDL_VideoInit(const char *driver_name);
void sword3_SDL_VideoQuit(void);
int sword3_SDL_AudioInit(const char *driver_name);
void sword3_SDL_AudioQuit(void);
const char *sword3_SDL_GetError(void);
Uint32 sword3_SDL_GetTicks(void);

SDL_Window *sword3_SDL_CreateWindow(const char *title, int x, int y, int w,
				    int h, Uint32 flags);
void sword3_SDL_DestroyWindow(SDL_Window *window);
void sword3_SDL_SetWindowSize(SDL_Window *window, int w, int h);
void sword3_SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
void sword3_SDL_ShowWindow(SDL_Window *window);
int sword3_SDL_SetWindowDisplayMode(SDL_Window *window,
				    const SDL_DisplayMode *mode);

SDL_Renderer *sword3_SDL_CreateRenderer(SDL_Window *window, int index,
					Uint32 flags);
void sword3_SDL_DestroyRenderer(SDL_Renderer *renderer);
int sword3_SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h);
int sword3_SDL_RenderSetScale(SDL_Renderer *renderer, float scaleX,
			      float scaleY);
int sword3_SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer,
				      SDL_BlendMode blendMode);
int sword3_SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g,
				  Uint8 b, Uint8 a);
int sword3_SDL_RenderClear(SDL_Renderer *renderer);
int sword3_SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
			  const SDL_Rect *srcrect, const SDL_Rect *dstrect);
int sword3_SDL_RenderCopyF(SDL_Renderer *renderer, SDL_Texture *texture,
			   const SDL_Rect *srcrect, const SDL_FRect *dstrect);
int sword3_SDL_RenderCopyEx(SDL_Renderer *renderer, SDL_Texture *texture,
			    const SDL_Rect *srcrect, const SDL_Rect *dstrect,
			    const double angle, const SDL_Point *center,
			    const SDL_RendererFlip flip);
int sword3_SDL_RenderCopyExF(SDL_Renderer *renderer, SDL_Texture *texture,
			     const SDL_Rect *srcrect, const SDL_FRect *dstrect,
			     const double angle, const SDL_FPoint *center,
			     const SDL_RendererFlip flip);
void sword3_SDL_RenderPresent(SDL_Renderer *renderer);
int sword3_SDL_RenderDrawPointsF(SDL_Renderer *renderer,
				 const SDL_FPoint *points, int count);
int sword3_SDL_RenderFillRectsF(SDL_Renderer *renderer, const SDL_FRect *rects,
				int count);
int sword3_SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture);
int sword3_SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h);

SDL_Texture *sword3_SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format,
				      int access, int w, int h);
void sword3_SDL_DestroyTexture(SDL_Texture *texture);
SDL_Texture *sword3_SDL_CreateTextureFromSurface(SDL_Renderer *renderer,
						 SDL_Surface *surface);
int sword3_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
			     const void *pixels, int pitch);
int sword3_SDL_SetTextureBlendMode(SDL_Texture *texture,
				   SDL_BlendMode blendMode);
int sword3_SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect,
			   void **pixels, int *pitch);
void sword3_SDL_UnlockTexture(SDL_Texture *texture);

int sword3_SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette);
int sword3_SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect,
			 SDL_Surface *dst, SDL_Rect *dstrect);
int sword3_SDL_UpperBlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
			       SDL_Surface *dst, SDL_Rect *dstrect);
SDL_Surface *sword3_SDL_CreateRGBSurface(Uint32 flags, int width, int height,
					 int depth, Uint32 rmask,
					 Uint32 gmask, Uint32 bmask,
					 Uint32 amask);
SDL_Surface *sword3_SDL_CreateRGBSurfaceFrom(void *pixels, int width,
					     int height, int depth, int pitch,
					     Uint32 rmask, Uint32 gmask,
					     Uint32 bmask, Uint32 amask);
SDL_Surface *sword3_SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width,
						   int height, int depth,
						   Uint32 format);
SDL_Surface *sword3_SDL_CreateRGBSurfaceWithFormatFrom(
	void *pixels, int width, int height, int depth, int pitch,
	Uint32 format);
SDL_Surface *sword3_SDL_ConvertSurface(SDL_Surface *src,
				       const SDL_PixelFormat *format,
				       Uint32 flags);
SDL_Surface *sword3_SDL_ConvertSurfaceFormat(SDL_Surface *src,
					     Uint32 format, Uint32 flags);
int sword3_SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
int sword3_SDL_FillRect(SDL_Surface *surface, const SDL_Rect *rect,
			Uint32 color);
int sword3_SDL_FillRects(SDL_Surface *surface, const SDL_Rect *rects,
			 int count, Uint32 color);
void sword3_SDL_FreeSurface(SDL_Surface *surface);

SDL_RWops *sword3_SDL_RWFromFile(const char *file, const char *mode);
SDL_Surface *sword3_IMG_Load(const char *file);
SDL_Surface *sword3_IMG_Load_RW(SDL_RWops *src, int freesrc);
SDL_Surface *sword3_IMG_LoadTyped_RW(SDL_RWops *src, int freesrc,
				     const char *type);
int sword3_SDL_PeepEvents(SDL_Event *events, int numevents,
			  SDL_eventaction action, Uint32 minType,
			  Uint32 maxType);
void sword3_SDL_PumpEvents(void);
int sword3_SDL_PollEvent(SDL_Event *event);
int sword3_SDL_WaitEventTimeout(SDL_Event *event, int timeout);
SDL_bool sword3_SDL_IsGameController(int joystick_index);
SDL_AudioDeviceID sword3_SDL_OpenAudioDevice(const char *device, int iscapture,
					     const SDL_AudioSpec *desired,
					     SDL_AudioSpec *obtained,
					     int allowed_changes);
void sword3_SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void sword3_SDL_LockAudioDevice(SDL_AudioDeviceID dev);
void sword3_SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);
void sword3_SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void sword3_field_install(void);
int sword3_host_play_music_file(const char *path, int loops);
int sword3_host_play_music_data(const void *data, size_t size, int loops);
void sword3_host_stop_music(void);
int sword3_host_play_video_file(const char *path, void (*done)(void));
void sword3_host_stop_video(void);
int sword3_host_video_playing(void);

/* Generic 0-return stub kept for future address hooks. */
int sword3_ret0(void);

#ifdef __cplusplus
}
#endif

#endif
