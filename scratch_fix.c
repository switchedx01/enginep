void render_gradient_header(SDL_Renderer *ren, const char *text, int x, int y,
                             SDL_Color c1, SDL_Color c2) {
  if (!g_hdr_tex || !text) return;
  int len = strlen(text);
  float curr_x = (float)x;
  float curr_y = (float)y;
  for (int i = 0; i < len; i++) {
    float t = (float)i / (float)fmax(1, len - 1);
    SDL_Color col = {(uint8_t)(c1.r + (c2.r - c1.r) * t),
                     (uint8_t)(c1.g + (c2.g - c1.g) * t),
                     (uint8_t)(c1.b + (c2.b - c1.b) * t), 255};
    if (text[i] >= 32 && text[i] < 128) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(g_hdata, 1024, 1024, text[i] - 32, &curr_x, &curr_y, &q, 1);
      
      SDL_Rect src = {(int)(q.s0 * 1024), (int)(q.t0 * 1024),
                      (int)((q.s1 - q.s0) * 1024), (int)((q.t1 - q.t0) * 1024)};
      SDL_Rect dst = {(int)q.x0, (int)q.y0, (int)(q.x1 - q.x0), (int)(q.y1 - q.y0)};
      
      SDL_SetTextureColorMod(g_hdr_tex, col.r, col.g, col.b);
      SDL_RenderCopy(ren, g_hdr_tex, &src, &dst);
    }
  }
}
