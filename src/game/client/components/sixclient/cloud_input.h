#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_CLOUD_INPUT_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_CLOUD_INPUT_H

#include <base/vmath.h>

#include <engine/client/enums.h>

#include <generated/protocol.h>

#include <game/client/component.h>

class CControls;
class CGameClient;

class CCloudInput : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	bool IsActive() const;
	float Amount() const;
	int SelfTickOffset() const;
	int OthersTickOffset() const;
	CNetObj_PlayerInput &Input(int Dummy);
	const CNetObj_PlayerInput &Input(int Dummy) const;
	bool CheckNewInput(const CControls &Controls);
	void ApplyOffset(const CGameClient &GameClient, int ClientId, int &Tick, float &Intra) const;
	bool TryGetPredPos(const CGameClient &GameClient, int ClientId, int Tick, float Intra, vec2 &OutPos) const;

private:
	CNetObj_PlayerInput m_aInput[NUM_DUMMIES] = {};
};

#endif
