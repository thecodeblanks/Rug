#pragma once

#ifdef RUG_EXPORTS
#define RUG_API __declspec(dllexport)
#else
#define RUG_API __declspec(dllimport)
#endif

extern "C" {

RUG_API int  rug_init(int width, int height);

RUG_API int  rug_load_texture(const wchar_t* path);

RUG_API void rug_rebuild(float scale, float drop);

RUG_API void rug_set_bumps(const float* data, int count);

RUG_API void rug_step(float dt);

// Returns 1 when dst was updated; rect receives the x, y, width, height that changed.
RUG_API int  rug_render(unsigned char* dst, int stride, int* rect);

// Forces the next rug_render to redraw, after the caller drew over dst itself.
RUG_API void rug_invalidate(void);

RUG_API int  rug_mouse_down(float x, float y);
RUG_API void rug_mouse_drag(float x, float y);
RUG_API void rug_mouse_up(void);

RUG_API int  rug_hit_test(float x, float y);

RUG_API void rug_get_bounds(float* out);

RUG_API void rug_rotate(float angle);

RUG_API void rug_resize(float scale);

RUG_API void rug_get_pose(float* out);

RUG_API void rug_get_corners(float* out);

RUG_API void rug_hold_begin(void);
RUG_API void rug_hold_end(void);

RUG_API void rug_layout(float cx, float cy, float angle, float drop);

// The rug's exact draped shape. rug_point_count gives the vertex count; rug_get_points copies every vertex
// position (3 floats each) into out when it holds at least maxVertices; rug_set_points puts a saved drape back
// at rest, so the exact folds reappear without a fresh drop, and returns 0 if the vertex count differs.
RUG_API int  rug_point_count(void);
RUG_API int  rug_get_points(float* out, int maxVertices);
RUG_API int  rug_set_points(const float* in, int count);

// Non-zero once the cloth has come to rest, so a caller can snapshot the settled drape.
RUG_API int  rug_is_settled(void);

RUG_API void rug_shutdown(void);

// Desktop icons through the Shell's IFolderView. Writes up to capacity x, y pairs (list view client
// coordinates) and layout = { spacing x, spacing y, icon size in logical pixels }. Returns the item count, -1 on failure.
RUG_API int  rug_desktop_items(int* xy, int capacity, int* layout);

// Sets the wallpaper on every monitor through IDesktopWallpaper; position is a DESKTOP_WALLPAPER_POSITION or -1 to
// keep it, and an empty path changes only the position.
RUG_API int  rug_set_wallpaper(const wchar_t* path, int position);

}
