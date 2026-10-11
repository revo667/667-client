/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CHAT_H
#define GAME_CLIENT_COMPONENTS_CHAT_H

#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/shared/ringbuffer.h>

#include <generated/protocol7.h>

#include <game/client/component.h>
#include <game/client/components/sixclient/chat_media.h> // sixclient
#include <game/client/components/hud_layout.h> // sixclient
#include <game/client/lineinput.h>
#include <game/client/render.h>
#include <game/client/ui.h> // sixclient
#include <game/client/ui_rect.h> // sixclient

#include <algorithm> // sixclient

#include <vector>

class CTranslateResponse
{
public:
	bool m_Error = false;
	char m_Text[1024] = "";
	char m_Language[16] = "";
};

constexpr auto SAVES_FILE = "ddnet-saves.txt";

class CChat : public CComponent
{
	static constexpr float CHAT_HEIGHT_FULL = 200.0f;
	static constexpr float CHAT_HEIGHT_MIN = 50.0f;
	static constexpr float CHAT_FONTSIZE_WIDTH_RATIO = 2.5f;

	enum
	{
		MAX_LINES = 64,
	};

	CLineInputBuffered<MAX_CHAT_LENGTH> m_Input;
	class CLine
	{
	public:
		CLine();
		void Reset(CChat &This);

		bool m_Initialized;
		int64_t m_Time;
		float m_aYOffset[2];
		int m_ClientId;
		int m_TeamNumber;
		bool m_Team;
		bool m_Whisper;
		int m_NameColor;
		char m_aName[64];
		char m_aText[MAX_CHAT_LENGTH];
		bool m_Friend;
		bool m_Highlighted;
		std::optional<ColorRGBA> m_CustomColor;
		// 667client
		bool m_ScCopyAsLoad = false;
		bool m_ScSavesChat = false;
		// 667client

		STextContainerIndex m_TextContainerIndex;
		int m_QuadContainerIndex;

		std::shared_ptr<CManagedTeeRenderInfo> m_pManagedTeeRenderInfo;

		float m_TextYOffset;

		int m_TimesRepeated;

		std::shared_ptr<CTranslateResponse> m_pTranslateResponse;
		// 667client
		SChatMediaLine m_Media;
		// 667client
	};

	bool m_PrevScoreBoardShowed;
	bool m_PrevShowChat;
	// 667client
	float m_PrevHudLayoutX = -10000.0f;
	float m_PrevHudLayoutY = -10000.0f;
	int m_PrevHudLayoutScale = -1;
	bool m_PrevHudLayoutEnabled = true;
	bool m_PrevModeActive = false;
	// 667client

	CLine m_aLines[MAX_LINES];
	int m_CurrentLine;
	// 667client
	float m_BacklogScrollOffset = 0.0f;
	float m_BacklogScrollOffsetChange = 0.0f;
	float m_ChatMaxScrollOffset = 0.0f;
	bool m_ScrollbarDragging = false;
	float m_ScrollbarDragOffset = 0.0f;
	int m_ActionLineIndex = -1;
	vec2 m_ActionMenuPos = vec2(0.0f, 0.0f);
	// 667client

	enum
	{
		// client IDs for special messages
		CLIENT_MSG = -2,
		SERVER_MSG = -1,
	};

	enum
	{
		MODE_NONE = 0,
		MODE_ALL,
		MODE_TEAM,
	};

	enum
	{
		CHAT_SERVER = 0,
		CHAT_HIGHLIGHT,
		CHAT_CLIENT,
		CHAT_NUM,
	};

	int m_Mode;
	// 667client
	std::optional<vec2> m_LastMousePos;
	// 667client
	bool m_Show;
	bool m_CompletionUsed;
	int m_CompletionChosen;
	char m_aCompletionBuffer[MAX_CHAT_LENGTH];
	int m_PlaceholderOffset;
	int m_PlaceholderLength;
	static char ms_aDisplayText[MAX_CHAT_LENGTH];
	class CRateablePlayer
	{
	public:
		int m_ClientId;
		int m_Score;
	};
	CRateablePlayer m_aPlayerCompletionList[MAX_CLIENTS];
	int m_PlayerCompletionListLength;

	struct CCommand
	{
		char m_aName[IConsole::TEMPCMD_NAME_LENGTH];
		char m_aParams[IConsole::TEMPCMD_PARAMS_LENGTH];
		char m_aHelpText[IConsole::TEMPCMD_HELP_LENGTH];

		CCommand() = default;
		CCommand(const char *pName, const char *pParams, const char *pHelpText)
		{
			str_copy(m_aName, pName);
			str_copy(m_aParams, pParams);
			str_copy(m_aHelpText, pHelpText);
		}

		bool operator<(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) < 0; }
		bool operator<=(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) <= 0; }
		bool operator==(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) == 0; }
	};

	std::vector<CCommand> m_vServerCommands;
	bool m_ServerCommandsNeedSorting;

	struct CHistoryEntry
	{
		int m_Team;
		char m_aText[1];
	};
	CHistoryEntry *m_pHistoryEntry;
	CStaticRingBuffer<CHistoryEntry, 64 * 1024, CRingBufferBase::FLAG_RECYCLE> m_History;
	int m_PendingChatCounter;
	int64_t m_LastChatSend;
	int64_t m_aLastSoundPlayed[CHAT_NUM];
	bool m_IsInputCensored;
	char m_aCurrentInputText[MAX_CHAT_LENGTH];
	bool m_EditingNewLine;

	bool m_ServerSupportsCommandInfo;

	// 667client
	CButtonContainer m_TranslateSettingsButton;
	CButtonContainer m_TranslateSettingsIncomingButton;
	CButtonContainer m_TranslateSettingsOutgoingButton;
	CButtonContainer m_TranslateSettingsPunctuationButton;
	SPopupMenuId m_TranslateSettingsPopupId;
	bool m_TranslateSettingsInputActive = false;
	bool m_TranslateButtonPressed = false;
	bool m_TranslateButtonRectValid = false;
	CUIRect m_TranslateButtonRect;
	CUIRect m_TranslateButtonUiRect;
	// 667client

	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConChat(IConsole::IResult *pResult, void *pUserData);
	static void ConShowChat(IConsole::IResult *pResult, void *pUserData);
	static void ConEcho(IConsole::IResult *pResult, void *pUserData);
	static void ConClearChat(IConsole::IResult *pResult, void *pUserData);

	static void ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	bool LineShouldHighlight(const char *pLine, const char *pName);
	void StoreSave(const char *pText);
	// 667client
	vec2 ChatMousePos() const;
	void OpenTranslateSettingsPopup();
	void RenderTranslateSettingsButton(const CUIRect &ButtonRect);
	static CUi::EPopupMenuFunctionResult PopupTranslateSettings(void *pContext, CUIRect View, bool Active);
	// 667client

	friend class CBindChat;
	friend class CTranslate;
	friend class CTClient;
	// 667client
	friend class CChatBubbles;
	friend class CChatQoL;
	friend class CChatMedia;
	friend class CChatScroll;
	// 667client

public:
	CChat();
	int Sizeof() const override { return sizeof(*this); }

	static constexpr float MESSAGE_TEE_PADDING_RIGHT = 0.5f;

	bool IsActive() const { return m_Mode != MODE_NONE; }
	void AddLine(int ClientId, int Team, const char *pLine);
	void EnableMode(int Team);
	void DisableMode();
	void RegisterCommand(const char *pName, const char *pParams, const char *pHelpText);
	void UnregisterCommand(const char *pName);
	void Echo(const char *pString);
	// 667client
	void AddColoredLine(const char *pLine, ColorRGBA Color, bool PlaySystemSound = false, bool CopyAsLoad = false);
	CGameClient *ScGameClient() { return GameClient(); }
	const CGameClient *ScGameClient() const { return GameClient(); }
	// 667client

	void OnWindowResize() override;
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	void OnPrepareLines(float y);
	CUIRect GetHudRect(float HudWidth, float HudHeight, bool ForcePreview = false) const; // 667client
	void Reset();
	void OnRelease() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override; // 667client
	void OnInit() override;

	void RebuildChat();
	void ClearLines();

	void EnsureCoherentFontSize() const;
	void EnsureCoherentWidth() const;

	// 667client
	float LayoutScaleFactor() const
	{
		return std::clamp(HudLayout::Get(HudLayout::MODULE_CHAT, HudLayout::CANVAS_WIDTH, HudLayout::CANVAS_HEIGHT).m_Scale / 100.0f, 0.25f, 3.0f);
	}
	float FontSize() const { return (g_Config.m_ClChatFontSize / 10.0f) * LayoutScaleFactor(); }
	float ChatWidth() const { return g_Config.m_ClChatWidth * LayoutScaleFactor(); }
	// 667client
	float MessagePaddingX() const { return FontSize() * (5 / 6.f); }
	float MessagePaddingY() const { return FontSize() * (1 / 6.f); }
	float MessageTeeSize() const { return FontSize() * (7 / 6.f); }
	float MessageRounding() const { return FontSize() * (1 / 2.f); }

	// ----- send functions -----

	// Sends a chat message to the server.
	//
	// @param Team MODE_ALL=0 MODE_TEAM=1
	// @param pLine the chat message
	void SendChat(int Team, const char *pLine);

	// Sends a chat message to the server.
	//
	// It uses a queue with a maximum of 3 entries
	// that ensures there is a minimum delay of one second
	// between sent messages.
	//
	// It uses team or public chat depending on m_Mode.
	// 667client
	void SendChatQueued(const char *pLine);

	void SendTranslatedChatQueued(int Team, const char *pLine);
	// 667client
};
#endif
