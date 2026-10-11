#ifndef GAME_CLIENT_COMPONENTS_SIXCLIENT_KOG_POINTS_H
#define GAME_CLIENT_COMPONENTS_SIXCLIENT_KOG_POINTS_H

#include <game/client/component.h>

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

class CKogPoints : public CComponent
{
public:
	struct SEntry
	{
		int m_Points = 0;
		int m_Rank = 0;
		int64_t m_UpdatedAt = 0;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;

	bool OnKogServer() const;
	const SEntry *Find(const char *pName) const;
	bool IsQueued(const char *pName) const;
	void Request(const char *pName, bool Force);
	void NoteManualQuery(const char *pName);
	bool ConsumeServerLine(const char *pLine);

private:
	struct SPending
	{
		std::string m_Name;
		int64_t m_SentAt = 0;
		bool m_Hide = false;
	};

	std::unordered_map<std::string, SEntry> m_Entries;
	std::deque<std::string> m_Queue;
	SPending m_Pending;
	int64_t m_LastSent = 0;
	bool m_Loaded = false;

	static std::string Key(const char *pName);
	static bool ParseLine(const char *pLine, const char *pName, int &Points, int &Rank);
	void Load();
	void Save();
	void Send(const std::string &Name, bool Hide);
};

#endif
