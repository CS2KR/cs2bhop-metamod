#include "bhop_style.h"
#include "version_gen.h"

#define STYLE_NAME       "HalfSideways"
#define STYLE_NAME_SHORT "HSW"

class BhopHalfSidewaysStylePlugin : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late);
	bool Unload(char *error, size_t maxlen);
	bool Pause(char *error, size_t maxlen);
	bool Unpause(char *error, size_t maxlen);

public:
	const char *GetAuthor()
	{
		return PLUGIN_AUTHOR;
	}

	const char *GetName()
	{
		return "CS2Bhop-Style-HalfSideways";
	}

	const char *GetDescription()
	{
		return "HalfSideways style plugin for CS2Bhop";
	}

	const char *GetURL()
	{
		return PLUGIN_URL;
	}

	const char *GetLicense()
	{
		return PLUGIN_LICENSE;
	}

	const char *GetVersion()
	{
		return PLUGIN_FULL_VERSION;
	}

	const char *GetDate()
	{
		return __DATE__;
	}

	const char *GetLogTag()
	{
		return PLUGIN_LOGTAG;
	}
};

class BhopHalfSidewaysStyleService : public BhopStyleService
{
	using BhopStyleService::BhopStyleService;

public:
	virtual const char *GetStyleName() override
	{
		return "HalfSideways";
	}

	virtual const char *GetStyleShortName() override
	{
		return "HSW";
	}

	virtual const CVValue_t *GetTweakedConvarValue(const char *name) override;
	virtual void Init() override;
	virtual void Cleanup() override;
	virtual void OnSetupMove(PlayerCommand *) override;
};
