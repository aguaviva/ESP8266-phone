
#include <Arduino.h>
#include "AudioIn.h"

static short buffer[4][AUDIO_IN_FRAMESIZE];
volatile static int bufPos = 0;
volatile static int bufLen = 0;

volatile static int state = 1;

static short * ICACHE_RAM_ATTR GetNewBuffer()
{
  if (bufLen<4)
  {
    if (state == 1)
    {
      Serial.print('S'); //signal no more dropped buffers
      state = 0;
    }

    return buffer[(bufPos + (bufLen++))&0x3];
  }

  if (state == 0)
  {
    Serial.print('d'); //signal dropped buffer
    state = 1;
  }

  return NULL;
}

static short *Lock()
{
  if (bufLen>1)
    return buffer[bufPos];
  else
    return NULL;
}

static void Unlock()
{
    noInterrupts();
    bufPos = (bufPos+1) & 0x3;
    bufLen--;
    interrupts();
}

short *currBuffer = NULL;
volatile static int dataPos = 0;

void Put(short value)
{
    if (currBuffer==NULL)
    {
        currBuffer = GetNewBuffer();
        if (currBuffer==NULL)
        {
            return;
        }
    }

    currBuffer[dataPos++] = value;

	// If the buffer is full, signal it's ready to be sent and switch to the other one
    if (dataPos >= AUDIO_IN_FRAMESIZE)
    {
        dataPos = 0;
        currBuffer = GetNewBuffer();
    }
}

void BufferDebug()
{
  ESP.wdtFeed();

  for(;;)
  {
    short *data = Lock();
    if (data==NULL)
      break;
    Serial.printf("chunk: %p, %i %i\n", data, bufPos, bufLen);

    for(int i=0;i<160;i++)
    {
      Serial.printf("%i ", data[i]);
    }
    Serial.printf("\n");
    ESP.wdtFeed();
    Unlock();
  }
  Serial.printf("bufptr: %i\n", dataPos);
}

short *aiLock()
{
    return Lock();
}

void aiUnlock()
{
  Unlock();
}

static GetSampleFn ReadMic=NULL;

void ICACHE_RAM_ATTR sample_isr()
{
  Put(ReadMic());
}

void aiBegin(GetSampleFn pfn, unsigned int sampleRate)
{
  ReadMic = pfn;

  Serial.printf("timer - begin\n");
  dataPos = 0;

  timer1_isr_init();
  timer1_attachInterrupt(sample_isr);

  // 80Mhz/16 * 5

  int period = 125;//(1000000/8000);
  timer1_write(clockCyclesPerMicrosecond() * period);//* sampleRate); //125us = 8kHz sampling freq
  timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
}

void aiEnd()
{
  timer1_disable();
  ReadMic=NULL;
}
