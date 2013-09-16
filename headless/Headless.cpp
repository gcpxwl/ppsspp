// Headless version of PPSSPP, for testing using http://code.google.com/p/pspautotests/ .
// See headless.txt.
// To build on non-windows systems, just run CMake in the SDL directory, it will build both a normal ppsspp and the headless version.

#include <cstdio>
#include <cstdlib>
#include <limits>

#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/System.h"
#include "Core/HLE/sceUtility.h"
#include "Core/Host.h"
#include "Log.h"
#include "LogManager.h"
#include "base/NativeApp.h"
#include "input/input_state.h"
#include "base/timeutil.h"

#include "Compare.h"
#include "StubHost.h"
#ifdef _WIN32
#include "Windows/OpenGLBase.h"
#include "WindowsHeadlessHost.h"
#include "WindowsHeadlessHostDx9.h"
#endif

class PrintfLogger : public LogListener
{
public:
	void Log(LogTypes::LOG_LEVELS level, const char *msg)
	{
		switch (level)
		{
		case LogTypes::LVERBOSE:
			fprintf(stderr, "V %s", msg);
			break;
		case LogTypes::LDEBUG:
			fprintf(stderr, "D %s", msg);
			break;
		case LogTypes::LINFO:
			fprintf(stderr, "I %s", msg);
			break;
		case LogTypes::LERROR:
			fprintf(stderr, "E %s", msg);
			break;
		case LogTypes::LWARNING:
			fprintf(stderr, "W %s", msg);
			break;
		case LogTypes::LNOTICE:
		default:
			fprintf(stderr, "N %s", msg);
			break;
		}
	}
};

struct InputState;
// Temporary hack around annoying linking error.
void GL_SwapBuffers() { }
void NativeUpdate(InputState &input_state) { }
void NativeRender() { }

std::string System_GetProperty(SystemProperty prop) { return ""; }

#ifndef _WIN32
InputState input_state;
#endif

void printUsage(const char *progname, const char *reason)
{
	if (reason != NULL)
		fprintf(stderr, "Error: %s\n\n", reason);
	fprintf(stderr, "PPSSPP Headless\n");
	fprintf(stderr, "This is primarily meant as a non-interactive test tool.\n\n");
	fprintf(stderr, "Usage: %s file.elf [options]\n\n", progname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -m, --mount umd.cso   mount iso on umd:\n");
	fprintf(stderr, "  -l, --log             full log output, not just emulated printfs\n");

	HEADLESSHOST_CLASS h1;
	HeadlessHost h2;
	if (typeid(h1) != typeid(h2))
	{
		fprintf(stderr, "  --graphics=BACKEND    use the full gpu backend (slower)\n");
		fprintf(stderr, "                        options: gles, software, directx9\n");
		fprintf(stderr, "  --screenshot=FILE     compare against a screenshot\n");
	}
	fprintf(stderr, "  --timeout=SECONDS     abort test it if takes longer than SECONDS\n");

	fprintf(stderr, "  -i                    use the interpreter\n");
	fprintf(stderr, "  -j                    use jit (default)\n");
	fprintf(stderr, "  -c, --compare         compare with output in file.expected\n");
	fprintf(stderr, "\nSee headless.txt for details.\n");
}

static HeadlessHost * getHost(GPUCore gpuCore) {
	switch(gpuCore) {
	case GPU_NULL:
		return new HeadlessHost();
	case GPU_DIRECTX9:
		return new WindowsHeadlessHostDx9();
	default:
		return new HEADLESSHOST_CLASS();
	}
}

static std::string ChopFront(std::string s, std::string front)
{
	if (s.size() >= front.size())
	{
		if (s.substr(0, front.size()) == front)
			return s.substr(front.size());
	}
	return s;
}

static std::string ChopEnd(std::string s, std::string end)
{
	if (s.size() >= end.size())
	{
		size_t endpos = s.size() - end.size();
		if (s.substr(endpos) == end)
			return s.substr(0, endpos);
	}
	return s;
}

int main(int argc, const char* argv[])
{
	bool fullLog = false;
	bool useJit = true;
	bool autoCompare = false;
	GPUCore gpuCore = GPU_NULL;
	
	const char *bootFilename = 0;
	const char *mountIso = 0;
	const char *screenshotFilename = 0;
	bool readMount = false;
	double timeout = -1.0;

	for (int i = 1; i < argc; i++)
	{
		if (readMount)
		{
			mountIso = argv[i];
			readMount = false;
			continue;
		}
		if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--mount"))
			readMount = true;
		else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--log"))
			fullLog = true;
		else if (!strcmp(argv[i], "-i"))
			useJit = false;
		else if (!strcmp(argv[i], "-j"))
			useJit = true;
		else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--compare"))
			autoCompare = true;
		else if (!strncmp(argv[i], "--graphics=", strlen("--graphics=")) && strlen(argv[i]) > strlen("--graphics="))
		{
			const char *gpuName = argv[i] + strlen("--graphics=");
			if (!strcasecmp(gpuName, "gles"))
				gpuCore = GPU_GLES;
			else if (!strcasecmp(gpuName, "software"))
				gpuCore = GPU_SOFTWARE;
			else if (!strcasecmp(gpuName, "directx9"))
				gpuCore = GPU_DIRECTX9;
			else if (!strcasecmp(gpuName, "null"))
				gpuCore = GPU_NULL;
			else
			{
				printUsage(argv[0], "Unknown gpu backend specified after --graphics=");
				return 1;
			}
		}
		// Default to GLES if no value selected.
		else if (!strcmp(argv[i], "--graphics"))
			gpuCore = GPU_GLES;
		else if (!strncmp(argv[i], "--screenshot=", strlen("--screenshot=")) && strlen(argv[i]) > strlen("--screenshot="))
			screenshotFilename = argv[i] + strlen("--screenshot=");
		else if (!strncmp(argv[i], "--timeout=", strlen("--timeout=")) && strlen(argv[i]) > strlen("--timeout="))
			timeout = strtod(argv[i] + strlen("--timeout="), NULL);
		else if (!strcmp(argv[i], "--teamcity"))
			teamCityMode = true;
		else if (bootFilename == 0)
			bootFilename = argv[i];
		else
		{
			if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
				printUsage(argv[0], NULL);
			else
			{
				std::string reason = "Unexpected argument " + std::string(argv[i]);
				printUsage(argv[0], reason.c_str());
			}
			return 1;
		}
	}

	if (readMount)
	{
		printUsage(argv[0], "Missing argument after -m");
		return 1;
	}
	if (!bootFilename)
	{
		printUsage(argv[0], argc <= 1 ? NULL : "No executable specified");
		return 1;
	}

	HeadlessHost *headlessHost = getHost(gpuCore);
	host = headlessHost;

	std::string error_string;
	bool glWorking = host->InitGL(&error_string);

	LogManager::Init();
	LogManager *logman = LogManager::GetInstance();
	
	PrintfLogger *printfLogger = new PrintfLogger();

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		logman->SetEnable(type, fullLog);
		logman->SetLogLevel(type, LogTypes::LDEBUG);
		logman->AddListener(type, printfLogger);
	}

	std::string output;

	CoreParameter coreParameter;
	coreParameter.cpuCore = useJit ? CPU_JIT : CPU_INTERPRETER;
	coreParameter.gpuCore = glWorking ? gpuCore : GPU_NULL;
	coreParameter.enableSound = false;
	coreParameter.fileToStart = bootFilename;
	coreParameter.mountIso = mountIso ? mountIso : "";
	coreParameter.startPaused = false;
	coreParameter.enableDebugging = false;
	coreParameter.printfEmuLog = !autoCompare;
	if (autoCompare)
		coreParameter.collectEmuLog = &output;
	coreParameter.headLess = true;
	coreParameter.renderWidth = 480;
	coreParameter.renderHeight = 272;
	coreParameter.outputWidth = 480;
	coreParameter.outputHeight = 272;
	coreParameter.pixelWidth = 480;
	coreParameter.pixelHeight = 272;
	coreParameter.unthrottle = true;

	g_Config.bEnableSound = false;
	g_Config.bFirstRun = false;
	g_Config.bIgnoreBadMemAccess = true;
	// Never report from tests.
	g_Config.sReportHost = "";
	g_Config.bAutoSaveSymbolMap = false;
	g_Config.iRenderingMode = 0;
	g_Config.bHardwareTransform = true;
#ifdef USING_GLES2
	g_Config.iAnisotropyLevel = 0;
#else
	g_Config.iAnisotropyLevel = 8;
#endif
	g_Config.bVertexCache = true;
	g_Config.bTrueColor = true;
	g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	g_Config.iTimeFormat = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
	g_Config.bEncryptSave = true;
	g_Config.sNickName = "shadow";
	g_Config.iTimeZone = 60;
	g_Config.iDateFormat = PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY;
	g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;
	g_Config.iLockParentalLevel = 9;
	g_Config.iInternalResolution = 1;

#if defined(ANDROID)
#elif defined(BLACKBERRY) || defined(__SYMBIAN32__)
#elif !defined(_WIN32)
	g_Config.memCardDirectory = std::string(getenv("HOME"))+"/.ppsspp/";
	g_Config.flashDirectory = g_Config.memCardDirectory+"/flash/";
#endif

	if (teamCityMode) {
		// Kinda ugly, trying to guesstimate the test name from filename...
		teamCityName = ChopEnd(ChopFront(ChopFront(bootFilename, "tests/"), "pspautotests/tests/"), ".prx");
	}

	if (!PSP_Init(coreParameter, &error_string)) {
		fprintf(stderr, "Failed to start %s. Error: %s\n", coreParameter.fileToStart.c_str(), error_string.c_str());
		printf("TESTERROR\n");
		TeamCityPrint("##teamcity[testIgnored name='%s' message='PRX/ELF missing']\n", teamCityName.c_str());
		return 1;
	}

	TeamCityPrint("##teamcity[testStarted name='%s' captureStandardOutput='true']\n", teamCityName.c_str());

	host->BootDone();

	if (screenshotFilename != 0)
		headlessHost->SetComparisonScreenshot(screenshotFilename);

	time_update();
	bool doCompare = true;
	double deadline = timeout < 0.0 ? std::numeric_limits<float>::infinity() : time_now() + timeout;

	coreState = CORE_RUNNING;
	while (coreState == CORE_RUNNING)
	{
		int blockTicks = usToCycles(1000000 / 10);
		PSP_RunLoopFor(blockTicks);

		// If we were rendering, this might be a nice time to do something about it.
		if (coreState == CORE_NEXTFRAME) {
			coreState = CORE_RUNNING;
			headlessHost->SwapBuffers();
		}
		time_update();
		if (time_now() > deadline) {
			// Don't compare, print the output at least up to this point, and bail.
			printf("%s", output.c_str());
			doCompare = false;

			host->SendDebugOutput("TIMEOUT\n");
			TeamCityPrint("##teamcity[testFailed name='%s' message='Test timeout']\n", teamCityName.c_str());
			Core_Stop();
		}
	}

	host->ShutdownGL();
	PSP_Shutdown();

	headlessHost->FlushDebugOutput();

	delete host;
	host = NULL;
	headlessHost = NULL;

	if (autoCompare && doCompare)
		CompareOutput(bootFilename, output);

	TeamCityPrint("##teamcity[testFinished name='%s']\n", teamCityName.c_str());

	return 0;
}

