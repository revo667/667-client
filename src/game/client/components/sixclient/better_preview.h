#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_BETTER_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_BETTER_PREVIEW_H

#include <engine/graphics.h>

#include <game/client/ui_rect.h>

class CUi;

namespace SixClientBetterPreview
{

void RenderEntities(IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture, const CUIRect &TextureRect, float TextureWidth);
void RenderGunpackCard(IGraphics *pGraphics, CUi *pUi, const IGraphics::CTextureHandle &Texture, const CUIRect &CardRect, const char *pName);

}

#endif
