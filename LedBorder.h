#pragma once
#include <FFGLSDK.h>
#include <chrono>

// LED Border - FFGL source plugin for Resolume Avenue / Arena (7.3.1+)
// Simulates an LED-matrix frame running around the edge of the screen:
// individual round / square LEDs, dim "off" LEDs, 13 chase effects,
// BPM sync, auto effect/colour changes on bar / beat / kick, audio VU.
class LedBorder : public CFFGLPlugin
{
public:
	LedBorder();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

private:
	void UpdateClock();
	void UpdateAudio();
	float Random01();

	struct HSBA
	{
		float hue = 0.0f, sat = 1.0f, bri = 1.0f, alpha = 1.0f;
	};

	// Parameter values (0..1 unless noted)
	float effect      = 0.0f;// option index
	float speed       = 0.35f;
	bool bpmSync      = false;
	bool reverse      = false;
	float segments    = 4.0f;// integer 1..16
	float tail        = 0.5f;
	float sides       = 0.0f;// option index
	float rows        = 2.0f;// integer 1..8
	float ledSize     = 0.3f;
	float shape       = 0.0f;// option index
	float gap         = 0.3f;
	float glow        = 0.5f;
	float offLevel    = 0.25f;
	float colorMode   = 0.0f;// option index
	HSBA color1;
	HSBA color2;
	float brightness  = 0.6f;
	float strobe      = 0.0f;
	float audioReact  = 0.0f;
	float effectChange = 0.0f;// option: off / bar / beat / kick
	float colorChange  = 0.0f;// option: off / bar / beat / kick
	bool transparent  = true;

	// Runtime state
	std::chrono::steady_clock::time_point lastTick;
	bool firstFrame     = true;
	double realTime     = 0.0;
	double laps         = 0.0;// accumulated animation phase
	double bars         = 0.0;
	float lastBarPhase  = 0.0f;
	int lastBeatIndex   = -1;
	bool barHit = false, beatHit = false, kickHit = false;

	float bassEnv       = 0.0f;
	float bassPeak      = 0.05f;
	float levelEnv      = 0.0f;// smoothed overall loudness for VU
	float levelPeak     = 0.05f;
	bool kickArmed      = true;
	double lastKickTime = -1.0;

	int effectOffset    = 0;
	float hueShift      = 0.0f;
	unsigned int rng    = 98765u;

	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;
};
