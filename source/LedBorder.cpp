#include "LedBorder.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace ffglex;

enum ParamID : FFUInt32
{
	PT_EFFECT,
	PT_SPEED,
	PT_BPM_SYNC,
	PT_REVERSE,
	PT_SEGMENTS,
	PT_TAIL,
	PT_SIDES,
	PT_ROWS,
	PT_LED_SIZE,
	PT_SHAPE,
	PT_GAP,
	PT_GLOW,
	PT_OFF_LEVEL,
	PT_COLOR_MODE,
	PT_HUE1,
	PT_SAT1,
	PT_BRI1,
	PT_ALPHA1,
	PT_HUE2,
	PT_SAT2,
	PT_BRI2,
	PT_ALPHA2,
	PT_BRIGHTNESS,
	PT_STROBE,
	PT_AUDIO,
	PT_FFT,
	PT_EFFECT_CHANGE,
	PT_COLOR_CHANGE,
	PT_TRANSPARENT,
};

enum MusicTrigger
{
	TRIG_OFF,
	TRIG_BAR,
	TRIG_BEAT,
	TRIG_KICK,
};

static const char* EFFECT_NAMES[] = {
	"Chase", "Comet", "Rainbow Flow", "Fill & Clear", "Ping-Pong", "Sparkle", "Breathe",
	"Theater", "Wave", "Row Pulse", "Corner Burst", "Mirror Chase", "Audio VU",
};
static const int NUM_EFFECTS      = 13;
static const int EFFECT_AUDIO_VU  = 12;
static const unsigned int FFT_LEN = 2048;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< LedBorder >,
	"QLED",                                      // Plugin unique ID (4 chars)
	"LED Border",                                // Plugin name
	2,                                           // API major version
	1,                                           // API minor version
	1,                                           // Plugin major version
	0,                                           // Plugin minor version
	FF_SOURCE,                                   // Plugin type
	"LED-matrix frame with running border effects",// Description
	"LED Border FFGL by QM LED"                  // About
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
uniform float uLaps;       // animation phase (laps around the frame)
uniform float uTime;       // real seconds
uniform int   uEffect;
uniform int   uSegments;
uniform float uTail;
uniform int   uSides;      // 0 all, 1 top+bottom, 2 left+right, 3 top, 4 bottom
uniform int   uRows;
uniform float uLedSize;
uniform int   uShape;      // 0 round, 1 square, 2 pixel
uniform float uGap;
uniform float uGlow;
uniform float uOffLevel;
uniform int   uColorMode;  // 0 single, 1 alternate, 2 gradient, 3 rainbow
uniform vec4  uColor1;
uniform vec4  uColor2;
uniform float uBrightness;
uniform float uGate;       // strobe 0/1
uniform float uAudio;      // bass boost 0..1
uniform float uLevel;      // loudness 0..1 for Audio VU
uniform float uHueShift;
uniform bool  uReverse;
uniform bool  uTransparent;

const float TAU = 6.28318530718;

float hash12( vec2 p )
{
	vec3 p3 = fract( vec3( p.xyx ) * 0.1031 );
	p3 += dot( p3, p3.yzx + 33.33 );
	return fract( ( p3.x + p3.y ) * p3.z );
}

vec3 hsv2rgb( vec3 c )
{
	vec3 p = abs( fract( c.xxx + vec3( 0.0, 2.0 / 3.0, 1.0 / 3.0 ) ) * 6.0 - 3.0 );
	return c.z * mix( vec3( 1.0 ), clamp( p - 1.0, 0.0, 1.0 ), c.y );
}

// distance between two positions on a loop of length L
float cdist( float a, float b, float L )
{
	float d = mod( abs( a - b ), L );
	return min( d, L - d );
}

void main()
{
	vec4 empty = uTransparent ? vec4( 0.0 ) : vec4( 0.0, 0.0, 0.0, 1.0 );

	// ---- LED grid: square cells that exactly fill the screen ----
	float aspect = uResolution.x / max( uResolution.y, 1.0 );
	int ny = int( floor( mix( 120.0, 12.0, uLedSize ) ) );
	ny     = max( ny, 2 * uRows + 2 );
	int nx = max( int( floor( aspect * float( ny ) + 0.5 ) ), 2 * uRows + 2 );

	vec2  g  = uv * vec2( nx, ny );
	ivec2 c  = clamp( ivec2( floor( g ) ), ivec2( 0 ), ivec2( nx - 1, ny - 1 ) );
	vec2  lc = fract( g ) - 0.5;// position inside the LED cell

	// ring index: 0 = outermost row of LEDs
	int r = min( min( c.x, nx - 1 - c.x ), min( c.y, ny - 1 - c.y ) );
	if( r >= uRows )
	{
		fragColor = empty;
		return;
	}

	// ---- which side of its ring this LED is on ----
	int rw = nx - 2 * r;
	int rh = ny - 2 * r;
	int x  = c.x - r;
	int y  = c.y - r;
	bool isTop = y == rh - 1, isBottom = y == 0, isLeft = x == 0, isRight = x == rw - 1;

	// ---- position along the frame, clockwise from the top-left corner ----
	// Measured on the outer ring for every row, so inner rows line up with outer ones.
	int w = nx;
	int h = ny;
	int k;
	if( isTop )        k = c.x;                                    // top, left -> right
	else if( isRight ) k = ( w - 1 ) + ( h - 1 - c.y );            // right, top -> bottom
	else if( isBottom ) k = ( w - 1 ) + ( h - 1 ) + ( w - 1 - c.x );// bottom, right -> left
	else               k = 2 * ( w - 1 ) + ( h - 1 ) + c.y;        // left, bottom -> top
	if( ( uSides == 1 && !( isTop || isBottom ) ) || ( uSides == 2 && !( isLeft || isRight ) ) ||
	    ( uSides == 3 && !isTop ) || ( uSides == 4 && !isBottom ) )
	{
		fragColor = empty;
		return;
	}

	float L    = float( 2 * ( w - 1 ) + 2 * ( h - 1 ) );
	float kGeo = float( k );
	float kf   = uReverse ? mod( L - kGeo, L ) : kGeo;
	float s    = kf / L;// 0..1 along the frame (direction aware)
	float dir  = uReverse ? -1.0 : 1.0;

	float t    = uLaps;
	float seg  = float( uSegments );
	float tl   = uTail;

	// ---- effects: brightness b of this LED ----
	float b        = 0.0;
	float segIdx   = floor( s * seg * 2.0 );
	float gpos     = 1.0 - abs( 2.0 * s - 1.0 );// gradient coordinate (seamless)
	bool  rainbowFlow = false;

	if( uEffect == 0 )// Chase
	{
		float xx = ( s - t ) * seg;
		segIdx   = floor( xx );
		b        = fract( xx ) < mix( 0.1, 0.9, tl ) ? 1.0 : 0.0;
	}
	else if( uEffect == 1 )// Comet
	{
		float xx = ( s - t ) * seg;
		segIdx   = floor( xx );
		b        = pow( fract( xx ), mix( 30.0, 1.5, tl ) );
	}
	else if( uEffect == 2 )// Rainbow Flow
	{
		b           = 1.0;
		rainbowFlow = true;
	}
	else if( uEffect == 3 )// Fill & Clear
	{
		float p = fract( t * 0.5 );
		b       = p < 0.5 ? step( s, p * 2.0 ) : step( ( p - 0.5 ) * 2.0, s );
	}
	else if( uEffect == 4 )// Ping-Pong: two heads bouncing and crossing
	{
		float pos   = abs( fract( t * 0.5 ) * 2.0 - 1.0 );
		float d     = min( abs( s - pos ), abs( s - ( 1.0 - pos ) ) ) * L;
		float width = mix( 1.0, L * 0.15, tl );
		b           = clamp( 1.0 - d / width, 0.0, 1.0 );
		b *= b;
	}
	else if( uEffect == 5 )// Sparkle
	{
		float ph  = t * 12.0 + hash12( vec2( c ) ) * 7.0;
		float rnd = hash12( vec2( c ) + floor( ph ) * 1.37 );
		b         = rnd < mix( 0.03, 0.5, tl ) ? 1.0 - fract( ph ) : 0.0;
	}
	else if( uEffect == 6 )// Breathe
	{
		b = 0.5 - 0.5 * cos( TAU * t );
		b = pow( b, mix( 3.0, 0.7, tl ) );
	}
	else if( uEffect == 7 )// Theater marquee
	{
		float grp   = 1.0 + floor( tl * 5.0 );
		float shift = floor( t * 24.0 );
		b           = mod( kf + shift, 3.0 * grp ) < grp ? 1.0 : 0.0;
		segIdx      = floor( ( kf + shift ) / ( 3.0 * grp ) );
	}
	else if( uEffect == 8 )// Wave
	{
		b = 0.5 + 0.5 * sin( TAU * ( s - t ) * seg );
		b = pow( b, mix( 6.0, 1.0, tl ) );
	}
	else if( uEffect == 9 )// Row Pulse: rows light up outer -> inner
	{
		float xx = fract( t * 2.0 - float( r ) / float( uRows ) );
		b        = pow( 1.0 - xx, mix( 8.0, 1.0, tl ) );
	}
	else if( uEffect == 10 )// Corner Burst
	{
		float c0 = 0.0, c1 = float( w - 1 ), c2 = float( w - 1 + h - 1 ), c3 = float( 2 * ( w - 1 ) + h - 1 );
		float dk = min( min( cdist( kGeo, c0, L ), cdist( kGeo, c1, L ) ), min( cdist( kGeo, c2, L ), cdist( kGeo, c3, L ) ) );
		float maxD  = float( max( w, h ) ) * 0.5;
		float front = ( uReverse ? 1.0 - fract( t ) : fract( t ) ) * maxD;
		float width = mix( 1.0, maxD * 0.4, tl );
		b           = clamp( 1.0 - abs( dk - front ) / width, 0.0, 1.0 );
		gpos        = dk / maxD;
	}
	else if( uEffect == 11 )// Mirror Chase: comets run from top centre down both sides
	{
		float m  = cdist( kGeo, float( w - 1 ) * 0.5, L ) / ( L * 0.5 );
		float xx = ( m - dir * t ) * seg;
		segIdx   = floor( xx );
		b        = pow( fract( xx ), mix( 30.0, 1.5, tl ) );
		gpos     = m;
	}
	else// Audio VU: fills up both sides from the bottom centre
	{
		float m = cdist( kGeo, float( w - 1 ) + float( h - 1 ) + float( w - 1 ) * 0.5, L ) / ( L * 0.5 );
		b       = m <= uLevel ? 1.0 : 0.0;
		gpos    = m;
	}

	if( uEffect != 12 )
		b *= 1.0 + uAudio * 1.2;

	// ---- colour ----
	vec3 c1 = uColor1.rgb * uColor1.a;
	vec3 c2 = uColor2.rgb * uColor2.a;
	vec3 ledCol;
	if( rainbowFlow )
		ledCol = hsv2rgb( vec3( fract( ( s - t ) * max( 1.0, seg * 0.5 ) + uHueShift ), 1.0, 1.0 ) ) * uColor1.a;
	else if( uColorMode == 1 )
		ledCol = mod( segIdx, 2.0 ) < 1.0 ? c1 : c2;
	else if( uColorMode == 2 )
		ledCol = mix( c1, c2, clamp( gpos, 0.0, 1.0 ) );
	else if( uColorMode == 3 )
		ledCol = hsv2rgb( vec3( fract( s + uTime * 0.05 + uHueShift ), 1.0, 1.0 ) ) * uColor1.a;
	else
		ledCol = c1;

	// ---- draw the LED ----
	float aa  = 1.5 * float( ny ) / max( uResolution.y, 1.0 );// ~1.5 px in cell units
	float rad = 0.5 * ( 1.0 - uGap * 0.8 );
	float d;
	if( uShape == 0 )      d = length( lc );
	else if( uShape == 1 ) d = max( abs( lc.x ), abs( lc.y ) );
	else { d = 0.0; rad = 0.5; }

	float led  = 1.0 - smoothstep( rad - aa, rad, d );
	float hot  = led * ( 1.0 - smoothstep( 0.0, rad, d ) ) * 0.35;// brighter centre
	float halo = ( 1.0 - led ) * exp( -max( d - rad, 0.0 ) * mix( 18.0, 4.0, uGlow ) ) * uGlow;

	vec3 col = ledCol * b * uBrightness * 3.0 * uGate * ( led * ( 1.0 + hot ) + halo );
	col += vec3( 0.55 ) * uOffLevel * 0.45 * led * ( 1.0 - clamp( b * uGate, 0.0, 1.0 ) );// unlit LED

	col = vec3( 1.0 ) - exp( -col );
	float alpha = uTransparent ? clamp( max( col.r, max( col.g, col.b ) ), 0.0, 1.0 ) : 1.0;
	fragColor   = vec4( col, alpha );
}
)";

LedBorder::LedBorder()
{
	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	color1.hue = 0.55f;// cyan-blue
	color2.hue = 0.85f;// magenta

	SetOptionParamInfo( PT_EFFECT, "Effect", NUM_EFFECTS, 0.0f );
	for( int i = 0; i < NUM_EFFECTS; ++i )
		SetParamElementInfo( PT_EFFECT, i, EFFECT_NAMES[ i ], (float)i );

	SetParamInfof( PT_SPEED, "Speed", FF_TYPE_STANDARD );
	SetParamInfo( PT_BPM_SYNC, "BPM Sync", FF_TYPE_BOOLEAN, bpmSync );
	SetParamInfo( PT_REVERSE, "Reverse", FF_TYPE_BOOLEAN, reverse );

	SetParamInfo( PT_SEGMENTS, "Segments", FF_TYPE_INTEGER, segments );
	SetParamRange( PT_SEGMENTS, 1.0f, 16.0f );
	if( auto* info = FindParamInfo( PT_SEGMENTS ) )
		info->defaultFloatVal = segments;

	SetParamInfof( PT_TAIL, "Length", FF_TYPE_STANDARD );

	SetOptionParamInfo( PT_SIDES, "Sides", 5, 0.0f );
	SetParamElementInfo( PT_SIDES, 0, "All", 0.0f );
	SetParamElementInfo( PT_SIDES, 1, "Top & Bottom", 1.0f );
	SetParamElementInfo( PT_SIDES, 2, "Left & Right", 2.0f );
	SetParamElementInfo( PT_SIDES, 3, "Top", 3.0f );
	SetParamElementInfo( PT_SIDES, 4, "Bottom", 4.0f );

	SetParamInfo( PT_ROWS, "Rows", FF_TYPE_INTEGER, rows );
	SetParamRange( PT_ROWS, 1.0f, 8.0f );
	if( auto* info = FindParamInfo( PT_ROWS ) )
		info->defaultFloatVal = rows;

	SetParamInfof( PT_LED_SIZE, "LED Size", FF_TYPE_STANDARD );
	SetOptionParamInfo( PT_SHAPE, "LED Shape", 3, 0.0f );
	SetParamElementInfo( PT_SHAPE, 0, "Round", 0.0f );
	SetParamElementInfo( PT_SHAPE, 1, "Square", 1.0f );
	SetParamElementInfo( PT_SHAPE, 2, "Pixel", 2.0f );
	SetParamInfof( PT_GAP, "Gap", FF_TYPE_STANDARD );
	SetParamInfof( PT_GLOW, "Glow", FF_TYPE_STANDARD );
	SetParamInfof( PT_OFF_LEVEL, "Unlit LEDs", FF_TYPE_STANDARD );

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
	SetParamInfof( PT_BRIGHTNESS, "Intensity", FF_TYPE_STANDARD );

	SetParamInfof( PT_STROBE, "Strobe", FF_TYPE_STANDARD );
	SetParamInfof( PT_AUDIO, "Audio React", FF_TYPE_STANDARD );
	SetBufferParamInfo( PT_FFT, "FFT", FFT_LEN, FF_USAGE_FFT );

	const char* triggerNames[] = { "Off", "Every Bar", "Every Beat", "On Kick" };
	SetOptionParamInfo( PT_EFFECT_CHANGE, "Effect Change", 4, 0.0f );
	SetOptionParamInfo( PT_COLOR_CHANGE, "Color Change", 4, 0.0f );
	for( int i = 0; i < 4; ++i )
	{
		SetParamElementInfo( PT_EFFECT_CHANGE, i, triggerNames[ i ], (float)i );
		SetParamElementInfo( PT_COLOR_CHANGE, i, triggerNames[ i ], (float)i );
	}
	SetParamInfo( PT_TRANSPARENT, "Transparent BG", FF_TYPE_BOOLEAN, transparent );

	for( FFUInt32 i = PT_EFFECT; i <= PT_SIDES; ++i )
		SetParamGroup( i, "Effect" );
	for( FFUInt32 i = PT_ROWS; i <= PT_OFF_LEVEL; ++i )
		SetParamGroup( i, "LED Matrix" );
	for( FFUInt32 i = PT_COLOR_MODE; i <= PT_BRIGHTNESS; ++i )
		SetParamGroup( i, "Color" );
	for( FFUInt32 i = PT_STROBE; i <= PT_COLOR_CHANGE; ++i )
		SetParamGroup( i, "Music & FX" );
}

float LedBorder::Random01()
{
	rng = rng * 1664525u + 1013904223u;
	return ( rng >> 8 ) * ( 1.0f / 16777216.0f );
}

FFResult LedBorder::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( vertexShaderCode, fragmentShaderCode ) || !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	firstFrame = true;
	return CFFGLPlugin::InitGL( vp );
}

FFResult LedBorder::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
	return FF_SUCCESS;
}

void LedBorder::UpdateClock()
{
	auto now  = std::chrono::steady_clock::now();
	double dt = firstFrame ? 0.0 : std::chrono::duration< double >( now - lastTick ).count();
	firstFrame = false;
	lastTick   = now;
	dt         = std::min( std::max( dt, 0.0 ), 0.1 );
	realTime += dt;

	// Bar / beat detection from Resolume's transport (4/4).
	barHit        = barPhase + 0.5f < lastBarPhase;
	int beatIndex = std::min( 3, std::max( 0, (int)std::floor( barPhase * 4.0f ) ) );
	beatHit       = lastBeatIndex >= 0 && beatIndex != lastBeatIndex;
	lastBeatIndex = beatIndex;
	lastBarPhase  = barPhase;
	if( barHit )
		bars += 1.0;

	if( bpmSync && bpm > 0.0f )
	{
		// Speed picks laps per bar: 1/4, 1/2, 1, 2, 4
		double rate = std::pow( 2.0, std::round( speed * 4.0 ) - 2.0 );
		laps        = ( bars + barPhase ) * rate;
	}
	else
	{
		laps += dt * ( speed * speed * 1.2 );// up to 1.2 laps / s
	}
}

void LedBorder::UpdateAudio()
{
	kickHit               = false;
	const ParamInfo* info = FindParamInfo( PT_FFT );
	if( info == nullptr || info->elements.empty() )
		return;
	size_t n = info->elements.size();

	// Bass (kick) band
	size_t to  = std::max< size_t >( 2, n / 128 );
	float bass = 0.0f;
	for( size_t i = 1; i < to && i < n; ++i )
		bass += info->elements[ i ].value;
	bass /= (float)( to - 1 );
	bassPeak   = std::max( bass, bassPeak * 0.995f );
	float norm = bassPeak > 1e-4f ? bass / bassPeak : 0.0f;
	norm       = std::max( 0.0f, ( norm - 0.35f ) / 0.65f );

	if( kickArmed && norm > 0.55f && realTime - lastKickTime > 0.18 )
	{
		kickHit      = true;
		kickArmed    = false;
		lastKickTime = realTime;
	}
	if( norm < 0.25f )
		kickArmed = true;
	bassEnv = norm > bassEnv ? norm : bassEnv * 0.86f;

	// Overall loudness for the VU meter
	size_t top  = std::max< size_t >( 2, n / 4 );
	float level = 0.0f;
	for( size_t i = 1; i < top; ++i )
		level += info->elements[ i ].value;
	level /= (float)( top - 1 );
	levelPeak   = std::max( level, levelPeak * 0.997f );
	float lnorm = levelPeak > 1e-4f ? std::min( 1.0f, level / levelPeak ) : 0.0f;
	levelEnv    = lnorm > levelEnv ? levelEnv + ( lnorm - levelEnv ) * 0.6f : levelEnv * 0.93f;
}

FFResult LedBorder::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	(void)pGL;
	UpdateClock();

	int effTrig = (int)std::lround( effectChange );
	int colTrig = (int)std::lround( colorChange );
	int baseEffect = std::min( NUM_EFFECTS - 1, std::max( 0, (int)std::lround( effect ) ) );

	bool needAudio = audioReact > 0.0f || effTrig == TRIG_KICK || colTrig == TRIG_KICK ||
	                 baseEffect == EFFECT_AUDIO_VU;
	if( needAudio )
		UpdateAudio();
	else
		kickHit = false;

	auto fired = [ this ]( int trig ) {
		return ( trig == TRIG_BAR && barHit ) || ( trig == TRIG_BEAT && beatHit ) ||
		       ( trig == TRIG_KICK && kickHit );
	};

	// Effect change: jump to a random different effect (Audio VU is left out,
	// it only makes sense when chosen on purpose).
	if( effTrig == TRIG_OFF )
		effectOffset = 0;
	else if( fired( effTrig ) )
	{
		int current = ( baseEffect + effectOffset ) % NUM_EFFECTS;
		int next    = current;
		while( next == current || next == EFFECT_AUDIO_VU )
			next = (int)( Random01() * NUM_EFFECTS ) % NUM_EFFECTS;
		effectOffset = ( next - baseEffect + NUM_EFFECTS ) % NUM_EFFECTS;
	}
	int activeEffect = ( baseEffect + effectOffset ) % NUM_EFFECTS;

	if( colTrig == TRIG_OFF )
		hueShift = 0.0f;
	else if( fired( colTrig ) )
		hueShift = std::fmod( hueShift + 0.2f + Random01() * 0.6f, 1.0f );

	float gate = 1.0f;
	if( strobe > 0.001f )
	{
		double freq;
		if( bpmSync && bpm > 0.0f )
			freq = bpm / 60.0 * std::pow( 2.0, std::round( strobe * 3.0 ) );
		else
			freq = 1.0 + strobe * strobe * 24.0;
		gate = std::fmod( realTime * freq, 1.0 ) < 0.35 ? 1.0f : 0.0f;
	}

	float rgb1[ 3 ], rgb2[ 3 ];
	float hue1 = std::fmod( ( color1.hue >= 1.0f ? 0.0f : color1.hue ) + hueShift, 1.0f );
	float hue2 = std::fmod( ( color2.hue >= 1.0f ? 0.0f : color2.hue ) + hueShift, 1.0f );
	HSVtoRGB( hue1, color1.sat, color1.bri, rgb1[ 0 ], rgb1[ 1 ], rgb1[ 2 ] );
	HSVtoRGB( hue2, color2.sat, color2.bri, rgb2[ 0 ], rgb2[ 1 ], rgb2[ 2 ] );

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	shader.Set( "uResolution", (float)currentViewport.width, (float)currentViewport.height );
	shader.Set( "uLaps", (float)std::fmod( laps, 1000.0 ) );
	shader.Set( "uTime", (float)std::fmod( realTime, 10000.0 ) );
	shader.Set( "uEffect", activeEffect );
	shader.Set( "uSegments", std::min( 16, std::max( 1, (int)std::lround( segments ) ) ) );
	shader.Set( "uTail", tail );
	shader.Set( "uSides", (int)std::lround( sides ) );
	shader.Set( "uRows", std::min( 8, std::max( 1, (int)std::lround( rows ) ) ) );
	shader.Set( "uLedSize", ledSize );
	shader.Set( "uShape", (int)std::lround( shape ) );
	shader.Set( "uGap", gap );
	shader.Set( "uGlow", glow );
	shader.Set( "uOffLevel", offLevel );
	shader.Set( "uColorMode", (int)std::lround( colorMode ) );
	shader.Set( "uColor1", rgb1[ 0 ], rgb1[ 1 ], rgb1[ 2 ], color1.alpha );
	shader.Set( "uColor2", rgb2[ 0 ], rgb2[ 1 ], rgb2[ 2 ], color2.alpha );
	shader.Set( "uBrightness", brightness );
	shader.Set( "uGate", gate );
	shader.Set( "uAudio", bassEnv * audioReact );
	shader.Set( "uLevel", levelEnv );
	shader.Set( "uHueShift", hueShift );
	shader.Set( "uReverse", reverse ? 1 : 0 );
	shader.Set( "uTransparent", transparent ? 1 : 0 );

	quad.Draw();
	return FF_SUCCESS;
}

FFResult LedBorder::SetFloatParameter( unsigned int index, float value )
{
	switch( index )
	{
	case PT_EFFECT:     effect = value; break;
	case PT_SPEED:      speed = value; break;
	case PT_BPM_SYNC:
	{
		bool was = bpmSync;
		bpmSync  = value > 0.5f;
		if( bpmSync && !was )
			bars = 0.0;
		break;
	}
	case PT_REVERSE:    reverse = value > 0.5f; break;
	case PT_SEGMENTS:   segments = value; break;
	case PT_TAIL:       tail = value; break;
	case PT_SIDES:      sides = value; break;
	case PT_ROWS:       rows = value; break;
	case PT_LED_SIZE:   ledSize = value; break;
	case PT_SHAPE:      shape = value; break;
	case PT_GAP:        gap = value; break;
	case PT_GLOW:       glow = value; break;
	case PT_OFF_LEVEL:  offLevel = value; break;
	case PT_COLOR_MODE: colorMode = value; break;
	case PT_HUE1:       color1.hue = value; break;
	case PT_SAT1:       color1.sat = value; break;
	case PT_BRI1:       color1.bri = value; break;
	case PT_ALPHA1:     color1.alpha = value; break;
	case PT_HUE2:       color2.hue = value; break;
	case PT_SAT2:       color2.sat = value; break;
	case PT_BRI2:       color2.bri = value; break;
	case PT_ALPHA2:     color2.alpha = value; break;
	case PT_BRIGHTNESS: brightness = value; break;
	case PT_STROBE:     strobe = value; break;
	case PT_AUDIO:      audioReact = value; break;
	case PT_EFFECT_CHANGE: effectChange = value; break;
	case PT_COLOR_CHANGE:  colorChange = value; break;
	case PT_TRANSPARENT:   transparent = value > 0.5f; break;
	case PT_FFT:
		// FFT data arrives as buffer elements; the host still sets a default here,
		// and refusing it would make the host destroy the instance.
		break;
	default:
		return FF_FAIL;
	}
	return FF_SUCCESS;
}

float LedBorder::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_EFFECT:     return effect;
	case PT_SPEED:      return speed;
	case PT_BPM_SYNC:   return bpmSync ? 1.0f : 0.0f;
	case PT_REVERSE:    return reverse ? 1.0f : 0.0f;
	case PT_SEGMENTS:   return segments;
	case PT_TAIL:       return tail;
	case PT_SIDES:      return sides;
	case PT_ROWS:       return rows;
	case PT_LED_SIZE:   return ledSize;
	case PT_SHAPE:      return shape;
	case PT_GAP:        return gap;
	case PT_GLOW:       return glow;
	case PT_OFF_LEVEL:  return offLevel;
	case PT_COLOR_MODE: return colorMode;
	case PT_HUE1:       return color1.hue;
	case PT_SAT1:       return color1.sat;
	case PT_BRI1:       return color1.bri;
	case PT_ALPHA1:     return color1.alpha;
	case PT_HUE2:       return color2.hue;
	case PT_SAT2:       return color2.sat;
	case PT_BRI2:       return color2.bri;
	case PT_ALPHA2:     return color2.alpha;
	case PT_BRIGHTNESS: return brightness;
	case PT_STROBE:     return strobe;
	case PT_AUDIO:      return audioReact;
	case PT_EFFECT_CHANGE: return effectChange;
	case PT_COLOR_CHANGE:  return colorChange;
	case PT_TRANSPARENT:   return transparent ? 1.0f : 0.0f;
	}
	return 0.0f;
}

char* LedBorder::GetParameterDisplay( unsigned int index )
{
	static char buf[ 32 ];
	switch( index )
	{
	case PT_SEGMENTS:
		std::snprintf( buf, sizeof( buf ), "%d", (int)std::lround( segments ) );
		return buf;
	case PT_ROWS:
		std::snprintf( buf, sizeof( buf ), "%d", (int)std::lround( rows ) );
		return buf;
	case PT_SPEED:
		if( bpmSync )
		{
			static const char* rates[] = { "1x / 4 bars", "1x / 2 bars", "1x / bar", "2x / bar", "4x / bar" };
			std::snprintf( buf, sizeof( buf ), "%s", rates[ std::min( 4, std::max( 0, (int)std::lround( speed * 4.0f ) ) ) ] );
		}
		else
			std::snprintf( buf, sizeof( buf ), "%.2f laps/s", speed * speed * 1.2f );
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
