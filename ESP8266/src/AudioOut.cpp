#include <Arduino.h>

#include <FS.h>

#include <i2s.h>
#include <math.h>
#include "AudioOut.h"


//---------------------------------------------------------------
static int init_cnt = 0;

void aoBegin(int samplingRate)
{
    if (init_cnt==0)
    {
        i2s_begin();
        i2s_set_rate(samplingRate);
    }
    init_cnt++;
}

void aoEnd()
{
    init_cnt--;
    if (init_cnt==0)
    {
        delay(100);
        i2s_end();
    }
}

int16_t aoQueue(int16_t *data, int len)
{
    return i2s_write_buffer_mono(data, len);
}

void PlaySin()
{
    //Serial.printf("PlaySin");

    short wave[8];
    for(int i=0;i<8;i++)
    {
        wave[i] = 16000*sin((float)i*2.0*3.14159/8.0);
    }

    aoBegin(8000);
    int size = 8000 * 10;
    for(int i=0;i<size;)
    {
        int16_t b=i2s_available();
        if (b>0)
        {
            i+=b;
            if (i>size)
                b = i-size;

            for(int k=0;k<b;k++)
            {
                short v = wave[k&7];
                uint32_t s32 = (v<<16) | (v & 0xffff);
                i2s_write_sample(s32);
            }
            ESP.wdtFeed();
        }
        else
        {
            delay((64*1000)/8000);
            ESP.wdtFeed();
        }
    }

    aoEnd();
}

int my_write_buffer_mono(short *data, int len)
{
    int16_t b=i2s_available();
    len = len<b?len:b;

    for(int i=0;i<len;i++)
    {
        signed short v = data[i] ;
        uint32_t s32 = (v<<16) | (v & 0xffff);
        i2s_write_sample(s32);
    }

    return len;
}

void PlayHolaRaw()
{
    fs::File f = SPIFFS.open("/hola.raw", "r");
    signed short data[1000];

    aoBegin(8000);
    for(;;)
    {
        int size = f.readBytes((char*)data, 1000*2)/2;
        if (size==0)
            break;

            int written = 0;
            while(written<size)
            {
                int to_write = size-written;
                int just_written = my_write_buffer_mono(&data[written], to_write);
                if (just_written<to_write)
                {
                    ESP.wdtFeed();
                }
                written += just_written;
            }
     }

    aoEnd();
    f.close();
}


void PlayHolaRawBuf()
{
    fs::File f = SPIFFS.open("/hola.raw", "r");
    signed short data[1000];

    aoBegin(8000);

    for(;;)
    {
        int size = f.readBytes((char*)data, 1000*2)/2;
        if (size==0)
            break;

        int written = 0;
        while(written<size)
        {
            int to_write = size-written;
            int just_written = i2s_write_buffer_mono(&data[written], to_write);
            if (just_written<to_write)
            {
                ESP.wdtFeed();
            }
            written += just_written;
        }
    }

    aoEnd();
    f.close();
}

void PlayHolaLPC(openlpc_decoder_state *st)
{
    fs::File f = SPIFFS.open("/hola.lpc", "r");

    unsigned char params[OPENLPC_ENCODED_FRAME_SIZE];
    signed short data[160];

    aoBegin(8000);

    for(;;)
    {
        //read coeffs
        int size = f.readBytes((char*)params, OPENLPC_ENCODED_FRAME_SIZE);
        if (size==0)
            break;

        //decode
        openlpc_decode(params, data, st);
        size = 160;

        int written = 0;
        while(written<size)
        {
            int to_write = size-written;
            int just_written = i2s_write_buffer_mono(&data[written], to_write);
            if (just_written<to_write)
            {
                ESP.wdtFeed();
            }
            written += just_written;
        }
    }

    aoEnd();
    f.close();
}
