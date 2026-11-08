#pragma once
#include <FFGLSDK.h>
#include <chrono>

// Laser Beams - FFGL source plugin for Resolume Avenue / Arena (7.3.1+)
// Draws glowing, hazy laser beams (fan, starburst, parallel scan, crossfire)
// that sweep, sync to BPM, strobe and react to audio.
class LaserBeams : public CFFGLPlugin
{
public:
	LaserBeams();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

private:
	void UpdateClock();
	float UpdateAudio();

	struct HSBA
	{
		float hue = 0.0f, sat = 1.0f, bri = 1.0f, alpha = 1.0f;
	};

	// Parameter values (all 0..1 unless noted)
	float pattern     = 0.0f;// option index
	float beams       = 8.0f;// integer 1..32
	float spread      = 0.6f;
	float phaseOffset = 0.0f;
	float sweep       = 0.5f;
	float speed       = 0.3f;
	bool bpmSync      = false;
	float rotation    = 0.5f;
	float originX     = 0.5f;
	float originY     = 0.95f;
	float width       = 0.25f;
	float glow        = 0.5f;
	float haze        = 0.4f;
	float length      = 0.7f;
	float intensity   = 0.5f;
	float colorMode   = 0.0f;// option index
	HSBA color1;
	HSBA color2;
	float strobe      = 0.0f;
	float audioReact  = 0.0f;
	bool transparent  = true;
	float patternChange = 0.0f;// option: off / bar / beat / kick
	float colorChange   = 0.0f;// option: off / bar / beat / kick

	// Runtime state
	std::chrono::steady_clock::time_point lastTick;
	bool firstFrame     = true;
	double realTime     = 0.0;// seconds since start
	double motionCycles = 0.0;// accumulated sweep cycles
	double bars         = 0.0;// whole bars counted (BPM sync)
	float lastBarPhase  = 0.0f;
	float bassEnv       = 0.0f;
	float bassPeak      = 0.05f;

	// Music triggers (set for one frame when they fire)
	bool barHit         = false;
	bool beatHit        = false;
	bool kickHit        = false;
	int lastBeatIndex   = -1;
	bool kickArmed      = true;
	double lastKickTime = -1.0;
	int patternOffset   = 0;    // added to the chosen Pattern
	float hueShift      = 0.0f; // added to all hues
	unsigned int rng    = 12345u;
	float Random01();

	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;
};
