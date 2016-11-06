/*
 * BigAziz.cpp
 *
 * Created: 31/10/2016 2:59:50 PM
 * Author : Frank Tkalcevic
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <string.h>
#include <util/delay.h>

#define LED_BRIGHTNESS	255
#define LIGHT_COUNT		(10*4+8*4)
#define DEBOUNCE_TIME	20	// ms

enum class EMode: uint8_t
{
	AllOn = 0,
	OneSeg,
	TwoSeg,
	ThreeSeg,
	FourSeg,
	MaxMode
};
EMode mode = EMode::AllOn;
uint8_t submode = 0;

uint8_t intensity[LIGHT_COUNT];
//uint8_t globalBrightness = 0x1F | 0xE0;
uint8_t globalBrightness = 0;
#define MAX_GLOBAL_BRIGHTNESS	31

#define FromAB(a,b)		(((a)<<3) | ((b)<<2))
#define ToAB(a,b)		(((a)<<1) | (b))
static int8_t EncoderMap[16];
volatile static uint8_t nLastEncoder;
volatile static int16_t nEncoder;
volatile static uint16_t ms;
volatile static bool bKeyDown = false;
volatile static bool bKeyUp = false;
uint8_t rgbColor;


#define RGB_R	_BV(PD2)
#define RGB_G	_BV(PD3)
#define RGB_B	_BV(PD4)
#define RGB_MASK (RGB_R|RGB_G|RGB_B)


extern "C"
{
	void __cxa_pure_virtual(void);
	void __cxa_pure_virtual(void) {};

	__extension__ typedef int __guard __attribute__((mode (__DI__)));

	int __cxa_guard_acquire(__guard *);
	void __cxa_guard_release (__guard *);
	void __cxa_guard_abort (__guard *);

	int __cxa_guard_acquire(__guard *g) {return !(*(char *)(g));};
	void __cxa_guard_release (__guard *g) {*(char *)g = 1;};
	void __cxa_guard_abort (__guard *) {};
}

static void SetRGB( uint8_t r, uint8_t g, uint8_t b )
{
	rgbColor = (r?0:_BV(PD2)) | (g?0:_BV(PD3)) | (b?0:_BV(PD4));
	PORTD = (PORTD & ~(RGB_MASK)) | rgbColor;
}

static void FlashOff()
{
	PORTD |= RGB_MASK;
}

static void FlashOn()
{
	PORTD = (PORTD & ~(RGB_MASK)) | rgbColor;
}

ISR(PCINT2_vect)
{
	// Encoder input
	nLastEncoder <<= 2;
	nLastEncoder |= ((PIND >> 6) & 3);
	nEncoder -= EncoderMap[nLastEncoder & 0XF];
}

ISR(TIMER0_COMPA_vect)
{
	static uint8_t nDebounce = 0;
	static uint8_t nRelease = 0;

	ms++;

	// Flash encoder lights
	if ( submode == 1 )
	{
		if ( (ms & 0x1FF ) == 0x100 )
			FlashOff();
		else if ( (ms & 0x1FF ) == 0x000 )
			FlashOn();
	}
	else if ( submode == 2 )
	{
		if ( (ms & 0xFF ) == 0x80 )
			FlashOff();
		else if ( (ms & 0xFF ) == 0x00 )
			FlashOn();
	}

	// Sw input (and debounce)
	uint8_t sw = PIND & _BV(PD5);
	if ( sw )
	{
		if ( nRelease != 0 )
		{
			nRelease = DEBOUNCE_TIME;
		}
		else if ( nDebounce != 0 )
		{
			nDebounce--;
			if ( nDebounce == 0 )
			{
				bKeyDown = true;
				nRelease = DEBOUNCE_TIME;
			}
		}
		else
		{
			nDebounce = DEBOUNCE_TIME;
		}
	}
	else
	{
		if ( nRelease != 0 )
		{
			nRelease--;
			if ( nRelease == 0 )
				bKeyUp = true;
		}
		nDebounce = 0;
	}
}

static void Send()
{
	// Start frame
	for ( uint8_t i = 0; i < 4; i++ )
	{
		SPDR = 0;
		while ( !(SPSR & _BV(SPIF)) );
	}

	// Data
	for ( uint8_t i = 0; i < LIGHT_COUNT; i++ )
	{
		SPDR = globalBrightness | 0xE0;
		while ( !(SPSR & _BV(SPIF)) );
		uint8_t n = intensity[i];
		SPDR = n;
		while ( !(SPSR & _BV(SPIF)) );
		SPDR = n;
		while ( !(SPSR & _BV(SPIF)) );
		SPDR = n;
		while ( !(SPSR & _BV(SPIF)) );
	}
	//// END Frame
	for ( uint8_t i = 0; i < 6; i++ )
	{
		SPDR = 0xFF;
		while ( !(SPSR & _BV(SPIF)) );
	}
}


static void ioinit()
{
	DDRB = _BV(PB5) | _BV(PB3) | _BV(PB2);								// SPI outptus
	SPCR = _BV(SPE) | _BV(MSTR) | _BV(SPI2X);	// SPI clk/128 (8MHz/2 = 4MHz)

	//RGB
	DDRD = _BV(PD2) | _BV(PD3) | _BV(PD4);
	PORTD = _BV(PD2) | _BV(PD3) | _BV(PD4);

	// Sw and Enc PD5, PD6/PD7 (PCINT22/23)
	PCICR |= _BV(PCIE2);
	PCMSK2 |= _BV(PCINT22) | _BV(PCINT23);		// pin change interupt
	PORTD |=  _BV(PD6) | _BV(PD7);	// pullups
	// Sw has physical pull down

	// ms timer
	TCCR0A = _BV(WGM01);	// CTC
	TCCR0B = _BV(CS01) | _BV(CS00);		// clk/64
	OCR0A = 125;	// 1000Hz
	TIMSK0 = _BV(OCIE0A);



    memset( EncoderMap, 0, sizeof(EncoderMap) );
    EncoderMap[FromAB(1,0) + ToAB(1,1)] = +1;
    EncoderMap[FromAB(1,0) + ToAB(0,0)] = -1;
    EncoderMap[FromAB(1,1) + ToAB(0,1)] = +1;
    EncoderMap[FromAB(1,1) + ToAB(1,0)] = -1;
    EncoderMap[FromAB(0,1) + ToAB(0,0)] = +1;
    EncoderMap[FromAB(0,1) + ToAB(1,1)] = -1;
    EncoderMap[FromAB(0,0) + ToAB(1,0)] = +1;
    EncoderMap[FromAB(0,0) + ToAB(0,1)] = -1;

	nLastEncoder = PIND & 0x3;
}


void SetSubmode( EMode mode, uint8_t subMode )
{
	if ( mode == EMode::AllOn )
	{
		submode = 0;
	}
	else 
	{
		submode = subMode;
	}
}

template<int n>
class MultiSegDisplay
{
private:
	uint8_t seg_start;
	uint8_t seg_length;
public:
	MultiSegDisplay()
	{
		seg_start = 0;
		seg_length = LIGHT_COUNT/(n+1);
	}

	void CalcSeg()
	{
		memset( intensity, 0, sizeof(intensity) );
		for ( uint8_t i= 0; i < seg_length; i++ )
			for ( uint8_t j=0; j < n; j++ )
			{
				intensity[(seg_start+i+j*LIGHT_COUNT/n)%LIGHT_COUNT] = LED_BRIGHTNESS;
			}
	}

	bool ProcessSeg( int8_t delta, int8_t deltaEncoder )
	{
		if ( submode == 1 )
		{
			seg_start = (seg_start + LIGHT_COUNT + deltaEncoder) % LIGHT_COUNT;
		}
		else if ( submode == 2 )
		{
			seg_length += delta;
			if ( seg_length < 1 )
				seg_length = 1;
			else if ( seg_length > LIGHT_COUNT/n )
				seg_length = LIGHT_COUNT/n;
		}
		CalcSeg();
		Send();

		return true;
	}
};


MultiSegDisplay<1> OneSeg;
MultiSegDisplay<2> TwoSeg;
MultiSegDisplay<3> ThreeSeg;
MultiSegDisplay<4> FourSeg;

static void SetMode( EMode newMode )
{
	mode = newMode;
	submode = 0;
	switch ( mode )
	{
		case EMode::AllOn:		
			SetRGB(1,1,1); 
			memset( intensity, LED_BRIGHTNESS, sizeof(intensity) );
			break;
		case EMode::OneSeg:		
			SetRGB(1,0,0); 
			OneSeg.CalcSeg();
			break;
		case EMode::TwoSeg:		
			SetRGB(0,1,0); 
			TwoSeg.CalcSeg();
			break;
		case EMode::ThreeSeg:	
			SetRGB(0,0,1); 
			ThreeSeg.CalcSeg();
			break;
		case EMode::FourSeg:	
			SetRGB(1,1,0); 
			FourSeg.CalcSeg();
			break;
	}
	Send();
}

static int8_t deltaEncoder = 0;
static void ProcessEncoder( int8_t delta )
{
	deltaEncoder += delta;

	int8_t nDelta = 0;
	bool bChange = false;
	while ( deltaEncoder >= 4 )
	{
		nDelta--;
		deltaEncoder -= 4;
	}
	while ( deltaEncoder <= -4 )
	{
		nDelta++;
		deltaEncoder += 4;
	}

	if ( submode == 0 )
	{
		if ( nDelta < 0 && globalBrightness > 0 )
		{
			globalBrightness--;
			bChange = true;
		}
		else if ( nDelta > 0 && globalBrightness < MAX_GLOBAL_BRIGHTNESS )
		{
			globalBrightness++;
			bChange = true;
		}

		if ( bChange )
			Send();
	}
	else if ( mode == EMode::OneSeg )
	{
		OneSeg.ProcessSeg(nDelta, delta);
	}
	else if ( mode == EMode::TwoSeg )
	{
		TwoSeg.ProcessSeg(nDelta, delta);
	}
	else if ( mode == EMode::ThreeSeg )
	{
		ThreeSeg.ProcessSeg(nDelta, delta);
	}
	else if ( mode == EMode::FourSeg )
	{
		FourSeg.ProcessSeg(nDelta, delta);
	}
}

int main(void)
{
	ioinit();

	memset( intensity, 0, sizeof(intensity) );
	Send();
	 
	static int16_t nLastEncoderValue = -1;
	sei();

	SetMode( EMode::AllOn );

	bool bSoftStarting = true;
	uint16_t nSoftStartTime = ms;
	uint16_t nDownTimeStart = 0;
	bool bOn = false;
    while (1) 
    {

		if ( bSoftStarting )
		{
			uint16_t Time = ms - nSoftStartTime;
			if ( Time > 30 )
			{
				globalBrightness++;
				Send();
				if ( globalBrightness >= MAX_GLOBAL_BRIGHTNESS/2 )
					bSoftStarting = false;
				else
					nSoftStartTime = ms;
			}
		}

		if ( nLastEncoderValue != nEncoder )
		{
			ProcessEncoder( nEncoder - nLastEncoderValue );
			nLastEncoderValue = nEncoder;
		}

		if ( bKeyDown )
		{
			bKeyDown = false;
			nDownTimeStart = ms;
			// hack so we can use nDownTime != 0 as keydown flag
			if ( nDownTimeStart == 0 )
				nDownTimeStart--;
		}

		if ( nDownTimeStart )
		{
			uint16_t nDownTime = ms - nDownTimeStart;
			if ( bKeyUp )
			{
				bKeyUp = false;
				if ( nDownTime < 1000 )
				{
					if ( submode == 0 )
					{
						EMode newMode = (EMode)((uint8_t)mode + 1);
						if ( newMode == EMode::MaxMode )
							newMode = EMode::AllOn;
						SetMode( newMode );
					}
					else
					{
						SetSubmode( mode, 0 );
						SetMode(mode);
					}
				}
				nDownTimeStart = 0;
			}

			if ( nDownTime > 2000 )			// 2 seconds
			{
				SetSubmode( mode, 2 );				
			}
			else if ( nDownTime > 1000 )	// 1 second
			{
				SetSubmode( mode, 1 );
			}
		}
    }
}


/*

Modes 
	1white
		- light, knob adjusts intensity
	2
		- 1 lot of lights
			- knob adjusts intensity
			- hold button 1 second
				rotate
			- hold button 2 seconds
				resize
	3	- 2 lots
	4	- 3 lots
	5	- 4 lots
*/