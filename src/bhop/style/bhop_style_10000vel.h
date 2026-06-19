#include "bhop_style.h"
#include "version_gen.h"

#define STYLE_NAME       "10000vel"
#define STYLE_NAME_SHORT "10kvel"

class Bhop10000VelStylePlugin : public ISmmPlugin, public IMetamodListener
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
		return "CS2Bhop-Style-10000vel";
	}

	const char *GetDescription()
	{
		return "10000vel style plugin for CS2Bhop";
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

class Bhop10000VelStyleService : public BhopStyleService
{
	using BhopStyleService::BhopStyleService;

public:
	virtual const char *GetStyleName() override
	{
		return "10000vel";
	}

	virtual const char *GetStyleShortName() override
	{
		return "10kvel";
	}

	virtual const CVValue_t *GetTweakedConvarValue(const char *name) override;
	virtual void Init() override;
	virtual void Cleanup() override;
	virtual void OnProcessMovement() override;
};
