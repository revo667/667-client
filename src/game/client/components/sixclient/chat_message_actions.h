#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_CHAT_MESSAGE_ACTIONS_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_CHAT_MESSAGE_ACTIONS_H

#include <base/vmath.h>

#include <game/client/ui_rect.h>

class IGraphics;
class ITextRender;

namespace SixClientChatMessageActions
{

enum class EAction
{
	None,
	Copy,
	Reply,
};

struct SLayout
{
	CUIRect m_Panel;
	CUIRect m_Reply;
	CUIRect m_Copy;
	bool m_ReplyEnabled = false;
};

SLayout BuildLayout(vec2 AnchorPos, float FontSize, bool ReplyEnabled);
void Render(ITextRender *pTextRender, const SLayout &Layout, vec2 MousePos, float Alpha);
EAction HitTest(const SLayout &Layout, vec2 MousePos);
bool PanelContains(const SLayout &Layout, vec2 MousePos);
void DrawMessageHighlight(IGraphics *pGraphics, float X, float Y, float W, float H, float Rounding, float Alpha);

} // namespace SixClientChatMessageActions

#endif
