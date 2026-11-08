#include "LaserBeams.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace ffglex;

enum ParamID : FFUInt32
{
	PT_PATTERN,
	PT_BEAMS,
	PT_SPREAD,
	PT_PHASE_OFFSET,
	PT_SWEEP,
	PT_SPEED,
	PT_BPM_SYNC,
	PT_ROTATION,
	PT_ORIGIN_X,
	PT_ORIGIN_Y,
	PT_WIDTH,
	PT_GLOW,
	PT_HAZE,
	PT_LENGTH,
	PT_INTENSITY,
	PT_COLOR_MODE,
	PT_HUE1,
	PT_SAT1,
	PT_BRI1,
	PT_ALPHA1,
	PT_HUE2,
	PT_SAT2,
	PT_BRI2,
	PT_ALPHA2,
	PT_STROBE,
	PT_AUDIO,
	PT_FFT,
	PT_TRANSPARENT,
	PT_PATTERN_CHANGE,
	PT_COLOR_CHANGE,
};

// Options for "Pattern Change" / "Color Change"
enum MusicTrigger
{
	TRIG_OFF,
	TRIG_BAR,
	TRIG_BEAT,
	TRIG_KICK,
};

static const int MAX_BEAMS        = 32;
static const unsigned int FFT_LEN = 2048;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< LaserBeams >,                          // Create method
	"QLZR",                                               // Plugin unique ID (4 chars)
	"Laser Beams",                                        // Plugin name
	2,                                                    // API major version
	1,                                                    // API minor version
	1,                                                    // Plugin major version
	0,                                                    // Plugin minor version
	FF_SOURCE,                                            // Plugin type
	"Glowing laser beams: fan, starburst, scan, crossfire",// Description
	"Laser Beams FFGL by QM LED"                          // About
);

static const char vertexShaderCode[] = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

static const char fragmentShaderCode[] = R"(#version 410 core
in vec2 uv;
out vec4 fragColor;

uniform vec2  uResolution;
uniform float uCycles;      // sweep cycles (speed / BPM driven)
uniform float uTime;        // real seconds, for haze drift + rainbow
uniform int   uPattern;     // 0 fan, 1 starburst, 2 parallel, 3 crossfire
uniform int   uBeams;
uniform float uSpread;
uniform float uPhaseOffset;
uniform float uSweep;
uniform float uRotation;    // radians
uniform vec2  uOrigin;      // 0..1 screen position
uniform float uWidth;
uniform float uGlow;
uniform float uHaze;
uniform float uLength;
uniform float uIntensity;
uniform int   uColorMode;   // 0 single, 1 alternate, 2 gradient, 3 rainbow
uniform vec4  uColor1;
uniform vec4  uColor2;
uniform float uGate;        // strobe gate 0/1
uniform float uAudio;       // 0..1 audio boost
uniform bool  uTransparent;
uniform float uHueShift;    // music-driven colour change

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

float hash( vec2 p )
{
	p = fract( p * vec2( 123.34, 456.21 ) );
	p += dot( p, p + 45.32 );
	return fract( p.x * p.y );
}

float vnoise( vec2 p )
{
	vec2 i = floor( p );
	vec2 f = fract( p );
	f = f * f * ( 3.0 - 2.0 * f );
	float a = hash( i );
	float b = hash( i + vec2( 1.0, 0.0 ) );
	float c = hash( i + vec2( 0.0, 1.0 ) );
	float d = hash( i + vec2( 1.0, 1.0 ) );
	return mix( mix( a, b, f.x ), mix( c, d, f.x ), f.y );
}

vec3 hsv2rgb( vec3 c )
{
	vec3 p = abs( fract( c.xxx + vec3( 0.0, 2.0 / 3.0, 1.0 / 3.0 ) ) * 6.0 - 3.0 );
	return c.z * mix( vec3( 1.0 ), clamp( p - 1.0, 0.0, 1.0 ), c.y );
}

vec3 beamColor( int i, float f )
{
	if( uColorMode == 1 )
		return ( i % 2 == 0 ) ? uColor1.rgb * uColor1.a : uColor2.rgb * uColor2.a;
	if( uColorMode == 2 )
		return mix( uColor1.rgb * uColor1.a, uColor2.rgb * uColor2.a, f );
	if( uColorMode == 3 )
		return hsv2rgb( vec3( fract( f + uTime * 0.08 + uHueShift ), 1.0, 1.0 ) ) * uColor1.a;
	return uColor1.rgb * uColor1.a;
}

// Light contribution of one beam that starts at o and travels along angle a.
float beam( vec2 p, vec2 o, float a, float w, float id )
{
	vec2 d     = vec2( cos( a ), sin( a ) );
	vec2 rel   = p - o;
	float along = dot( rel, d );
	float dist  = abs( rel.x * d.y - rel.y * d.x );

	float behind = smoothstep( -0.015, 0.0, along );// no light behind the source
	along        = max( along, 0.0 );

	float ww   = w * ( 1.0 + along * 0.8 );// slight divergence
	float core = exp( -( dist * dist ) / ( ww * ww ) );
	float halo = ( ww * 4.0 ) / ( dist + ww * 4.0 );
	halo = uGlow * 0.45 * halo * halo * halo;

	float fade = exp( -along * mix( 4.0, 0.15, uLength ) );

	// Haze / smoke: light scattered unevenly along the beam
	float n    = vnoise( vec2( along * 5.0 - uTime * 0.7, id * 3.7 ) ) * 0.6
	           + vnoise( vec2( along * 13.0 + uTime * 0.4, id * 1.3 + 9.0 ) ) * 0.4;
	float hz   = mix( 1.0, 0.25 + 1.5 * n, uHaze );

	// Hot spot at the source
	float hot  = exp( -length( rel ) / ( w * 6.0 ) ) * 0.6;

	return ( ( core + halo ) * fade * hz + hot ) * behind;
}

void main()
{
	float aspect = uResolution.x / max( uResolution.y, 1.0 );
	vec2 p = ( uv - 0.5 ) * vec2( aspect, 1.0 );
	vec2 o = ( uOrigin - 0.5 ) * vec2( aspect, 1.0 );

	float w    = mix( 0.0008, 0.02, uWidth * uWidth ) * ( 1.0 + uAudio * 0.8 );
	float swing = sin( TAU * uCycles );

	vec3 col = vec3( 0.0 );
	for( int i = 0; i < 32; ++i )
	{
		if( i >= uBeams )
			break;

		float f   = ( uBeams > 1 ) ? float( i ) / float( uBeams - 1 ) : 0.5;
		float ph  = sin( TAU * uCycles + f * uPhaseOffset * TAU );// per-beam wave
		float id  = float( i );
		vec3  bc  = beamColor( i, f );
		float lum = 0.0;

		if( uPattern == 0 )// Fan: beams spread from one point
		{
			float a = -PI * 0.5 + uRotation + ( f - 0.5 ) * uSpread * PI * 0.95
			        + ph * uSweep * 0.7;
			lum = beam( p, o, a, w, id );
		}
		else if( uPattern == 1 )// Starburst: full circle, spinning
		{
			float a = uRotation + TAU * ( float( i ) / float( uBeams ) ) + uCycles * TAU * 0.25
			        + ph * uSweep * 0.4 * uSpread;
			lum = beam( p, o, a, w, id );
		}
		else if( uPattern == 2 )// Parallel scan: a row of sources, beams move together
		{
			vec2  oi = o + vec2( ( f - 0.5 ) * uSpread * aspect * 1.8, 0.0 );
			float a  = -PI * 0.5 + uRotation + ph * uSweep * 0.7;
			lum = beam( p, oi, a, w, id );
		}
		else// Crossfire: two mirrored fans from the sides
		{
			vec2  oa  = vec2( o.x - 0.45 * aspect, o.y );
			vec2  ob  = vec2( -oa.x, o.y );
			float aim = atan( -oa.y, -oa.x ) + uRotation;
			float a   = aim + ( f - 0.5 ) * uSpread * PI * 0.6 + ph * uSweep * 0.5;
			lum = beam( p, oa, a, w, id ) + beam( p, ob, PI - a, w, id + 50.0 );
		}

		col += bc * lum;
	}

	col *= uIntensity * uGate * ( 1.0 + uAudio * 1.5 );
	col = vec3( 1.0 ) - exp( -col );// soft tone-map keeps the hot core white-ish

	float alpha = uTransparent ? clamp( max( col.r, max( col.g, col.b ) ), 0.0, 1.0 ) : 1.0;
	fragColor = vec4( col, alpha );
}
)";

LaserBeams::LaserBeams()
{
	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	color1.hue = 0.33f;// green
	color2.hue = 0.0f; // red

	SetOptionParamInfo( PT_PATTERN, "Pattern", 4, 0.0f );
	SetParamElementInfo( PT_PATTERN, 0, "Fan", 0.0f );
	SetParamElementInfo( PT_PATTERN, 1, "Starburst", 1.0f );
	SetParamElementInfo( PT_PATTERN, 2, "Parallel Scan", 2.0f );
	SetParamElementInfo( PT_PATTERN, 3, "Crossfire", 3.0f );

	SetParamInfo( PT_BEAMS, "Beams", FF_TYPE_INTEGER, beams );
	SetParamRange( PT_BEAMS, 1.0f, (float)MAX_BEAMS );
	if( auto* info = FindParamInfo( PT_BEAMS ) )
		info->defaultFloatVal = beams;

	SetParamInfof( PT_SPREAD, "Spread", FF_TYPE_STANDARD );
	SetParamInfof( PT_PHASE_OFFSET, "Wave", FF_TYPE_STANDARD );
	SetParamInfof( PT_SWEEP, "Sweep", FF_TYPE_STANDARD );
	SetParamInfof( PT_SPEED, "Speed", FF_TYPE_STANDARD );
	SetParamInfo( PT_BPM_SYNC, "BPM Sync", FF_TYPE_BOOLEAN, bpmSync );
	SetParamInfof( PT_ROTATION, "Rotation", FF_TYPE_STANDARD );
	SetParamInfof( PT_ORIGIN_X, "Origin X", FF_TYPE_XPOS );
	SetParamInfof( PT_ORIGIN_Y, "Origin Y", FF_TYPE_YPOS );

	SetParamInfof( PT_WIDTH, "Width", FF_TYPE_STANDARD );
	SetParamInfof( PT_GLOW, "Glow", FF_TYPE_STANDARD );
	SetParamInfof( PT_HAZE, "Haze", FF_TYPE_STANDARD );
	SetParamInfof( PT_LENGTH, "Length", FF_TYPE_STANDARD );
	SetParamInfof( PT_INTENSITY, "Intensity", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_COLOR_MODE, "Color Mode", 4, 0.0f );
	SetParamElementInfo( PT_COLOR_MODE, 0, "Single", 0.0f );
	SetParamElementInfo( PT_COLOR_MODE, 1, "Alternate", 1.0f );
	SetParamElementInfo( PT_COLOR_MODE, 2, "Gradient", 2.0f );
	SetParamElementInfo( PT_COLOR_MODE, 3, "Rainbow", 3.0f );

	SetParamInfof( PT_HUE1, "Color 1", FF_TYPE_HUE );
	SetParamInfof( PT_SAT1, "Saturation 1", FF_TYPE_SATURATION );
	SetParamInfof( PT_BRI1, "Brightness 1", FF_TYPE_BRIGHTNESS );
	SetParamInfof( PT_ALPHA1, "Alpha 1", FF_TYPE_ALPHA );
	SetParamInfof( PT_HUE2, "Color 2", FF_TYPE_HUE );
	SetParamInfof( PT_SAT2, "Saturation 2", FF_TYPE_SATURATION );
	SetParamInfof( PT_BRI2, "Brightness 2", FF_TYPE_BRIGHTNESS );
	SetParamInfof( PT_ALPHA2, "Alpha 2", FF_TYPE_ALPHA );

	SetParamInfof( PT_STROBE, "Strobe", FF_TYPE_STANDARD );
	SetParamInfof( PT_AUDIO, "Audio React", FF_TYPE_STANDARD );
	SetBufferParamInfo( PT_FFT, "FFT", FFT_LEN, FF_USAGE_FFT );
	SetParamInfo( PT_TRANSPARENT, "Transparent BG", FF_TYPE_BOOLEAN, transparent );

	const char* triggerNames[] = { "Off", "Every Bar", "Every Beat", "On Kick" };
	SetOptionParamInfo( PT_PATTERN_CHANGE, "Pattern Change", 4, 0.0f );
	SetOptionParamInfo( PT_COLOR_CHANGE, "Color Change", 4, 0.0f );
	for( int t = 0; t < 4; ++t )
	{
		SetParamElementInfo( PT_PATTERN_CHANGE, t, triggerNames[ t ], (float)t );
		SetParamElementInfo( PT_COLOR_CHANGE, t, triggerNames[ t ], (float)t );
	}

	for( FFUInt32 i = PT_PATTERN; i <= PT_ORIGIN_Y; ++i )
		SetParamGroup( i, "Shape & Motion" );
	for( FFUInt32 i = PT_WIDTH; i <= PT_INTENSITY; ++i )
		SetParamGroup( i, "Look" );
	for( FFUInt32 i = PT_COLOR_MODE; i <= PT_ALPHA2; ++i )
		SetParamGroup( i, "Color" );
	for( FFUInt32 i = PT_STROBE; i <= PT_FFT; ++i )
		SetParamGroup( i, "FX" );
	SetParamGroup( PT_PATTERN_CHANGE, "Music" );
	SetParamGroup( PT_COLOR_CHANGE, "Music" );
}

float LaserBeams::Random01()
{
	rng = rng * 1664525u + 1013904223u;
	return ( rng >> 8 ) * ( 1.0f / 16777216.0f );
}

FFResult LaserBeams::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( vertexShaderCode, fragmentShaderCode ) )
	{
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	firstFrame = true;
	return CFFGLPlugin::InitGL( vp );
}

FFResult LaserBeams::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
	return FF_SUCCESS;
}

void LaserBeams::UpdateClock()
{
	auto now = std::chrono::steady_clock::now();
	double dt = 0.0;
	if( !firstFrame )
		dt = std::chrono::duration< double >( now - lastTick ).count();
	firstFrame = false;
	lastTick   = now;
	dt         = std::min( std::max( dt, 0.0 ), 0.1 );// avoid jumps after a pause
	realTime += dt;

	// Bar / beat detection from Resolume's transport (4 beats per bar).
	barHit  = barPhase + 0.5f < lastBarPhase;
	int beatIndex = std::min( 3, std::max( 0, (int)std::floor( barPhase * 4.0f ) ) );
	beatHit = lastBeatIndex >= 0 && beatIndex != lastBeatIndex;
	lastBeatIndex = beatIndex;
	lastBarPhase  = barPhase;
	if( barHit )
		bars += 1.0;

	if( bpmSync && bpm > 0.0f )
	{
		// Lock the sweep to Resolume's bar phase. Speed picks the rate:
		// 0 = 1 sweep / 4 bars ... 1 = 4 sweeps / bar.
		double rate  = std::pow( 2.0, std::round( speed * 4.0 ) - 2.0 );// 1/4 .. 4
		motionCycles = ( bars + barPhase ) * rate;
	}
	else
	{
		motionCycles += dt * ( speed * speed * 2.5 );// up to 2.5 sweeps / s
	}
}

float LaserBeams::UpdateAudio()
{
	kickHit = false;
	const ParamInfo* info = FindParamInfo( PT_FFT );
	if( info == nullptr || info->elements.empty() )
		return 0.0f;

	// Low bins = bass/kick. Resolume's FFT covers 0..Nyquist across the buffer.
	size_t n    = info->elements.size();
	size_t from = 1;
	size_t to   = std::max< size_t >( from + 1, n / 128 );// ~0-180 Hz at 48 kHz
	float sum   = 0.0f;
	for( size_t i = from; i < to && i < n; ++i )
		sum += info->elements[ i ].value;
	float level = sum / (float)( to - from );

	// Automatic gain: follow the recent peak so quiet and loud tracks both pulse.
	bassPeak = std::max( level, bassPeak * 0.995f );
	float norm = bassPeak > 1e-4f ? level / bassPeak : 0.0f;
	norm       = std::max( 0.0f, ( norm - 0.35f ) / 0.65f );// gate out the floor

	// Kick: bass jumps high after having dropped, at most ~5 per second.
	if( kickArmed && norm > 0.55f && realTime - lastKickTime > 0.18 )
	{
		kickHit      = true;
		kickArmed    = false;
		lastKickTime = realTime;
	}
	if( norm < 0.25f )
		kickArmed = true;

	// Fast attack, smooth release.
	bassEnv = norm > bassEnv ? norm : bassEnv * 0.86f;
	return bassEnv;
}

FFResult LaserBeams::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	(void)pGL;
	UpdateClock();
	int patTrig = (int)std::lround( patternChange );
	int colTrig = (int)std::lround( colorChange );
	bool needAudio = audioReact > 0.0f || patTrig == TRIG_KICK || colTrig == TRIG_KICK;
	float bass  = needAudio ? UpdateAudio() : 0.0f;
	float audio = bass * audioReact;

	auto fired = [ this ]( int trig ) {
		return ( trig == TRIG_BAR && barHit ) || ( trig == TRIG_BEAT && beatHit ) ||
		       ( trig == TRIG_KICK && kickHit );
	};

	// Pattern change: jump to a different random pattern.
	if( patTrig == TRIG_OFF )
		patternOffset = 0;
	else if( fired( patTrig ) )
		patternOffset = ( patternOffset + 1 + (int)( Random01() * 2.999f ) ) % 4;
	int activePattern = ( (int)std::lround( pattern ) + patternOffset ) % 4;

	// Color change: jump hue by a clearly visible amount (72..288 degrees).
	if( colTrig == TRIG_OFF )
		hueShift = 0.0f;
	else if( fired( colTrig ) )
		hueShift = std::fmod( hueShift + 0.2f + Random01() * 0.6f, 1.0f );

	float gate = 1.0f;
	if( strobe > 0.001f )
	{
		double freq;
		if( bpmSync && bpm > 0.0f )
			freq = bpm / 60.0 * std::pow( 2.0, std::round( strobe * 3.0 ) );// 1/4 .. 1/32 notes
		else
			freq = 1.0 + strobe * strobe * 24.0;// 1..25 Hz
		gate = std::fmod( realTime * freq, 1.0 ) < 0.35 ? 1.0f : 0.0f;
	}

	float rgb1[ 3 ], rgb2[ 3 ];
	float hue1 = std::fmod( ( color1.hue >= 1.0f ? 0.0f : color1.hue ) + hueShift, 1.0f );
	float hue2 = std::fmod( ( color2.hue >= 1.0f ? 0.0f : color2.hue ) + hueShift, 1.0f );
	HSVtoRGB( hue1, color1.sat, color1.bri, rgb1[ 0 ], rgb1[ 1 ], rgb1[ 2 ] );
	HSVtoRGB( hue2, color2.sat, color2.bri, rgb2[ 0 ], rgb2[ 1 ], rgb2[ 2 ] );

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	shader.Set( "uResolution", (float)currentViewport.width, (float)currentViewport.height );
	shader.Set( "uCycles", (float)std::fmod( motionCycles, 1000.0 ) );
	shader.Set( "uTime", (float)std::fmod( realTime, 10000.0 ) );
	shader.Set( "uPattern", activePattern );
	shader.Set( "uBeams", std::min( MAX_BEAMS, std::max( 1, (int)std::lround( beams ) ) ) );
	shader.Set( "uSpread", spread );
	shader.Set( "uPhaseOffset", phaseOffset );
	shader.Set( "uSweep", sweep );
	shader.Set( "uRotation", ( rotation - 0.5f ) * 6.2831853f );
	shader.Set( "uOrigin", originX, originY );
	shader.Set( "uWidth", width );
	shader.Set( "uGlow", glow );
	shader.Set( "uHaze", haze );
	shader.Set( "uLength", length );
	shader.Set( "uIntensity", intensity * 3.0f );
	shader.Set( "uColorMode", (int)std::lround( colorMode ) );
	shader.Set( "uColor1", rgb1[ 0 ], rgb1[ 1 ], rgb1[ 2 ], color1.alpha );
	shader.Set( "uColor2", rgb2[ 0 ], rgb2[ 1 ], rgb2[ 2 ], color2.alpha );
	shader.Set( "uGate", gate );
	shader.Set( "uAudio", audio );
	shader.Set( "uTransparent", transparent ? 1 : 0 );
	shader.Set( "uHueShift", hueShift );

	quad.Draw();
	return FF_SUCCESS;
}

FFResult LaserBeams::SetFloatParameter( unsigned int index, float value )
{
	switch( index )
	{
	case PT_PATTERN:      pattern = value; break;
	case PT_BEAMS:        beams = value; break;
	case PT_SPREAD:       spread = value; break;
	case PT_PHASE_OFFSET: phaseOffset = value; break;
	case PT_SWEEP:        sweep = value; break;
	case PT_SPEED:        speed = value; break;
	case PT_BPM_SYNC:
	{
		bool wasSynced = bpmSync;
		bpmSync        = value > 0.5f;
		if( bpmSync && !wasSynced )
			bars = 0.0;
		break;
	}
	case PT_ROTATION:     rotation = value; break;
	case PT_ORIGIN_X:     originX = value; break;
	case PT_ORIGIN_Y:     originY = value; break;
	case PT_WIDTH:        width = value; break;
	case PT_GLOW:         glow = value; break;
	case PT_HAZE:         haze = value; break;
	case PT_LENGTH:       length = value; break;
	case PT_INTENSITY:    intensity = value; break;
	case PT_COLOR_MODE:   colorMode = value; break;
	case PT_HUE1:         color1.hue = value; break;
	case PT_SAT1:         color1.sat = value; break;
	case PT_BRI1:         color1.bri = value; break;
	case PT_ALPHA1:       color1.alpha = value; break;
	case PT_HUE2:         color2.hue = value; break;
	case PT_SAT2:         color2.sat = value; break;
	case PT_BRI2:         color2.bri = value; break;
	case PT_ALPHA2:       color2.alpha = value; break;
	case PT_STROBE:       strobe = value; break;
	case PT_AUDIO:        audioReact = value; break;
	case PT_TRANSPARENT:  transparent = value > 0.5f; break;
	case PT_PATTERN_CHANGE: patternChange = value; break;
	case PT_COLOR_CHANGE:   colorChange = value; break;
	default:
		return FF_FAIL;
	}
	return FF_SUCCESS;
}

float LaserBeams::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_PATTERN:      return pattern;
	case PT_BEAMS:        return beams;
	case PT_SPREAD:       return spread;
	case PT_PHASE_OFFSET: return phaseOffset;
	case PT_SWEEP:        return sweep;
	case PT_SPEED:        return speed;
	case PT_BPM_SYNC:     return bpmSync ? 1.0f : 0.0f;
	case PT_ROTATION:     return rotation;
	case PT_ORIGIN_X:     return originX;
	case PT_ORIGIN_Y:     return originY;
	case PT_WIDTH:        return width;
	case PT_GLOW:         return glow;
	case PT_HAZE:         return haze;
	case PT_LENGTH:       return length;
	case PT_INTENSITY:    return intensity;
	case PT_COLOR_MODE:   return colorMode;
	case PT_HUE1:         return color1.hue;
	case PT_SAT1:         return color1.sat;
	case PT_BRI1:         return color1.bri;
	case PT_ALPHA1:       return color1.alpha;
	case PT_HUE2:         return color2.hue;
	case PT_SAT2:         return color2.sat;
	case PT_BRI2:         return color2.bri;
	case PT_ALPHA2:       return color2.alpha;
	case PT_STROBE:       return strobe;
	case PT_AUDIO:        return audioReact;
	case PT_TRANSPARENT:  return transparent ? 1.0f : 0.0f;
	case PT_PATTERN_CHANGE: return patternChange;
	case PT_COLOR_CHANGE:   return colorChange;
	}
	return 0.0f;
}

char* LaserBeams::GetParameterDisplay( unsigned int index )
{
	static char buf[ 32 ];
	switch( index )
	{
	case PT_BEAMS:
		std::snprintf( buf, sizeof( buf ), "%d", (int)std::lround( beams ) );
		return buf;
	case PT_ROTATION:
		std::snprintf( buf, sizeof( buf ), "%.0f deg", ( rotation - 0.5f ) * 360.0f );
		return buf;
	case PT_SPEED:
		if( bpmSync )
		{
			static const char* rates[] = { "1x / 4 bars", "1x / 2 bars", "1x / bar", "2x / bar", "4x / bar" };
			int i = std::min( 4, std::max( 0, (int)std::lround( speed * 4.0f ) ) );
			std::snprintf( buf, sizeof( buf ), "%s", rates[ i ] );
		}
		else
			std::snprintf( buf, sizeof( buf ), "%.2f /s", speed * speed * 2.5f );
		return buf;
	case PT_STROBE:
		if( strobe <= 0.001f )
			std::snprintf( buf, sizeof( buf ), "Off" );
		else if( bpmSync )
		{
			static const char* notes[] = { "1/4", "1/8", "1/16", "1/32" };
			std::snprintf( buf, sizeof( buf ), "%s", notes[ std::min( 3, (int)std::lround( strobe * 3.0f ) ) ] );
		}
		else
			std::snprintf( buf, sizeof( buf ), "%.1f Hz", 1.0f + strobe * strobe * 24.0f );
		return buf;
	}
	return CFFGLPlugin::GetParameterDisplay( index );
}
